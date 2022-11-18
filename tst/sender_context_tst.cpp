#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <thread>
#include <utility>
#include <iostream>
#include "simple_thread_test_utils.hpp"
#include "sthread"

namespace stt { // simple thread test

enum op {
    Default,
    Integer,
    Cstring,
    String,
    Double,
    Void
};

template <typename SHARED_SEND_CONTEXT>
struct shared_sender_context_test : public stt::test_runner<SHARED_SEND_CONTEXT> {
    shared_sender_context_test(
            std::function<SHARED_SEND_CONTEXT(std::size_t)> make) :
        stt::test_runner<SHARED_SEND_CONTEXT>("shared_sender_context_test"),
        m_make(make)
    { }
    
protected:
    virtual void test() {
        // alive 
        SHARED_SEND_CONTEXT ssc = m_make(0);
        EXPECT_TRUE(ssc.alive());
        
        // terminate
        ssc.terminate(); 
        EXPECT_FALSE(ssc.alive());
        auto ssc2 = m_make(0);
        ssc = ssc2;
        EXPECT_TRUE(ssc.alive());
        ssc.terminate(true); 
        EXPECT_FALSE(ssc.alive());
        ssc = m_make(0);
        EXPECT_TRUE(ssc.alive());
        ssc.terminate(false); 
        EXPECT_FALSE(ssc.alive());
        
        // queued (does this compile)
        ssc = m_make(9);
        EXPECT_EQ(0, ssc.queued());

        // send 
        EXPECT_TRUE(ssc.send());
        EXPECT_TRUE(ssc.send(op::Default));
        EXPECT_TRUE(ssc.send(op::Integer,1));
        EXPECT_TRUE(ssc.send(op::Cstring,"hello"));
        EXPECT_TRUE(ssc.send(op::String,std::string("world")));
        EXPECT_TRUE(ssc.send(op::Double,st::data::make<double>(3.6)));

        // async 
        ssc.async(op::Integer,[]{ return 1; }); // return int
        ssc.async(op::String,[]{ return std::string("world"); }); // return string
        ssc.async(op::Void,[]{}); // return void
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::function<SHARED_SEND_CONTEXT(std::size_t)> m_make;
};

struct object {
    object(std::size_t exp_msg_recv_cnt) : 
        m_expected_msg_recv_cnt(exp_msg_recv_cnt) 
    { }

    ~object() {
        EXPECT_EQ(m_expected_msg_recv_cnt, m_actual_msg_recv_cnt);
    }

    void recv(st::message msg) {
        ++m_actual_msg_recv_cnt;
        
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
    }

    std::size_t m_expected_msg_recv_cnt=0;
    std::size_t m_actual_msg_recv_cnt=0;
};

} 

TEST(simple_thread, sender_context) {
    stt::shared_sender_context_test<st::channel> (
            [](std::size_t unused){ 
                return st::channel::make(); 
            }).run();

    stt::shared_sender_context_test<st::thread>(
            [](std::size_t exp_msg_recv_cnt){ 
                return st::thread::make<stt::object>(exp_msg_recv_cnt); 
            }).run();
}
