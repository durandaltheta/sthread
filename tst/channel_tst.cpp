#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <thread>
#include <utility>
#include <iostream>
#include <functional>
#include "simple_thread_test_utils.hpp"
#include "sthread"

TEST(simple_thread, channel) {
    st::message msg;
    std::size_t expected_msg_recv_cnt=0;
    std::size_t actual_msg_recv_cnt=0;

    enum op {
        Default = 0,
        Integer,
        Cstring,
        String,
        Double,
        Void
    };

    auto reset_rcv_cnts = [&](std::size_t exp){ 
        expected_msg_recv_cnt=exp;
        actual_msg_recv_cnt=0;
    };

    auto msg_handler = [&](st::message msg) {
        ++actual_msg_recv_cnt;

        auto print_type_error = [&](const std::type_info& expected){
            stt::log("channel", "expected type: ", expected.name());
            stt::log("channel", "actual type: ", msg.data().type_info().name());
            EXPECT_TRUE(false); // error
        };
        
        switch(msg.id()) {
            case op::Default:
                break;
            case op::Integer:
                {
                    int i;
                    if(msg.data().copy_to(i)) {
                        EXPECT_EQ(1, msg.data().cast_to<int>());
                    } else {
                        print_type_error(typeid(int));
                        EXPECT_TRUE(false); // error
                    }
                }
                break;
            case op::Cstring:
                if(msg.data().is<const char*>()) {
                    EXPECT_EQ("hello", msg.data().cast_to<const char*>());
                } else {
                    print_type_error(typeid(const char*));
                }
                break;
            case op::String:
                {
                    std::string s;
                    if(msg.data().copy_to(s)) {
                        EXPECT_EQ(std::string("world"), s);
                    } else {
                        print_type_error(typeid(std::string));
                    }
                }
                break;
            case op::Double:
                {
                    double d;
                    if(msg.data().copy_to(d)) {
                        EXPECT_EQ((double)3.6, d);
                    } else {
                        print_type_error(typeid(double));
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
                break;
            default:
                EXPECT_TRUE(false); // error
                break;
        }
    };

    auto msg_loop = [&](st::channel ch) {
        for(auto msg : ch) {
            msg_handler(msg);
        }
    };

    stt::log("channel", "closed");
    st::channel ch = st::channel::make();
    EXPECT_FALSE(ch.closed());
    
    stt::log("channel", "close");
    ch.close(); 
    EXPECT_TRUE(ch.closed());
    auto ch2 = st::channel::make();
    ch = ch2;
    EXPECT_FALSE(ch.closed());
    EXPECT_FALSE(ch2.closed());
    ch.send(0,0);
    ch.close(true); 
    EXPECT_TRUE(ch.recv(msg));
    EXPECT_FALSE(ch.recv(msg));
    EXPECT_TRUE(ch.closed());
    EXPECT_TRUE(ch2.closed());
    ch = st::channel::make();
    EXPECT_FALSE(ch.closed());
    EXPECT_TRUE(ch2.closed());
    ch.send(0,0);
    ch.close(false); 
    EXPECT_FALSE(ch.recv(msg));
    EXPECT_FALSE(ch.recv(msg));
    EXPECT_TRUE(ch.closed());
    EXPECT_TRUE(ch2.closed());
    
    stt::log("channel", "queued");
    ch = st::channel::make();
    EXPECT_EQ(0, ch.queued());
    EXPECT_TRUE(ch.send(op::Default));
    EXPECT_EQ(1, ch.queued());
    EXPECT_TRUE(ch.recv(msg));
    EXPECT_EQ(msg.id(), op::Default);
    ch.close();

    stt::log("channel", "send/recv");
    reset_rcv_cnts(6);
    ch = st::channel::make();
    EXPECT_TRUE(ch.send());
    EXPECT_TRUE(ch.send(op::Default));
    EXPECT_TRUE(ch.send(op::Integer,1));
    EXPECT_TRUE(ch.send(op::Cstring, "hello"));
    EXPECT_TRUE(ch.send(op::String,std::string("world")));
    EXPECT_TRUE(ch.send(op::Double,st::data::make<double>(3.6)));
    EXPECT_EQ(6, ch.queued());
    std::thread recv_thd(msg_loop, ch);
    ch.close();
    recv_thd.join();
    EXPECT_EQ(6, actual_msg_recv_cnt);

    stt::log("channel", "async");
    reset_rcv_cnts(3);
    ch = st::channel::make();
    ch.async(op::Integer,[]{ return 1; }); // return int
    ch.async(op::String,[]{ return std::string("world"); }); // return string
    ch.async(op::Void,[]{}); // return void
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(3, ch.queued());
    recv_thd = std::thread(msg_loop, ch);
    ch.close();
    recv_thd.join();
    EXPECT_EQ(3, actual_msg_recv_cnt);
    
    //stt::log("channel", "timer");
    //reset_rcv_cnts(3);
    //ch = st::channel::make();
    //ch.timer(op::Integer,std::chrono::milliseconds(200),1); 
    //ch.timer(op::String,std::chrono::milliseconds(300),std::string("world")); 
    //ch.timer(op::Void,std::chrono::milliseconds(400)); 
    //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    //EXPECT_EQ(3, ch.queued());
    //recv_thd = std::thread(msg_loop, ch);
    //ch.close();
    //recv_thd.join();
    //EXPECT_EQ(3, actual_msg_recv_cnt);
}
