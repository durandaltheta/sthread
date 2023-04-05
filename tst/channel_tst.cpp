#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <thread>
#include <utility>
#include <iostream>
#include <functional>
#include "simple_thread_test_utils.hpp"
#include "sthread"

struct msg_handling_object {
    // message recv API
    void recv(st::message msg) {
        m_hdl(msg);
    }

    // actual message handler, settable by this test
    std::function<void(st::message msg)> m_hdl;
};

TEST(simple_thread, channel) {
    st::message msg;
    std::size_t expected_msg_recv_cnt=0;
    std::size_t actual_msg_recv_cnt=0;

    auto reset_rcv_cnts = [&](std::size_t exp){ 
        expected_msg_recv_cnt=exp;
        actual_msg_recv_cnt=0;
    };

    auto msg_handler = [](st::message msg) {
        ++actual_msg_recv_cnt;
        
        switch(msg.id()) {
            case op::Default:
                break;
            case op::Integer:
                {
                    int i;
                    if(msg.data().copy_to(i)) {
                        EXPECT_EQ(1, msg.data().cast_to<int>());
                    } else {
                        EXPECT_TRUE(false); // error
                    }
                }
                break;
            case op::Cstring:
                if(msg.data().is<const char*>()) {
                    EXPECT_EQ("hello", msg.data().cast_to<const char*>());
                } else {
                    EXPECT_TRUE(false); // error
                }
                break;
            case op::String:
                {
                    std::string s;
                    if(msg.data().copy_to(s)) {
                        EXPECT_EQ(std::string("world"), s);
                    } else {
                        EXPECT_TRUE(false); // error
                    }
                }
                break;
            case op::Double:
                {
                    double d;
                    if(msg.data().copy_to(d)) {
                        EXPECT_EQ((double)3.6, d);
                    } else {
                        EXPECT_TRUE(false); // error
                    }
                }
                break;
            case op::Void:
                {
                    if(msg.data()) {
                        EXPECT_TRUE(false); // error
                    } else {
                        EXPECT_TRUE(true);
                    }
                }
            default:
                EXPECT_TRUE(false); // error
                break;
        }
    };

    // closed 
    st::channel ch = st::channel.make();
    EXPECT_FALSE(ch.closed());
    
    // terminate
    ch.terminate(); 
    EXPECT_TRUE(ch.closed());
    auto ch2 = st::channel::make();
    ch = ch2;
    EXPECT_FALSE(ch.closed());
    ch.terminate(true); 
    EXPECT_TRUE(ch.closed());
    ch = st::channel::make();
    EXPECT_FALSE(ch.closed());
    ch.terminate(false); 
    EXPECT_TRUE(ch.closed());
    
    // queued (does this compile)
    ch = st::channel::make();
    EXPECT_EQ(0, ch.queued());
    EXPECT_TRUE(ch.send(op::Default));
    EXPECT_EQ(1, ch.queued());
    EXPECT_TRUE(ch.recv(msg));
    EXPECT_EQ(msg.id(), op::Default);
    ch.close();

    // send 
    ch = st::channel::make();
    EXPECT_TRUE(ch.send());
    EXPECT_TRUE(ch.send(op::Default));
    EXPECT_TRUE(ch.send(op::Integer,1));
    EXPECT_TRUE(ch.send(op::Cstring,"hello"));
    EXPECT_TRUE(ch.send(op::String,std::string("world")));
    EXPECT_TRUE(ch.send(op::Double,st::data::make<double>(3.6)));
    EXPECT_EQ(6, ch.queued());

    reset_rcv_cnts(6);
    std::thread recv_thd(ch.launch_recv_thread(msg_handler));
    ch.close();
    recv_thd.join();
    EXPECT_EQ(6, actual_msg_recv_cnt);

    // async 
    ch = st::channel::make();
    ch.async(op::Integer,[]{ return 1; }); // return int
    ch.async(op::String,[]{ return std::string("world"); }); // return string
    ch.async(op::Void,[]{}); // return void
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(3, ch.queued());
    reset_rcv_cnts(3);
    // recv_loop Callable
    recv_thd = ch.launch_recv_thread(msg_handler);
    ch.close();
    recv_thd.join();
    EXPECT_EQ(3, actual_msg_recv_cnt);

    // schedule 
    ch = st::channel::make();
    msg_handling_object obj;
    obj.m_hdl = msg_handler;
    EXPECT_TRUE(ch.schedule([=]{ ch.send(op::String,std::string("foo")); }));
    EXPECT_TRUE(ch.schedule([=]{ ch.send(op::String,std::string("faa")); }));
    EXPECT_EQ(2, ch.queued());
    // recv_loop object
    recv_thd = ch.launch_recv_thread(obj, msg_handling_object::recv);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ch.close();
    recv_thd.join();
    EXPECT_EQ(4, actual_msg_recv_cnt);
}
