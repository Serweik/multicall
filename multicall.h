/*
 * multicall.h
 *
 *  Created on: 3 апр. 2022 г.
 *      Author: Serweik
 */

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <any>
#include <functional>
#include <cassert>
#include <memory>
#include <map>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <string.h>

#define MC_DECLARE_INTERFACE(interfaceType) using __MC_IINTERFACE = interfaceType;
#define MC_DECLARE_SIGNAL(signal_name) virtual void signal_name final {};

//Just for test. Don't use it.
#define MC_CONFIG_USE_SPINLOCK 0

namespace multicall 
{

#if MC_CONFIG_USE_SPINLOCK == 1
	struct SpinSharedMutex {
		std::atomic<int> unique_lock_counter{ 0 };
		std::atomic<int> shared_lock_counter{ 0 };
		std::atomic_flag unique_lock_flag{ ATOMIC_FLAG_INIT };

		inline void lock() noexcept {
			while (unique_lock_flag.test_and_set(std::memory_order_acquire));
			++unique_lock_counter;
			while (shared_lock_counter);
		}
		inline void unlock() noexcept {
			--unique_lock_counter;
			unique_lock_flag.clear(std::memory_order_release);
		}

		inline void lock_shared() noexcept {
			while (unique_lock_counter);
			++shared_lock_counter;
		}
		inline void unlock_shared() noexcept {
			--shared_lock_counter;
		}
	};
#else
	struct SpinSharedMutex {
		std::atomic<int> unique_lock_counter{ 0 }; //To avoid situation when shared_lock is alwais get the lock and unique_lock is wait most time
		std::shared_mutex mutex;

		inline void lock() noexcept {
			++unique_lock_counter;
			mutex.lock();
		}
		inline void unlock() noexcept {
			--unique_lock_counter;
			mutex.unlock();
		}

		inline void lock_shared() noexcept {
			while (unique_lock_counter);
			mutex.lock_shared();
		}
		inline void unlock_shared() noexcept {
			mutex.unlock_shared();
		}
	};
#endif


	class McFunctionId;
	class MultiCallBase;

	class McFunctionIdImpl final {
	public:
		McFunctionIdImpl() = default;
		~McFunctionIdImpl() noexcept { deleteImpl(); }
		McFunctionIdImpl(const McFunctionIdImpl& other) noexcept : m_impl(other.clone_impl_to(m_small_starage_buffer)) {}
		McFunctionIdImpl(McFunctionIdImpl&& other) noexcept : m_impl(other.move_impl_to(m_small_starage_buffer)) {}

		McFunctionIdImpl& operator = (const McFunctionIdImpl& other) {
			if (&other == this) { return *this; }
			deleteImpl();
			m_impl = other.clone_impl_to(m_small_starage_buffer);
			return *this;
		}
		McFunctionIdImpl& operator = (McFunctionIdImpl&& other) {
			if (&other == this) { return *this; }
			deleteImpl();
			m_impl = other.move_impl_to(m_small_starage_buffer);
			return *this;
		}

		bool isValid() const noexcept { return bool(m_impl); }

		struct InternalImplBase {
			virtual ~InternalImplBase() = default;
			virtual InternalImplBase* clone_to(void* buffer) noexcept = 0;
			virtual InternalImplBase* clone_to() noexcept = 0;
			virtual InternalImplBase* move_to(void* buffer) noexcept = 0;
			virtual InternalImplBase* move_to() noexcept = 0;

			virtual std::pair<const uint8_t* const, size_t> rawData() const noexcept = 0;
			virtual MultiCallBase* getObject() const noexcept = 0;

			size_t hash() const noexcept {
				const auto [data, size] = rawData();
				size_t result = 0;
				if (size % sizeof(size_t) == 0) {
					const size_t new_size = size / sizeof(size_t);
					const size_t* new_data = reinterpret_cast<const size_t*>(data);
					for (size_t i = 0; i < new_size; ++i) {
						hash_combine(result, new_data[i]);
					}
				}else {
					for (size_t i = 0; i < size; ++i) {
						hash_combine(result, data[i]);
					}
				}
				return result;
			};
			bool compare(InternalImplBase* other) const noexcept {
				const auto [data, size] = rawData();
				const auto [other_data, other_size] = other->rawData();
				if (size == other_size) {
					if (memcmp(data, other_data, size) == 0) {
						return true;
					}
				}
				return false;
			}
			template <class T>
			inline void hash_combine(std::size_t& s, const T& v) const noexcept {
				static const std::hash<T> h;
				s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
			}
		};

		template<class T, class F>
		struct IdMemberStorage {
			T* object;
			F func;
		};

		template<class F>
		struct IdFunctionStorage {
			F func;
		};

