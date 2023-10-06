#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include "sthread"

static void looping_recv(st::channel ch) {
    st::message msg;

    // it is possible to manually receive values instead of through iterators
    while(ch.recv(msg)) {
        std::string s;
        if(msg.data().copy_to(s)) {
            std::cout << s << std::endl;
        }
    }
}

TEST(example, closing_channels) {
    auto my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);

    my_channel.send(0, std::string("you say goodbye"));
    my_channel.send(0, std::string("and I say hello"));
    // close channel and join thread
    my_channel.close();
    my_thread.join(); 
}
