#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <thread>
#include <utility>
#include <iostream>
#include <functional>
#include "sthread"
#include "simple_thread_test_utils.hpp"

namespace stt {
namespace channel {

enum op {
    Default = 0,
    Integer,
    Cstring,
    String,
    Double,
    Void
};

std::mutex mtx;
std::size_t private_msg_recv_cnt=0;

int msg_recv_cnt() {
    std::lock_guard<std::mutex> lk(mtx);
    return private_msg_recv_cnt;
}

void incr_recv_cnt() {
    std::lock_guard<std::mutex> lk(mtx);
    ++private_msg_recv_cnt;
}

void reset_recv_cnt() { 
    std::lock_guard<std::mutex> lk(mtx);
    private_msg_recv_cnt=0;
};

void print_type_error(const std::type_info& actual, const std::type_info& expected) {
    stt::log("channel", "expected type: ", expected.name());
    stt::log("channel", "actual type: ", actual.name());
    EXPECT_TRUE(false); // error
};

void msg_handler(st::message msg) {
    stt::channel::incr_recv_cnt();
    
    switch(msg.id()) {
        case stt::channel::op::Default:
            break;
        case stt::channel::op::Integer:
            {
                int i;
                if(msg.data().copy_to(i)) {
                    EXPECT_EQ(1, msg.data().cast_to<int>());
                } else {
                    print_type_error(msg.data().type_info(), typeid(int));
                    EXPECT_TRUE(false); // error
                }
            }
            break;
        case stt::channel::op::Cstring:
            if(msg.data().is<const char*>()) {
                EXPECT_EQ("hello", msg.data().cast_to<const char*>());
            } else {
                print_type_error(msg.data().type_info(), typeid(const char*));
            }
            break;
        case stt::channel::op::String:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    EXPECT_EQ(std::string("world"), s);
                } else {
                    print_type_error(msg.data().type_info(), typeid(std::string));
                }
            }
            break;
        case stt::channel::op::Double:
            {
                double d;
                if(msg.data().copy_to(d)) {
                    EXPECT_EQ((double)3.6, d);
                } else {
                    print_type_error(msg.data().type_info(), typeid(double));
                }
            }
            break;
        case stt::channel::op::Void:
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

void msg_while_recv_loop(st::channel ch) {
    st::message msg;
    while(ch.recv(msg)) {
        msg_handler(msg);
    }
};

void msg_for_recv_loop(st::channel ch) {
    for(auto msg : ch) {
        msg_handler(msg);
    }
};
}
}

TEST(simple_thread, channel_closed) {
    st::message msg;
    st::channel ch = st::channel::make();
    EXPECT_FALSE(ch.closed());
}
    
TEST(simple_thread, channel_close) {
    st::message msg;
    st::channel ch;
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
}
    
TEST(simple_thread, channel_queued) {
    st::message msg;
    st::channel ch;
    ch = st::channel::make();
    EXPECT_EQ(0, ch.queued());
    EXPECT_TRUE(ch.send(stt::channel::op::Default));
    EXPECT_EQ(1, ch.queued());
    EXPECT_TRUE(ch.recv(msg));
    EXPECT_EQ(msg.id(), stt::channel::op::Default);
    ch.close();
}

TEST(simple_thread, channel_send_recv) {
    st::message msg;
    st::channel ch;
    stt::channel::reset_recv_cnt();
    ch = st::channel::make();
    EXPECT_TRUE(ch.send());
    EXPECT_TRUE(ch.send(stt::channel::op::Default));
    EXPECT_TRUE(ch.send(stt::channel::op::Integer,1));
    EXPECT_TRUE(ch.send(stt::channel::op::Cstring, (const char*)"hello"));
    EXPECT_TRUE(ch.send(stt::channel::op::String,std::string("world")));
    EXPECT_TRUE(ch.send(stt::channel::op::Double,st::data::make<double>(3.6)));
    EXPECT_EQ(6, ch.queued());
    std::thread recv_thd(stt::channel::msg_while_recv_loop, ch);
    ch.close();
    recv_thd.join();
    EXPECT_EQ(6, stt::channel::msg_recv_cnt());
}

TEST(simple_thread, channel_try_recv) {
    st::channel ch = st::channel::make();

    std::thread([](st::channel ch) {
        st::message msg;
        EXPECT_EQ(st::state::failure, ch.try_recv(msg));
    },
    ch).join();

    ch.send(13, (const char*)"hello");

    std::thread([](st::channel ch) {
        st::message msg;
        EXPECT_EQ(st::state::success, ch.try_recv(msg));
        EXPECT_EQ(13, msg.id());
        EXPECT_TRUE(msg.data().is<const char*>());
        EXPECT_EQ(std::string("hello"), std::string(msg.data().cast_to<const char*>()));
    },
    ch).join();

    std::thread([](st::channel ch) {
        st::message msg;
        EXPECT_EQ(st::state::failure, ch.try_recv(msg));
    },
    ch).join();

    ch.close();

    std::thread([](st::channel ch) {
        st::message msg;
        EXPECT_EQ(st::state::closed, ch.try_recv(msg));
    },
    ch).join();
}

TEST(simple_thread, channel_blocked_receivers) {
    st::message msg;
    st::channel ch = st::channel::make();
    EXPECT_EQ(0, ch.blocked_receivers());
    auto do_recv = [](st::channel ch) { st::message msg; return ch.recv(msg); };
    std::thread t1(do_recv, ch);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(1, ch.blocked_receivers());
    std::thread t2(do_recv, ch);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(2, ch.blocked_receivers());
    ch.send(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(1, ch.blocked_receivers());
    ch.close();
    t1.join();
    t2.join();
    EXPECT_EQ(0, ch.blocked_receivers());
}

TEST(simple_thread, channel_async) {
    st::message msg;
    st::channel ch;
    stt::channel::reset_recv_cnt();
    ch = st::channel::make();
    ch.async(stt::channel::op::Integer,[]{ return 1; }); // return int
    ch.async(stt::channel::op::String,[]{ return std::string("world"); }); // return string
    ch.async(stt::channel::op::Void,[]{}); // return void
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(3, ch.queued());
    std::thread recv_thd = std::thread(stt::channel::msg_for_recv_loop, ch);
    ch.close();
    recv_thd.join();
    EXPECT_EQ(3, stt::channel::msg_recv_cnt());
}
    
TEST(simple_thread, channel_timer) {
    st::message msg;
    st::channel ch;
    stt::channel::reset_recv_cnt();
    ch = st::channel::make();
    ch.timer(stt::channel::op::Integer,std::chrono::milliseconds(200),1); 
    ch.timer(stt::channel::op::String,std::chrono::milliseconds(300),std::string("world")); 
    ch.timer(stt::channel::op::Void,std::chrono::milliseconds(400)); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(3, ch.queued());
    std::thread recv_thd = std::thread(stt::channel::msg_for_recv_loop, ch);
    ch.close();
    recv_thd.join();
    EXPECT_EQ(3, stt::channel::msg_recv_cnt());
}
