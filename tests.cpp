#include "multicall.h"
#include "factory.h"
#include <iostream>

Factory global_factory;

using namespace multicall;

class Reciever : public MultiCallBase {
public:
    ~Reciever() {
        MultiCallBase::DisconnectFromAll();
    }

    std::atomic<int64_t> call_counter{ 0 };
    std::atomic<int> counter{ 0 };
    void new_tick(int val) {
        counter = val;
        //std::cout << val << "\n";
    }

    void tick_counter(int) {
        ++call_counter;
    }

    void new_tick(int val, std::string text) {
        counter = val;
        std::cout << text;
    }
};

std::atomic<int64_t> global_call_counter{ 0 };
void global_tick_counter(int) {
    ++global_call_counter;
}

std::atomic<int> global_counter{0};
void global_ew_tick(int val) {
    global_counter = val;
}

void Test_connect_disconnect_to_member() {
    auto object1 = global_factory.createSender(Factory::Type1);
    Reciever reciever;
    for (int i = 0; i < 10; ++ i) {
        int old_counter = reciever.counter;
        MultiCallBase::Connect(McSignal<int>(object1.get(), &SenderInterface::tick), &reciever, &Reciever::new_tick);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        MultiCallBase::Disconnect(McSignal<int>(object1.get(), &SenderInterface::tick), &reciever, &Reciever::new_tick);
        assert(old_counter != reciever.counter);
        old_counter = reciever.counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(reciever.counter - old_counter < 2);
    }
}

void Test_connect_disconnect_to_member_no_overload() {
    auto object1 = global_factory.createSender(Factory::Type3);
    Reciever reciever;
    for (int i = 0; i < 10; ++i) {
        int old_counter = reciever.counter;
        MultiCallBase::Connect(McSignal(object1.get(), &SenderInterface::tick2), &reciever, &Reciever::new_tick);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        MultiCallBase::Disconnect(McSignal(object1.get(), &SenderInterface::tick2), &reciever, &Reciever::new_tick);
        assert(old_counter != reciever.counter);
        old_counter = reciever.counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(reciever.counter - old_counter < 2); //If McEmit() was able to start emitting before we called Disconnect(), the emit will be executed 
    }
}

void Test_connect_disconnect_to_static_function() {
    auto object1 = global_factory.createSender(Factory::Type1);
    for (int i = 0; i < 10; ++i) {
        int old_counter = global_counter;
        MultiCallBase::Connect(McSignal<int>(object1.get(), &SenderInterface::tick), global_ew_tick);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        MultiCallBase::Disconnect(McSignal<int>(object1.get(), &SenderInterface::tick), global_ew_tick);
        assert(old_counter != global_counter);
        old_counter = global_counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(global_counter - old_counter < 2);
    }
    global_counter = 0;
}

void Test_connect_disconnect_to_lambda() {
    auto object1 = global_factory.createSender(Factory::Type1);
    Reciever reciever;
    for (int i = 0; i < 10; ++i) {
        int old_counter = reciever.counter;
        auto [ok, id] = MultiCallBase::Connect(McSignal<int>(object1.get(), &SenderInterface::tick), [&reciever](int val) {
            reciever.new_tick(val);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        MultiCallBase::Disconnect(McSignal<int>(object1.get(), &SenderInterface::tick), id);
        assert(old_counter != reciever.counter);
        old_counter = reciever.counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(reciever.counter - old_counter < 2);
    }
}

void Test_two_senders() {
    auto object1 = global_factory.createSender(Factory::Type1);
    auto object2 = global_factory.createSender(Factory::Type2);

    Reciever* reciever = new Reciever();

    for (int i = 0; i < 10; ++i) {
        int old_counter = reciever->counter;
        MultiCallBase::Connect(McSignal<int>(object1.get(), &SenderInterface::tick), reciever, &Reciever::new_tick);
        MultiCallBase::Connect(McSignal<int>(object2.get(), &SenderInterface::tick), reciever, &Reciever::new_tick);
        MultiCallBase::Connect(McSignal<int, std::string>(object2.get(), &SenderInterface::tick), reciever, &Reciever::new_tick);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        MultiCallBase::Disconnect(McSignal<int>(object1.get(), &SenderInterface::tick), reciever, &Reciever::new_tick);
        assert(old_counter != reciever->counter);
        old_counter = reciever->counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(old_counter != reciever->counter);
    }
    delete reciever; //last connection must be deleted automatically
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

void Test_member_call_counter() {
    auto object1 = global_factory.createSender(Factory::Type1);
    Reciever reciever;
    int64_t summ = 0;
    static const int times = 5;
    for (int i = 0; i < times; ++ i) {
        MultiCallBase::Connect(McSignal<int>(object1.get(), &SenderInterface::tick), &reciever, &Reciever::tick_counter);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        MultiCallBase::Disconnect(McSignal<int>(object1.get(), &SenderInterface::tick), &reciever, &Reciever::tick_counter);
        summ += reciever.call_counter;
        reciever.call_counter = 0;
    }
    std::cout << "calls member function per second = " << summ / times << std::endl;
}

void Test_global_call_counter() {
    auto object1 = global_factory.createSender(Factory::Type1);
    int64_t summ = 0;
    static const int times = 5;
    for (int i = 0; i < times; ++i) {
        MultiCallBase::Connect(McSignal<int>(object1.get(), &SenderInterface::tick), global_tick_counter);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        MultiCallBase::Disconnect(McSignal<int>(object1.get(), &SenderInterface::tick), global_tick_counter);
        summ += global_call_counter;
        global_call_counter = 0;
    }
    std::cout << "calls global function per second = " << summ / times << std::endl;
}

void Test_lambda_call_counter() {
    auto object1 = global_factory.createSender(Factory::Type1);

    std::atomic<int64_t> counter;

    int64_t summ = 0;
    static const int times = 5;
    for (int i = 0; i < times; ++i) {
        auto [ok, id] = MultiCallBase::Connect(McSignal<int>(object1.get(), &SenderInterface::tick), [&counter](int) {
            ++counter;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        MultiCallBase::Disconnect(McSignal<int>(object1.get(), &SenderInterface::tick), id);
        summ += counter;
        counter = 0;
    }

    std::cout << "calls lambda per second = " << summ / times << std::endl;
}

int main() {
    std::cout << "start unit tests" << std::endl;

    Test_connect_disconnect_to_member();
    Test_connect_disconnect_to_member_no_overload();
    Test_connect_disconnect_to_static_function();
    Test_connect_disconnect_to_lambda();
    Test_two_senders();

    std::cout << "all unit tests are successfully passed!" << std::endl;
    std::cout << "start performance test" << std::endl;

    Test_member_call_counter();
    Test_global_call_counter();
    Test_lambda_call_counter();

    std::cout << "all tests are successfully passed!" << std::endl;
    
    system("pause");

    return 0;
}