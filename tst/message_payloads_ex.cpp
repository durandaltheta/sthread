#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include "sthread"

enum op {
    print
};

static void my_function(st::channel ch) {
    for(auto msg : ch) {
        switch(msg.id()) {
            case op::print:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << s << std::endl;
                } else {
                    std::cout << "message data was not a string" << std::endl;
                }
                break;
            }
        }
    }
};

TEST(example, message_payloads) {
    auto my_channel = st::channel::make();
    std::thread my_thread(my_function, my_channel);

    my_channel.send(op::print, std::string("hello again"));
    my_channel.send(op::print, 14);
    my_channel.close();
    my_thread.join();
}
