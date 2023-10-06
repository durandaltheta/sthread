#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include "sthread"

enum opA {
    request_value = 0 // send a value back to a requestor
};

enum opB {
    // same enumeration value as opA::request_value, normally a potential bug
    receive_value = 0 
};

static void childThreadA(st::channel ch) {
    std::string value = "foofaa";

    for(auto msg : ch) {
        std::cout << "child threadA recv" << std::endl;
        switch(msg.id()) {
            case opA::request_value:
            {
                // this thread doesn't know anything about who they are replying to
                if(msg.data().is<st::reply>()) {
                    msg.data().cast_to<st::reply>().send(value);
                }
                break;
            }
        }
    }
}

static void childThreadB(st::channel ch, st::channel value_received_conf_ch) {
    for(auto msg : ch) {
        std::cout << "child threadB recv" << std::endl;
        switch(msg.id()) {
            // this thread doesn't know who they are receiving from 
            case opB::receive_value:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << "received " << s << "!" << std::endl;
                    value_received_conf_ch.send();
                }
                break;
            }
        }
    }
};

TEST(example, abstracting_message_replies) {
    // launch child threads
    auto ch_a = st::channel::make(); 
    std::thread thd_a(childThreadA, ch_a);

    auto ch_b = st::channel::make(); 
    auto value_received_conf_ch = st::channel::make(); 
    std::thread thd_b(childThreadB, ch_b, value_received_conf_ch);

    // create an `st::reply` to forward a value to `ch_b`
    auto rep_b = st::reply::make(ch_b, opB::receive_value);

    // send the request for a value over `ch_a`
    ch_a.send(opA::request_value, rep_b);

    // wait for childThreadB to process the response from childThreadA 
    st::message msg;
    value_received_conf_ch.recv(msg); 

    // close and join child threads 
    std::cout << "a" << std::endl;
    ch_a.close();
    std::cout << "b" << std::endl;
    ch_b.close();
    std::cout << "c" << std::endl;
    thd_a.join();
    thd_b.join();
    std::cout << "DEAD" << std::endl;
}