		template<class T, class F>
		struct InternalImplMember : InternalImplBase {
			InternalImplMember(T* obj, F func) noexcept { raw_data.storage.object = obj; raw_data.storage.func = func; }
			virtual InternalImplBase* clone_to(void* buffer) noexcept { return new(buffer) InternalImplMember<T, F>(*this); }; //for placement new only!
			virtual InternalImplBase* clone_to() noexcept {return new InternalImplMember<T, F>(*this);};
			virtual InternalImplBase* move_to(void* buffer) noexcept { return new(buffer) InternalImplMember<T, F>(std::move(*this)); }; //for placement new only!
			virtual InternalImplBase* move_to() noexcept { return this; };
			
			union RawData {
				IdMemberStorage<T, F> storage;
				uint8_t data[sizeof(IdMemberStorage<T, F>)];
			};
			RawData raw_data;
			std::pair<const uint8_t* const, size_t> rawData() const noexcept override {
				return std::make_pair(raw_data.data, sizeof(raw_data.data));
			}
			MultiCallBase* getObject() const noexcept override {
				MultiCallBase* base_prt = dynamic_cast<MultiCallBase*>(raw_data.storage.object);
				return base_prt;
			}
		};

		template<class F>
		struct InternalImplFunction : InternalImplBase {
			InternalImplFunction(F func) noexcept : raw_data{ func } {}
			virtual InternalImplBase* clone_to(void* buffer) noexcept { return new(buffer) InternalImplFunction<F>(*this); }; //for placement new only!
			virtual InternalImplBase* clone_to() noexcept { return new InternalImplFunction<F>(*this); };
			virtual InternalImplBase* move_to(void* buffer) noexcept { return new(buffer) InternalImplFunction<F>(std::move(*this)); }; //for placement new only!
			virtual InternalImplBase* move_to() noexcept { return this; };

			union RawData {
				IdFunctionStorage<F> storage;
				uint8_t data[sizeof(IdFunctionStorage<F>)];
			};
			RawData raw_data;
			std::pair<const uint8_t* const, size_t> rawData() const noexcept override {
				return std::make_pair(raw_data.data, sizeof(raw_data.data));
			}
			MultiCallBase* getObject() const noexcept override {
				return nullptr;
			}
		};

	private:
		InternalImplBase* m_impl = nullptr;
		alignas(size_t) uint8_t m_small_starage_buffer[sizeof(size_t) * 6]; // 6 is emperic. Just enough to put pointers to an object and a method with vtbl here

		static const inline size_t m_magical_constant = 0x9e3779b9;

		void deleteImpl() {
			if (m_impl) {
				if (reinterpret_cast<const size_t*>(m_small_starage_buffer)[0] == m_magical_constant &&
					reinterpret_cast<const size_t*>(m_small_starage_buffer)[1] == m_magical_constant)
				{
					delete m_impl;
				}
				else
				{
					m_impl->~InternalImplBase(); //placement new was used
				}
				m_impl = nullptr;
			}
		}

		InternalImplBase* clone_impl_to(void* buffer) const {
			if (m_impl) {
				if (reinterpret_cast<const size_t*>(m_small_starage_buffer)[0] == m_magical_constant &&
					reinterpret_cast<const size_t*>(m_small_starage_buffer)[1] == m_magical_constant)
				{
					return m_impl->clone_to();
				}
				else
				{
					return m_impl->clone_to(buffer); //placement new was used
				}
			}
			return nullptr;
		}

		InternalImplBase* move_impl_to(void* buffer) {
			if (m_impl) {
				if (reinterpret_cast<const size_t*>(m_small_starage_buffer)[0] == m_magical_constant &&
					reinterpret_cast<const size_t*>(m_small_starage_buffer)[1] == m_magical_constant)
				{
					InternalImplBase* temp = m_impl->move_to();
					m_impl = nullptr;
					return temp;
				}
				else
				{
					return m_impl->move_to(buffer); //placement new was used
				}
			}
			return nullptr;
		}


		friend McFunctionId;

		template<class T, class F>
		void setContent(T* obj, F func) noexcept {
			static_assert(std::is_member_function_pointer_v<F>);
			deleteImpl();
			if constexpr (sizeof(InternalImplMember<T, F>) < sizeof(m_small_starage_buffer)) {
				m_impl = new(m_small_starage_buffer) InternalImplMember<T, F>(obj, func);
			}else {
				reinterpret_cast<size_t*>(m_small_starage_buffer)[0] = m_magical_constant;
				reinterpret_cast<size_t*>(m_small_starage_buffer)[1] = m_magical_constant; //to understand that it wasn't used
				m_impl = new InternalImplMember<T, F>(obj, func);
			}
		}
		template<class F>
		void setContent(F func) noexcept {
			//static_assert(std::is_member_function_pointer_v<F>);
			deleteImpl();
			if constexpr (sizeof(InternalImplFunction<F>) < sizeof(m_small_starage_buffer)) {
				m_impl = new(m_small_starage_buffer) InternalImplFunction<F>(func);
			}else {
				reinterpret_cast<size_t*>(m_small_starage_buffer)[0] = m_magical_constant;
				reinterpret_cast<size_t*>(m_small_starage_buffer)[1] = m_magical_constant; //to understand that it wasn't used
				m_impl = new InternalImplFunction<F>(func);
			}
		}

