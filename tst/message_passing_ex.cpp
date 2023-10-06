#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include "sthread"

enum op {
    say, // print an std::string
    goodbye // print a goodbye message
};

st::channel ch = st::channel::make();

static void childThread() {
    for(auto msg : ch) { // msg is an `st::message`
        switch(msg.id()) {
            case op::say:
                {
                    std::string s;
                    if(msg.data().copy_to(s)) {
                        std::cout << "child thread says: " << s << std::endl;
                    }
                }
                break;
            case op::goodbye:
                std::cout << "Thanks for all the fish!" << std::endl;
        }
    }
}

TEST(example, message_passing) {
    std::thread thd(childThread);
    ch.send(op::say, std::string("hello"));
    ch.send(op::say, std::string("world"));
    ch.send(op::goodbye);
    ch.close();
    thd.join();
}
