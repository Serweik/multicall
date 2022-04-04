#include "factory.h"

namespace 
{
	using namespace multicall;

	class SenderType1 : public SenderInterface, public MultiCallBase {
	public:
		SenderType1() {
			m_thread = std::thread(&SenderType1::senderThread, this);
		}
		~SenderType1() {
			working = false;
			m_thread.join();
		}

	private:
		std::thread m_thread;
		bool working = true;
		int counter = 0;

		void senderThread() {
			while (working) {
				counter = counter > 2000000 ? 0 : ++counter;
				McEmit(McSignal<int>(this, &SenderInterface::tick), counter);

				std::this_thread::sleep_for(std::chrono::microseconds(1));
			}
		}
	};

	class SenderType2 : public SenderInterface, public MultiCallBase {
	public:
		SenderType2() {
			m_thread = std::thread(&SenderType2::senderThread, this);
			m_thread2 = std::thread(&SenderType2::senderThread2, this);
		}
		~SenderType2() {
			working = false;
			m_thread.join();
			m_thread2.join();
		}

	private:
		std::thread m_thread;
		std::thread m_thread2;
		bool working = true;
		int counter = 0;

		void senderThread() {
			while (working) {
				counter = counter > 2000000 ? 0 : ++counter;
				McEmit(McSignal<int>(this, &SenderInterface::tick), counter);

				std::this_thread::sleep_for(std::chrono::microseconds(1));
			}
		}
		void senderThread2() {
			while (working) {
				int temp_counter = counter;
				std::string val = "counter = " + std::to_string(temp_counter) + "\n";
				McEmit(McSignal<int, std::string>(this, &SenderInterface::tick), temp_counter, val);

				std::this_thread::sleep_for(std::chrono::microseconds(1));
			}
		}
	};
};

std::shared_ptr<SenderInterface> Factory::createSender(SenderType type)
{
	switch (type) {
		case Type1:
			return std::make_shared<SenderType1>();
		case Type2:
			return std::make_shared<SenderType2>();
	};
	return nullptr;
}
