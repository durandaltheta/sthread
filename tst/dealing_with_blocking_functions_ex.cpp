#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "sthread"

enum op {
    timeout
};

// variant of user_timer returning a value
static std::string user_timer(std::chrono::milliseconds ms, std::string s) {
    std::this_thread::sleep_for(ms);
    std::cout << "sleep ended on temporary thread" << std::endl;
    return s;
}

// variant of user_timer returning void
static void user_timer_no_return(std::chrono::milliseconds ms) {
    std::this_thread::sleep_for(ms);
    std::cout << "sleep ended on temporary thread with no return" << std::endl;
}

static void process_timeouts(st::channel ch, st::channel timeout_conf_ch) {
    for(auto msg: ch) { 
        switch(msg.id()) {
            case op::timeout:
                std::cout << "timeout detected" << std::endl;

                if(msg.data().is<std::string>()) {
                    std::cout << msg.data().cast_to<std::string>() << std::endl;
                }

                // let main thread know we processed the timeout
                timeout_conf_ch.send(0); 
                break;
        }
    }
}

TEST(example, dealing_with_blocking_functions) {
    auto ch = st::channel::make();
    auto timeout_conf_ch = st::channel::make();
    std::thread thd(process_timeouts, ch, timeout_conf_ch);

    ch.async(op::timeout, 
             user_timer, 
             std::chrono::milliseconds(100), 
             std::string("that's all folks!"));

    ch.async(op::timeout, user_timer_no_return, std::chrono::milliseconds(200));

    ch.timer(op::timeout, std::chrono::milliseconds(300), std::string("timer with payload"));
    ch.timer(op::timeout, std::chrono::milliseconds(400));

    // wait for child thread to indicate it received the timeout confirmation
    st::message msg;
    timeout_conf_ch.recv(msg);
    timeout_conf_ch.recv(msg);
    timeout_conf_ch.recv(msg);
    timeout_conf_ch.recv(msg);

    ch.close();
    thd.join(); 
}
