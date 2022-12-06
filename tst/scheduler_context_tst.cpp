#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <thread>
#include <utility>
#include <iostream>
#include "simple_thread_test_utils.hpp"
#include "sthread"

namespace stt { // simple thread test

template <typename SHARED_SCHEDULER_CONTEXT>
struct shared_scheduler_context_test : public stt::test_runner<SHARED_SCHEDULER_CONTEXT> {
    shared_scheduler_context_test(
            std::function<SHARED_SCHEDULER_CONTEXT()> make) :
        stt::test_runner<SHARED_SCHEDULER_CONTEXT>("shared_scheduler_context_test"),
        m_make(make)
    { }
    
protected:
    virtual void test() {
        SHARED_SCHEDULER_CONTEXT ssc = m_make();
        st::channel ch = st::channel::make();
        st::message msg;

        // schedule no arguments 
        ssc.schedule([=]() mutable { 
            ch.send(0,3); 
        });

        EXPECT_TRUE(ch.recv(msg));
        EXPECT_TRUE(msg.data());
        EXPECT_TRUE(msg.data().is<int>());
        EXPECT_EQ(3, msg.data().cast_to<int>());

        // schedule with arguments
        this->log(__PRETTY_FUNCTION__, __LINE__);

        auto sched_func = std::string(__PRETTY_FUNCTION__) + "_thread_schedule";

        ssc.schedule([=](std::string s) mutable { 
            this->log(sched_func.c_str(), __LINE__);
            ch.send(0,s); 
            this->log(sched_func.c_str(), __LINE__);
        }, std::string("hello"));

        this->log(__PRETTY_FUNCTION__, __LINE__);
        EXPECT_TRUE(ch.recv(msg));
        this->log(__PRETTY_FUNCTION__, __LINE__);
        EXPECT_TRUE(msg.data());
        this->log(__PRETTY_FUNCTION__, __LINE__);
        if(msg.data().is<std::string>()) {
            EXPECT_TRUE(true);
            EXPECT_EQ(std::string("hello"), msg.data().cast_to<std::string>());
            this->log(__PRETTY_FUNCTION__, __LINE__);
        } else {
            EXPECT_TRUE(false); // failure
        }
        this->log(__PRETTY_FUNCTION__, __LINE__);
    }

    const char* test_name = "shared_scheduler_context_test";
    std::function<SHARED_SCHEDULER_CONTEXT()> m_make;
};

struct object {
    ~object() {
        EXPECT_EQ(0, m_actual_msg_recv_cnt);
    }

    void recv(st::message msg) {
        ++m_actual_msg_recv_cnt;
    }

    std::size_t m_actual_msg_recv_cnt=0;
};

} 

TEST(simple_thread, scheduler_context) {
    stt::shared_scheduler_context_test<st::thread>(
            []{ return st::thread::make<stt::object>(); }).run();
}
