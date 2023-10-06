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
                if(msg.data().is<std::string>()) {
                    std::cout << msg.data().cast_to<std::string>();
                } else if(msg.data().is<int>()) {
                    std::cout << msg.data().cast_to<int>();
                }
                break;
        }
    }
};

TEST(example, payload_data_type_checking) {
    auto ch = st::channel::make();
    std::thread thd(my_function, ch);

    ch.send(op::print, std::string("hello "));
    ch.send(op::print, 1);
    ch.send(op::print, std::string(" more time\n"));
    ch.close();
    thd.join();
}