		bool operator == (const McFunctionIdImpl& other) const { return m_impl->compare(other.m_impl); }
		size_t hash() const { return m_impl->hash(); }
		MultiCallBase* getObject() const noexcept { return m_impl->getObject(); }
	};

	class McFunctionId {
	public:
		McFunctionId() = default;
		template<class T, class F>
		McFunctionId(T* obj, F func) noexcept { m_impl.setContent(obj, func); }

		template<class ..._Signature, class T>
		McFunctionId(T* obj, void(T::* func)(_Signature...)) noexcept { m_impl.setContent(obj, func); }

		template<class F>
		McFunctionId(F func) noexcept { m_impl.setContent(func); }

		bool operator == (const McFunctionId& other) const noexcept {
			return (m_impl.isValid() && (m_impl.isValid() == other.m_impl.isValid())) ? m_impl == other.m_impl : false;
		}
		size_t hash() const noexcept { return m_impl.isValid() ? m_impl.hash() : 0; }
		MultiCallBase* getObject() const noexcept { return m_impl.isValid() ? m_impl.getObject() : nullptr; }

	private:
		McFunctionIdImpl m_impl;
	};

	struct McFunctionIdHash
	{
		size_t operator() (const McFunctionId& k) const noexcept {
			return k.hash();
		}
	};

	template<class... Args>
	struct McSignal {
		template<class _Sender, class _Interface>
		McSignal(_Sender* obj, void(_Interface::* func)(Args...)) 
			: m_func_id(static_cast<_Interface*>(obj), func)
		{
			static_assert(std::is_same_v<typename _Interface::__MC_IINTERFACE, _Interface>, "The type of sender must be the same as type of the interface");
		}
		McFunctionId m_func_id;
	};

	class MultiCallBase
	{
	public:
		virtual ~MultiCallBase() {
			DisconnectFromAll();
		};

		inline void DisconnectFromAll() {
			std::unique_lock locker(__m_mutex);
			for (auto& [reciever_id, senders] : __m_senders_map) {
				for (auto& sender : senders) {
					MultiCallBase* sender_obj = sender.getObject();
					if (sender_obj) {
						sender_obj->removeSubscriber(sender, reciever_id);
					}
				}
			}
			for (auto& [sender_id, recievers] : __m_mc_recievers_map) {
				for (auto& [reciever, _] : recievers) {
					MultiCallBase* reciever_obj = reciever.getObject();
					if (reciever_obj) {
						reciever_obj->removeSender(sender_id, reciever);
					}
				}
			}
		}

		template<class _Reciever, class ..._Signature>
		static inline std::pair<bool, McFunctionId> Connect(const McSignal<_Signature...>& sender_id, _Reciever* reciever, void(_Reciever::* callback)(_Signature...)) {
			MultiCallBase* sender_object = sender_id.m_func_id.getObject();
			if (!sender_object) {
				//std::cerr << "Sender doesn't inherits to MultiCallBase!\n";
				return std::make_pair(false, McFunctionId());
			}
			MultiCallBase* reciever_object = dynamic_cast<MultiCallBase*>(reciever);
			if (!reciever_object) {
				//std::cerr << "Reciever doesn't inherits to MultiCallBase!\n";
				return std::make_pair(false, McFunctionId());
			}
			std::function<void(_Signature...)> func = [callback, a_ptr = reciever](_Signature... args) {
				(a_ptr->*callback)(args...);
			};

			McFunctionId reciever_id(reciever, callback);
			const bool result = sender_object->addSubscriber(sender_id.m_func_id, reciever_id, func);
			if (result) {
				reciever_object->addSender(sender_id.m_func_id, reciever_id);
			}
			return std::make_pair(result, reciever_id);
		}

		template<class ..._Signature>
		static inline std::pair<bool, McFunctionId> Connect(const McSignal<_Signature...>& sender_id, void(*callback)(_Signature...)) {
			MultiCallBase* sender_object = sender_id.m_func_id.getObject();
			if (!sender_object) {
				//std::cerr << "Sender doesn't inherits to MultiCallBase!\n";
				return std::make_pair(false, McFunctionId());
			}
			std::function<void(_Signature...)> func = [callback](_Signature... args) {
				callback(args...);
			};
			McFunctionId reciever_id(callback);
			const bool result = sender_object->addSubscriber(sender_id.m_func_id, reciever_id, func);
			return std::make_pair(result, reciever_id);
		}

		template<class ..._Signature, class F>
		static inline std::pair<bool, McFunctionId> Connect(const McSignal<_Signature...>& sender_id, F callback) {
			static_assert(std::is_convertible_v<F, std::function<void(_Signature...)>>, "F must be convertible to std::function");
			MultiCallBase* sender_object = sender_id.m_func_id.getObject();
			if (!sender_object) {
				//std::cerr << "Sender doesn't inherits to MultiCallBase!\n";
				return std::make_pair(false, McFunctionId());
			}
			std::function<void(_Signature...)> func = [callback](_Signature... args) {
				callback(args...);
			};
			McFunctionId reciever_id(callback);
			const bool result = sender_object->addSubscriber(sender_id.m_func_id, reciever_id, func);
			return std::make_pair(result, reciever_id);
		}

		template<class _Reciever, class ..._Signature>
		static inline bool Disconnect(const McSignal<_Signature...>& sender_id, _Reciever* reciever, void(_Reciever::* callback)(_Signature...)) {
			MultiCallBase* sender_object = sender_id.m_func_id.getObject();
			if (!sender_object) {
				//std::cerr << "Sender doesn't inherits to MultiCallBase!\n";
				return false;
			}
			MultiCallBase* reciever_object = dynamic_cast<MultiCallBase*>(reciever);
			if (!reciever_object) {
				//std::cerr << "Reciever doesn't inherits to MultiCallBase!\n";
				return false;
			}
			McFunctionId reciever_id(reciever, callback);
			bool result = sender_object->removeSubscriber(sender_id.m_func_id, reciever_id);
			if (result) {
				reciever_object->removeSender(sender_id.m_func_id, reciever_id);
			}
			return result;
		}

		template<class ..._Signature>
		static inline bool Disconnect(const McSignal<_Signature...>& sender_id, void(*callback)(_Signature...)) {
			MultiCallBase* sender_object = sender_id.m_func_id.getObject();
			if (!sender_object) {
				//std::cerr << "Sender doesn't inherits to MultiCallBase!\n";
				return false;
			}
			McFunctionId reciever_id(callback);
			return sender_object->removeSubscriber(sender_id.m_func_id, reciever_id);
		}

		template<class ..._Signature>
		static inline bool Disconnect(const McSignal<_Signature...>& sender_id, const McFunctionId& reciever_id) {
			MultiCallBase* sender_object = sender_id.m_func_id.getObject();
			if (!sender_object) {
				//std::cerr << "Sender doesn't inherits to MultiCallBase!\n";
				return false;
			}
			return sender_object->removeSubscriber(sender_id.m_func_id, reciever_id);
		}


	protected:
		inline virtual bool addSubscriber(const McFunctionId& signal_id, const McFunctionId& subscriber_id, const std::any& func) {
			std::unique_lock locker(__m_mutex);
			__m_mc_recievers_map[signal_id][subscriber_id] = func;
			return true;
		}

		inline virtual bool removeSubscriber(const McFunctionId& signal_id, McFunctionId subscriber_id) {
			std::unique_lock locker(__m_mutex);
			__m_mc_recievers_map[signal_id].erase(subscriber_id);
			return true;
		}

		inline virtual void addSender(const McFunctionId& sender_id, const McFunctionId& subscriber_id) {
			std::unique_lock locker(__m_mutex);
			__m_senders_map[subscriber_id].insert(sender_id);
		}

		inline virtual void removeSender(const McFunctionId& sender_id, const McFunctionId& subscriber_id) {
			std::unique_lock locker(__m_mutex);
			__m_senders_map[subscriber_id].erase(sender_id);
		}

		template<class... _Signature>
		inline void McEmit(const McSignal<_Signature...>& signal_id, _Signature... args) {
			std::shared_lock locker(__m_mutex);
			auto subscribers_it = __m_mc_recievers_map.find(signal_id.m_func_id);
			if (subscribers_it != __m_mc_recievers_map.end()) {
				for (auto& [_, func] : subscribers_it->second) {
					const auto& call_fn = std::any_cast<std::function<void(_Signature...)>>(func);
					call_fn(std::forward<_Signature>(args)...);
				}
			}
		}

	private:
		using __RecieversStorage = std::unordered_map<McFunctionId, std::any, McFunctionIdHash>;
		using __SendersStorage = std::unordered_set<McFunctionId, McFunctionIdHash>;
		std::unordered_map<McFunctionId, __RecieversStorage, McFunctionIdHash> __m_mc_recievers_map;
		std::unordered_map<McFunctionId, __SendersStorage, McFunctionIdHash> __m_senders_map;
		SpinSharedMutex __m_mutex;
	};
};
