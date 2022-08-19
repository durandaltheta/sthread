//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis
#include <gtest/gtest.h>
#include "simple_thread.hpp"
#include <thread>
#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <tuple>
#include <vector>
#include <map>
#include <functional>

TEST(simple_thread, message) {
    enum op {
        unknown,
        integer,
        string
    };

    // int message
    {
        int i = 14;
        st::sptr<st::message> msg = st::message::make(op::integer,i);

        EXPECT_EQ(msg->id(), op::integer);
        EXPECT_NE(msg->id(), op::string);
        EXPECT_EQ(msg->type_code(), st::code<int>());
        EXPECT_NE(msg->type_code(), st::code<std::string>());
        EXPECT_TRUE(msg->is<int>());
        EXPECT_FALSE(msg->is<std::string>());

        {
            std::string s = "";
            EXPECT_FALSE(msg->copy_data_to(s));
        }

        {
            std::string s = "";
            EXPECT_FALSE(msg->move_data_to(s));
        }

        {
            int i2 = 0;
            EXPECT_TRUE(msg->copy_data_to(i2));
            EXPECT_EQ(i, i2);
        }

        {
            int i2 = 0;
            EXPECT_TRUE(msg->move_data_to(i2));
            EXPECT_EQ(i, i2);
        }

        // 2nd successful move should prove that data was swapped with 1st move
        {
            int i2 = 0;
            EXPECT_TRUE(msg->move_data_to(i2));
            EXPECT_NE(i, i2);
            EXPECT_EQ(i2, 0);
        }
    }

    // std::string message
    {
        std::string s = "codemonkey";
        st::sptr<st::message> msg = st::message::make(op::string,s);

        EXPECT_EQ(msg->id(), op::string);
        EXPECT_NE(msg->id(), op::integer);
        EXPECT_EQ(msg->type_code(), st::code<std::string>());
        EXPECT_NE(msg->type_code(), st::code<int>());
        EXPECT_TRUE(msg->is<std::string>());
        EXPECT_FALSE(msg->is<int>());

        {
            int i = 0;
            EXPECT_FALSE(msg->copy_data_to(i));
        }

        {
            int i = 0;
            EXPECT_FALSE(msg->move_data_to(i));
        }

        {
            std::string s2 = "";
            EXPECT_TRUE(msg->copy_data_to(s2));
            EXPECT_EQ(s, s2);
        }

        {
            std::string s2 = "";
            EXPECT_TRUE(msg->move_data_to(s2));
            EXPECT_EQ(s, s2);
        }

        // 2nd successful move should prove that data was swapped with 1st move
        {
            std::string s2 = "";
            EXPECT_TRUE(msg->move_data_to(s2));
            EXPECT_NE(s, s2);
            EXPECT_EQ(s2, "");
        }
    }
}

// show channels generally work
TEST(simple_thread, channel) {
    struct garbazoo { }; // random type unknown to worker

    enum op {
        print_int,
        print_string,
        unknown
    };
        
    // used for worker behavior confirmation purposes
    st::sptr<st::channel> ret_ch = st::channel::make();

    st::sptr<st::channel> ch = st::channel::make();
    std::thread thd([](st::sptr<st::channel> ch, st::sptr<st::channel> ret_ch) {
        st::sptr<st::message> msg;

        while(ch->recv(msg)) {
            switch(msg->id()) {
                case op::print_int:
                {
                    int i;

                    if(msg->copy_data_to<int>(i)) {
                        std::cout << "int: " << i << std::endl;
                        ret_ch->send(op::print_int);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        ret_ch->send(op::unknown);
                    }
                    break;
                }
                case op::print_string:
                {
                    std::string s;

                    if(msg->copy_data_to<std::string>(s)) {
                        std::cout << "string: " << s << std::endl;
                        ret_ch->send(op::print_string);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        ret_ch->send(op::unknown);
                    }
                    break;
                }
                default:
                    std::cout << "unknown" << std::endl;;
                    ret_ch->send(op::unknown);
                    break;
            }
        }
    }, ch, ret_ch);

    st::sptr<st::message> msg;
    int i = 3;
    std::string s = "hello";
    garbazoo g;

    // print_int
    EXPECT_TRUE(ch->send(op::print_int, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::print_int);

    {
        // ensure `st::worker::send(st::sptr<st::message>)` works
        msg = st::message::make(op::print_int, i);
        EXPECT_TRUE(ch->send(msg));
        EXPECT_TRUE(ret_ch->recv(msg));
        EXPECT_EQ(msg->id(), op::print_int);
    }

    EXPECT_TRUE(ch->send(op::print_int, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::unknown);

    EXPECT_TRUE(ch->send(op::print_int, g));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::unknown);

    // print_string
    EXPECT_TRUE(ch->send(op::print_string, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::print_string);

    {
        // ensure `st::worker::send(st::sptr<st::message>)` works
        msg = st::message::make(op::print_string, s);
        EXPECT_TRUE(ch->send(op::print_string, s));
        EXPECT_TRUE(ret_ch->recv(msg));
        EXPECT_EQ(msg->id(), op::print_string);
    }

    EXPECT_TRUE(ch->send(op::print_string, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::unknown);

    EXPECT_TRUE(ch->send(op::print_string, g));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::unknown);

    // print_unknown
    EXPECT_TRUE(ch->send(op::unknown, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::unknown);

    {
        // ensure `st::worker::send(std::size_t)` works
        EXPECT_TRUE(ch->send(op::unknown));
        EXPECT_TRUE(ret_ch->recv(msg));
        EXPECT_EQ(msg->id(), op::unknown);
    }

    EXPECT_TRUE(ch->send(op::unknown, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::unknown);

    EXPECT_TRUE(ch->send(op::unknown, g));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::unknown);

    ch->close();
    EXPECT_TRUE(ch->closed());
    thd.join();
}

// show worker lifecycle behavior generally works
TEST(simple_thread, worker_lifecycle) {
    struct hdl {
        hdl(bool* running_ptr) : m_running_ptr(running_ptr) { *m_running_ptr = true; }
        ~hdl() { *m_running_ptr = false; }

        inline void operator()(st::sptr<st::message> msg) { 
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bool* m_running_ptr;
    };


    // shutdown via `st::worker::shutdown()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        auto wt = wkr->get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // double shutdown (check for breakages due to incorrect state transition)
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();
        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        auto wt = wkr->get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // hard shutdown via `st::worker::shutdown(false)`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        auto wt = wkr->get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // double hard shutdown (check for breakages due to incorrect state transition)
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown(false);
        wkr->shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        auto wt = wkr->get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // shutdown via `st::~worker()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr.reset();

        EXPECT_FALSE(thread_running);
        // cannot call `st::worker::running()` without valid pointer
    }
    
    // restart via `st::worker::restart()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        auto wt = wkr->get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // restart & shutdown via `st::worker::restart()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        wkr->restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }
    }

    // double shutdown (check for breakages due to incorrect state transition) 
    // followed by a restart
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();
        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        wkr->restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }
    }

    // hard restart & shutdown via `st::worker::restart(false)`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        wkr->restart(false);

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }
    }

    // double hard shutdown (check for breakages due to incorrect state 
    // transition) followed by a restart
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::sptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown(false);
        wkr->shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        wkr->restart(false);

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        {
            auto wt = wkr->get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }
    }
}

// showcase simple worker thread launching and message handling
TEST(simple_thread, worker_messaging) {
    // short for "handler"
    struct hdl {
        enum op {
            print_int,
            print_string,
            unknown
        };

        hdl(st::sptr<st::channel> ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id()) {
                case op::print_int:
                {
                    int i;

                    if(msg->copy_data_to<int>(i)) {
                        std::cout << "int: " << i << std::endl;
                        m_ret_ch->send(op::print_int);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        m_ret_ch->send(op::unknown);
                    }
                    break;
                }
                case op::print_string:
                {
                    std::string s;

                    if(msg->copy_data_to<std::string>(s)) {
                        std::cout << "string: " << s << std::endl;
                        m_ret_ch->send(op::print_string);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        m_ret_ch->send(op::unknown);
                    }
                    break;
                }
                default:
                    std::cout << "unknown" << std::endl;;
                    m_ret_ch->send(op::unknown);
                    break;
            }
        }
            
        st::sptr<st::channel> m_ret_ch; 
    };

    struct garbazoo { }; // random type unknown to worker
        
    // used for worker behavior confirmation purposes
    st::sptr<st::channel> ret_ch = st::channel::make();
    st::sptr<st::worker> wkr = st::worker::make<hdl>(ret_ch);
    st::sptr<st::message> msg;
    int i = 3;
    std::string s = "hello";
    garbazoo g;

    // print_int
    EXPECT_TRUE(wkr->send(hdl::op::print_int, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::print_int);

    {
        // ensure `st::worker::send(st::sptr<st::message>)` works
        msg = st::message::make(hdl::op::print_int, i);
        EXPECT_TRUE(wkr->send(msg));
        EXPECT_TRUE(ret_ch->recv(msg));
        EXPECT_EQ(msg->id(), hdl::op::print_int);
    }

    EXPECT_TRUE(wkr->send(hdl::op::print_int, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    EXPECT_TRUE(wkr->send(hdl::op::print_int, g));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    // print_string
    EXPECT_TRUE(wkr->send(hdl::op::print_string, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::print_string);

    {
        // ensure `st::worker::send(st::sptr<st::message>)` works
        msg = st::message::make(hdl::op::print_string, s);
        EXPECT_TRUE(wkr->send(hdl::op::print_string, s));
        EXPECT_TRUE(ret_ch->recv(msg));
        EXPECT_EQ(msg->id(), hdl::op::print_string);
    }

    EXPECT_TRUE(wkr->send(hdl::op::print_string, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    EXPECT_TRUE(wkr->send(hdl::op::print_string, g));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    // print_unknown
    EXPECT_TRUE(wkr->send(hdl::op::unknown, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    {
        // ensure `st::worker::send(std::size_t)` works
        EXPECT_TRUE(wkr->send(hdl::op::unknown));
        EXPECT_TRUE(ret_ch->recv(msg));
        EXPECT_EQ(msg->id(), hdl::op::unknown);
    }

    EXPECT_TRUE(wkr->send(hdl::op::unknown, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    EXPECT_TRUE(wkr->send(hdl::op::unknown, g));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);
}

/*
The purpose of this test is to showcase trivial message data payload type 
detection. 
 */
TEST(simple_thread, worker_multiple_payload_types) {
    struct hdl {
        // convenience typing
        typedef std::tuple<int, std::string> intstring_t;
        typedef std::tuple<std::string, int> stringint_t;

        // in this test we only want 1 valid message id
        enum op { discern_type }; 

        hdl(st::sptr<st::channel> ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id()) {
                case op::discern_type:
                {
                    if(msg->is<int>()) {
                        int i=0;
                        msg->copy_data_to(i);
                        std::cout << "int: " << i << std::endl;
                    } else if(msg->is<std::string>()) {
                        std::string s;
                        msg->copy_data_to(s);
                        std::cout << "string: " << s << std::endl;
                    } else if(msg->is<intstring_t>()) {
                        intstring_t is;
                        msg->copy_data_to(is);
                        std::cout << "int: " << std::get<0>(is) 
                                  << ", string: " << std::get<1>(is) 
                                  << std::endl;
                    } else if(msg->is<stringint_t>()) {
                        stringint_t si;
                        msg->copy_data_to(si);
                        std::cout << "int: " << std::get<0>(si) 
                                  << ", string: " << std::get<1>(si) 
                                  << std::endl;
                    } else {
                        ADD_FAILURE(); 
                    }
                    
                    m_ret_ch->send(msg->type_code());
                    break;
                }
                default:
                    ADD_FAILURE();
                    break;
            }
        }

        st::sptr<st::channel> m_ret_ch;
    };

    // used for worker behavior confirmation purposes
    st::sptr<st::channel> ret_ch = st::channel::make();
    st::sptr<st::worker> wkr = st::worker::make<hdl>(ret_ch);
    st::sptr<st::message> msg;
    int i = 0;
    std::string s = "Hello, my baby";
    hdl::intstring_t is{1,"Hello, my honey"};
    hdl::stringint_t si{"Hello, my ragtime gal", 2};

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, is));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::code<hdl::intstring_t>());

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::code<std::string>());

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::code<int>());

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, si));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::code<hdl::stringint_t>());
}

TEST(simple_thread, this_worker) {
    struct hdl {
        enum op { req_self };

        hdl(st::sptr<st::channel> ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id()) {
                case op::req_self:
                {
                    std::weak_ptr<st::worker> self = st::worker::this_worker();
                    m_ret_ch->send(0,self);
                    break;
                }
            }
        }

        st::sptr<st::channel> m_ret_ch;
    };

    st::sptr<st::channel> ret_ch = st::channel::make();
    st::sptr<st::worker> wkr = st::worker::make<hdl>(ret_ch);
    st::sptr<st::message> msg;
    std::weak_ptr<st::worker> wp;

    EXPECT_TRUE(wkr->send(hdl::op::req_self));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_TRUE(msg->copy_data_to(wp));
    EXPECT_EQ(wp.use_count(), 1);

    st::sptr<st::worker> new_wkr = wp.lock();

    EXPECT_TRUE(wkr);
    EXPECT_TRUE(new_wkr);
    EXPECT_EQ(wp.use_count(), 2);
    EXPECT_EQ(new_wkr.use_count(), 2);

    new_wkr.reset();

    EXPECT_TRUE(wkr);
    EXPECT_FALSE(new_wkr);
    EXPECT_EQ(wp.use_count(), 1);
    EXPECT_EQ(new_wkr.use_count(), 0);
}

TEST(simple_thread, weight) {
    struct hdl {
        hdl(st::sptr<st::channel> wait_ch) : m_wait_ch(wait_ch) { }

        inline void operator()(st::sptr<st::message> msg) {
            m_wait_ch->recv(msg);
        }

        st::sptr<st::channel> m_wait_ch;
    };

    auto wait_ch = st::channel::make();
    auto wkr1 = st::worker::make<hdl>(wait_ch);
    auto wkr2 = st::worker::make<hdl>(wait_ch);
    auto wkr3 = st::worker::make<hdl>(wait_ch);
    auto wkr4 = st::worker::make<hdl>(wait_ch);

    auto send_msgs = [](st::sptr<st::worker> wkr, int max) {
        for(int i=0; i<max; i++) {
            wkr->send(0);
        }
    };

    send_msgs(wkr1, 1);
    send_msgs(wkr2, 3);
    send_msgs(wkr3, 2);

    auto get_lightest_weight = [](std::vector<st::worker::weight> weights) -> int {
        typedef std::vector<st::worker::weight>::size_type vint;
        vint lightest_index = 0;
        auto lightest_weight = weights[0];

        for(vint i = 1; i<weights.size(); i++) {
            if(weights[i] < lightest_weight) {
                lightest_index = i;
                lightest_weight = weights[i];
            }
        }

        return lightest_index;
    };

    auto get_lightest = [](std::vector<st::worker*> wkrs) -> st::worker* {
        std::map<st::worker::weight, st::worker*> wkr_map;

        for(auto& w : wkrs) {
            wkr_map[w->get_weight()] = w;
        }

        return wkr_map.begin()->second;
    };

    {
        std::vector<st::worker::weight> v{
            wkr1->get_weight(), 
            wkr2->get_weight(),
            wkr3->get_weight(),
            wkr4->get_weight()
        };
        EXPECT_EQ(get_lightest_weight(v), 3);
    }

    {
        std::vector<st::worker::weight> v{
            wkr1->get_weight(), 
            wkr2->get_weight(),
            wkr3->get_weight()
        };
        EXPECT_EQ(get_lightest_weight(v), 0);
    }

    {
        std::vector<st::worker::weight>v{
            wkr2->get_weight(),
            wkr3->get_weight()
        };
        EXPECT_EQ(get_lightest_weight(v), 1);
    }

    {
        std::vector<st::worker*> v{
            wkr1.get(), 
            wkr2.get(),
            wkr3.get(),
            wkr4.get()
        };
        EXPECT_EQ(get_lightest(v), wkr4.get());
    }

    {
        std::vector<st::worker*> v{
            wkr1.get(), 
            wkr2.get(),
            wkr3.get()
        };
        EXPECT_EQ(get_lightest(v), wkr1.get());
    }

    {
        std::vector<st::worker*> v{
            wkr2.get(),
            wkr3.get()
        };
        EXPECT_EQ(get_lightest(v), wkr3.get());
    }

    wait_ch->close();
    EXPECT_TRUE(wait_ch->closed());
}

TEST(simple_thread, executor_process_task) {
    std::size_t wkr_cnt = 10;
    st::sptr<at::executor> exec = at::executor::make<at::processor>(wkr_cnt);
    auto ret_ch = st::channel::make();

    EXPECT_EQ(wkr_cnt, exec->worker_count());

    std::mutex mtx;
    int i(0);

    auto incr = [&] {
        std::lock_guard<std::mutex> lk(mtx);
        ++i;
        ret_ch->send(0);
    };

    for(std::size_t c=0; c<wkr_cnt; ++c) {
        exec->send(0, at::processor::task(incr));
    }

    for(std::size_t c=0; c<wkr_cnt; ++c) {
        st::sptr<st::message> msg;
        ret_ch->recv(msg);
    }

    EXPECT_EQ(10, i);
}

TEST(simple_thread, state_type_checks) {
    struct state1 : public at::state { };
    struct state2 : public at::state { };
    struct state3 : public at::state { };

    auto st1 = at::state::make<state1>();
    auto st2 = at::state::make<state2>();
    auto st3 = at::state::make<state3>();

    EXPECT_EQ(st1->type_code(), st::code<state1>());
    EXPECT_NE(st1->type_code(), st::code<state2>());
    EXPECT_NE(st1->type_code(), st::code<state3>());

    EXPECT_NE(st2->type_code(), st::code<state1>());
    EXPECT_EQ(st2->type_code(), st::code<state2>());
    EXPECT_NE(st2->type_code(), st::code<state3>());

    EXPECT_NE(st3->type_code(), st::code<state1>());
    EXPECT_NE(st3->type_code(), st::code<state2>());
    EXPECT_EQ(st3->type_code(), st::code<state3>());

    EXPECT_EQ(st1->type_code(), st1->type_code());
    EXPECT_NE(st1->type_code(), st2->type_code());
    EXPECT_NE(st1->type_code(), st3->type_code());

    EXPECT_NE(st2->type_code(), st1->type_code());
    EXPECT_EQ(st2->type_code(), st2->type_code());
    EXPECT_NE(st2->type_code(), st3->type_code());

    EXPECT_NE(st3->type_code(), st1->type_code());
    EXPECT_NE(st3->type_code(), st2->type_code());
    EXPECT_EQ(st3->type_code(), st3->type_code());

    EXPECT_TRUE(st1->is<state1>());
    EXPECT_FALSE(st1->is<state2>());
    EXPECT_FALSE(st1->is<state3>());

    EXPECT_FALSE(st2->is<state1>());
    EXPECT_TRUE(st2->is<state2>());
    EXPECT_FALSE(st2->is<state3>());

    EXPECT_FALSE(st3->is<state1>());
    EXPECT_FALSE(st3->is<state2>());
    EXPECT_TRUE(st3->is<state3>());
}

TEST(simple_thread, state_machine_basic_usage) {
    struct conversation {
        enum event {
            partner_speaks,
            you_speak 
        };
    };

    struct listening : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "your partner begins speaking and you listen" << std::endl;
            return st::sptr<st::message>();
        }
    };

    struct talking : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "you begin speaking and your partner listens" << std::endl;
            return st::sptr<st::message>();
        }
    };

    auto listening_st = at::state::make<listening>();
    auto talking_st = at::state::make<talking>();
    auto conversation_machine = at::state::machine::make();

    // register the state transitions 
    conversation_machine->register_transition(conversation::event::partner_speaks, listening_st);
    conversation_machine->register_transition(conversation::event::you_speak, talking_st);

    // set the initial machine state 
    conversation_machine->process_event(conversation::event::partner_speaks);

    // have a conversation
    conversation_machine->process_event(conversation::event::you_speak); 
    conversation_machine->process_event(conversation::event::partner_speaks); 

    auto cur_st = conversation_machine->current_status();
    EXPECT_EQ(conversation::event::partner_speaks, cur_st.event);
    EXPECT_EQ(listening_st, cur_st.state);
}

TEST(simple_thread, state_machine_with_guards_and_payload) {
    struct conversation {
        enum event {
            partner_speaks,
            you_speak 
        };
    };

    struct listening : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "your partner speaks: " << s << std::endl;
            return st::sptr<st::message>();
        }

        inline bool exit(st::sptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::partner_speaks) {
                return true;
            } else {
                return false;
            }
        }
    };

    struct talking : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "you speak: " << s << std::endl;
            return st::sptr<st::message>();
        }

        inline bool exit(st::sptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::you_speak) {
                return true;
            } else {
                return false;
            }
        }
    };

    auto listening_st = at::state::make<listening>();
    auto talking_st = at::state::make<talking>();
    auto conversation_machine = at::state::machine::make();

    // register the state transitions 
    conversation_machine->register_transition(conversation::event::partner_speaks, listening_st);
    conversation_machine->register_transition(conversation::event::you_speak, talking_st);

    // set the initial machine state and begin handling events (duplicate events 
    // will be ignored)
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo")); 
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo2")); 
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo3"));
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa")); 
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa2")); 
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa3")); 

    auto cur_st = conversation_machine->current_status();
    EXPECT_EQ(conversation::event::you_speak, cur_st.event);
    EXPECT_EQ(talking_st, cur_st.state);
}

TEST(simple_thread, state_machine_on_worker) {
    struct conversation_worker {
        enum op {
            partner_speaks,
            you_speak 
        };

        struct listening : public at::state {
            inline st::sptr<st::message> enter(st::sptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "your partner speaks: " << s << std::endl;
                return st::sptr<st::message>();
            }
        };

        struct talking : public at::state {
            inline st::sptr<st::message> enter(st::sptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "you speak: " << s << std::endl;
                return st::sptr<st::message>();
            }
        };

        conversation_worker() { 
            auto listening_st = at::state::make<listening>();
            auto talking_st = at::state::make<talking>();
            m_machine = at::state::machine::make();

            // register the state transitions 
            m_machine->register_transition(conversation_worker::op::partner_speaks, listening_st);
            m_machine->register_transition(conversation_worker::op::you_speak, talking_st);
        }

        ~conversation_worker() {
            auto cur_st = m_machine->current_status();
            EXPECT_EQ(conversation_worker::op::you_speak, cur_st.event);
        }

        inline void operator()(st::sptr<st::message> msg) {
            m_machine->process_event(msg);
        }

        st::sptr<at::state::machine> m_machine;
    };

    // launch a worker thread to utilize the state machine
    auto wkr = st::worker::make<conversation_worker>();

    // set the initial machine state and begin handling events
    wkr->send(conversation_worker::op::partner_speaks, std::string("hello foo"));
    wkr->send(conversation_worker::op::you_speak, std::string("hello faa")); 
    wkr->send(conversation_worker::op::partner_speaks, std::string("goodbye foo")); 
    wkr->send(conversation_worker::op::you_speak, std::string("goodbye faa")); 
}

TEST(simple_thread, state_machine_transitory_state) {
    struct events {
        enum op {
            event1,
            event2,
            event3
        };
    };

    bool reached_state1=false;
    bool reached_state2=false;
    bool reached_state3=false;

    struct state1 : public at::state {
        state1(bool& b) : m_b(b) { }

        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            m_b = true;
            std::cout << "state1" << std::endl;
            return st::message::make(events::event2);
        }

        bool& m_b;
    };

    struct state2 : public at::state {
        state2(bool& b) : m_b(b) { }

        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            m_b = true;
            std::cout << "state2" << std::endl;
            return st::message::make(events::event3);
        }

        bool& m_b;
    };

    struct state3 : public at::state {
        state3(bool& b) : m_b(b) { }

        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            m_b = true;
            std::cout << "state3" << std::endl;
            return st::sptr<st::message>();
        }

        bool& m_b;
    };

    auto sm = at::state::machine::make();
    sm->register_transition(events::event1, at::state::make<state1>(reached_state1));
    sm->register_transition(events::event2, at::state::make<state2>(reached_state2));
    sm->register_transition(events::event3, at::state::make<state3>(reached_state3));

    sm->process_event(events::event1);

    EXPECT_TRUE(reached_state1);
    EXPECT_TRUE(reached_state2);
    EXPECT_TRUE(reached_state3);

    auto cur_st = sm->current_status();
    EXPECT_EQ(events::event3, cur_st.event);
}

TEST(simple_thread, state_machine_callback) {
    struct actual_state : public at::state { };

    bool enter_flag = false;

    auto callback = [&](st::sptr<st::message> event) {
        std::cout << "I have arrived" << std::endl;
        enter_flag = true;
        return st::sptr<st::message>();
    };

    auto sm = at::state::machine::make();
    auto as = at::state::make<actual_state>();

    sm->register_transition(0, as);
    sm->register_callback(1, callback);

    sm->process_event(0);
    sm->process_event(1);

    auto sts = sm->current_status();

    EXPECT_TRUE(enter_flag);
    EXPECT_EQ(sts.event, 0);
    EXPECT_EQ(sts.state, as);
}


TEST(simple_thread, state_machine_callback_cascade) {
    enum class op {
        trigger_cb1,
        trigger_cb2,
        trigger_final_state
    };

    struct final_state : public at::state { };

    bool cb1_flag = false;
    bool cb2_flag = false;

    auto callback1 = [&](st::sptr<st::message> event) {
        cb1_flag = true;
        return st::message::make(op::trigger_cb2);
    };

    auto callback2 = [&](st::sptr<st::message> event) {
        cb2_flag = true;
        return st::message::make(op::trigger_final_state);
    };

    auto sm = at::state::machine::make();
    auto sts = sm->current_status();

    EXPECT_FALSE(sts);

    auto fs = at::state::make<final_state>();

    sm->register_callback(op::trigger_cb1, callback1);
    sm->register_callback(op::trigger_cb2, callback2);
    sm->register_transition(op::trigger_final_state, fs);

    sm->process_event(op::trigger_cb1);

    sts = sm->current_status();

    EXPECT_TRUE(cb1_flag);
    EXPECT_TRUE(cb2_flag);
    EXPECT_EQ(sts.event, 2);
    EXPECT_EQ(sts.state, fs);
}

// README EXAMPLES 
TEST(simple_thread, readme_example1) {
    struct MyClass {
        enum op {
            hello,
            world
        };

        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id()) {
                case op::hello:
                    std::cout << "hello " << std::endl;
                    break;
                case op::world:
                    std::cout << "world" << std::endl;
                    break;
            }
        }
    };

    st::sptr<st::worker> my_worker = st::worker::make<MyClass>();

    my_worker->send(MyClass::op::hello);
    my_worker->send(MyClass::op::world);
}

TEST(simple_thread, readme_example2) {
    struct MyClass {
        enum op {
            print
        };

        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id()) {
                case op::print:
                {
                    std::string s;
                    if(msg->copy_data_to(s)) {
                        std::cout << s << std::endl;
                    }
                    break;
                }
            }
        }
    };

    st::sptr<st::worker> my_worker = st::worker::make<MyClass>();

    std::string s("hello again");
    my_worker->send(MyClass::op::print, s);
}

TEST(simple_thread, readme_example3) {
    struct MyClass {
        enum op {
            print
        };

        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id()) {
                case op::print:
                    if(msg->is<std::string>()) {
                        std::string s;
                        msg->copy_data_to(s);
                        std::cout << s;
                    } else if(msg->is<int>()) {
                        int i = 0;
                        msg->copy_data_to(i);
                        std::cout << i;
                    }
                    break;
            }
        }
    };

    st::sptr<st::worker> my_worker = st::worker::make<MyClass>();

    std::string s("hello ");
    my_worker->send(MyClass::op::print, s);
    int i = 1;
    my_worker->send(MyClass::op::print, i);
    s = " more time\n";
    my_worker->send(MyClass::op::print, s);
}

TEST(simple_thread, readme_example4) {
    struct MyClass {
        MyClass(std::string constructor_string, std::string destructor_string) :
            m_destructor_string(destructor_string)
        {
            std::cout << std::this_thread::get_id() << ":" << constructor_string << std::endl;
        }

        ~MyClass() {
            std::cout << std::this_thread::get_id() << ":" <<  m_destructor_string << std::endl;
        }

        inline void operator()(st::sptr<st::message> msg) { }

        std::string m_destructor_string;
    };

    std::cout << std::this_thread::get_id() << ":" <<  "parent thread started" << std::endl;
    st::sptr<st::worker> wkr = st::worker::make<MyClass>("hello", "goodbye");
}


TEST(simple_thread, readme_example5) {
    struct MyClass {
        static inline MyClass make() {
            return MyClass(st::worker::make<Worker>());
        }

        inline void set_string(std::string txt) {
            m_wkr->send(op::eset_string, txt);
        }

        inline std::string get_string() {
            auto ret_ch = st::channel::make();
            m_wkr->send(op::eget_string, ret_ch);
            std::string s;
            st::sptr<st::message> msg;
            ret_ch->recv(msg);
            msg->copy_data_to(s);
            return s;
        }

    private:
        enum op {
            eset_string,
            eget_string
        };

        struct Worker { 
            inline void operator()(st::sptr<st::message> msg) {
                switch(msg->id()) {
                    case op::eset_string:
                        msg->copy_data_to(m_str);
                        break;
                    case op::eget_string:
                    {
                        st::sptr<st::channel> ret_ch;
                        if(msg->copy_data_to(ret_ch)) {
                            ret_ch->send(0,m_str);
                        }
                        break;
                    }
                }
            }

            std::string m_str;
        };

        MyClass(st::sptr<st::worker> wkr) : m_wkr(wkr) { }

        st::sptr<st::worker> m_wkr;
    };

    MyClass my_class = MyClass::make();
    my_class.set_string("hello");
    std::cout << my_class.get_string() << std::endl;
    my_class.set_string("hello hello");
    std::cout << my_class.get_string() << std::endl;
}

TEST(simple_thread, readme_example6) {
    struct MyClass {
        enum op {
            forward
        };

        MyClass(st::sptr<st::channel> fwd_ch) : m_fwd_ch(fwd_ch) { }

        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id()) {
                case op::forward:
                    m_fwd_ch->send(msg);
                    break;
            }
        }

        st::sptr<st::channel> m_fwd_ch;
    };

    st::sptr<st::channel> my_channel = st::channel::make();
    st::sptr<st::worker> my_worker = st::worker::make<MyClass>(my_channel);

    my_worker->send(MyClass::op::forward, std::string("forward this string"));
    
    st::sptr<st::message> msg;
    my_channel->recv(msg);

    std::string s;
    if(msg->copy_data_to(s)) {
        std::cout << s << std::endl;
    }
}

TEST(simple_thread, readme_example7) {
    auto looping_recv = [](st::sptr<st::channel> ch) {
        st::sptr<st::message> msg;

        while(ch->recv(msg)) {
            std::string s;
            if(msg->copy_data_to(s)) {
                std::cout << s << std::endl;
            }
        }

        std::cout << "thread done" << std::endl;
    };

    st::sptr<st::channel> my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);
    st::sptr<st::message> msg;

    my_channel->send(0, std::string("You say goodbye"));
    my_channel->send(0, std::string("And I say hello"));

    my_channel->close(); // end thread looping 
    EXPECT_TRUE(my_channel->closed());
    my_thread.join(); // join thread
}

TEST(advanced_thread, readme_processor_example1) {
    st::sptr<st::worker> proc = st::worker::make<at::processor>();

    std::cout << std::this_thread::get_id() << ": main thread\n";

    auto greet = [] {
        std::stringstream ss;
        ss << std::this_thread::get_id() << ": hello\n";
        std::cout << ss.str().c_str();
    };

    for(std::size_t c=0; c<5; ++c) {
        proc->send(0, at::processor::task(greet));
    }
}

TEST(advanced_thread, readme_cotask_example1) {
}

TEST(advanced_thread, readme_executor_example1) {
    std::size_t wkr_cnt = at::executor::default_worker_count();
    st::sptr<at::executor> exec = at::executor::make<at::processor>(wkr_cnt);

    std::cout << std::this_thread::get_id() << ": worker count: " << exec->worker_count() << std::endl;

    auto greet = [] {
        std::stringstream ss;
        ss << std::this_thread::get_id() << ": hello\n";
        std::cout << ss.str().c_str();
    };

    for(std::size_t c=0; c<5; ++c) {
        exec->send(0, at::processor::task(greet));
    }
}

TEST(advanced_thread, readme_state_example1) {
    struct conversation {
        enum event {
            partner_speaks,
            you_speak 
        };
    };

    struct listening : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "your partner begins speaking and you listen" << std::endl;
            // a default (null) shared pointer returned from enter() causes transition to continue normally
            return st::sptr<st::message>(); 
        }
    };

    struct talking : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "you begin speaking and your partner listens" << std::endl;
            return st::sptr<st::message>();
        }
    };

    auto listening_st = at::state::make<listening>();
    auto talking_st = at::state::make<talking>();
    auto conversation_machine = at::state::machine::make();

    // register the state transitions 
    conversation_machine->register_transition(conversation::event::partner_speaks, listening_st);
    conversation_machine->register_transition(conversation::event::you_speak, talking_st);

    // set the initial machine state 
    conversation_machine->process_event(conversation::event::partner_speaks);

    // have a conversation
    conversation_machine->process_event(conversation::event::you_speak); 
    conversation_machine->process_event(conversation::event::partner_speaks); 
}

TEST(advanced_thread, readme_state_example2) {
    struct conversation_worker {
        enum op {
            partner_speaks,
            you_speak 
        };

        struct listening : public at::state {
            inline st::sptr<st::message> enter(st::sptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "your partner speaks: " << s << std::endl;
                return st::sptr<st::message>();
            }
        };

        struct talking : public at::state {
            inline st::sptr<st::message> enter(st::sptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "you speak: " << s << std::endl;
                return st::sptr<st::message>();
            }
        };

        conversation_worker() { 
            auto listening_st = at::state::make<listening>();
            auto talking_st = at::state::make<talking>();
            m_machine = at::state::machine::make();

            // register the state transitions 
            m_machine->register_transition(conversation_worker::op::partner_speaks, listening_st);
            m_machine->register_transition(conversation_worker::op::you_speak, talking_st);
        }

        inline void operator()(st::sptr<st::message> msg) {
            m_machine->process_event(msg);
        }

        st::sptr<at::state::machine> m_machine;
    };

    // launch a worker thread to utilize the state machine
    auto wkr = st::worker::make<conversation_worker>();

    // set the initial machine state and begin handling events
    wkr->send(conversation_worker::op::partner_speaks, std::string("hello foo"));
    wkr->send(conversation_worker::op::you_speak, std::string("hello faa")); 
    wkr->send(conversation_worker::op::partner_speaks, std::string("goodbye foo")); 
    wkr->send(conversation_worker::op::you_speak, std::string("goodbye faa")); 
}

TEST(advanced_thread, readme_state_example3) {
    struct conversation {
        enum event {
            partner_speaks,
            you_speak 
        };
    };

    struct listening : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "your partner speaks: " << s << std::endl;
            return st::sptr<st::message>();
        }

        inline bool exit(st::sptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::partner_speaks) {
                return true;
            } else {
                return false;
            }
        }
    };

    struct talking : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "you speak: " << s << std::endl;
            return st::sptr<st::message>();
        }

        inline bool exit(st::sptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::you_speak) {
                return true;
            } else {
                return false;
            }
        }
    };

    auto listening_st = at::state::make<listening>();
    auto talking_st = at::state::make<talking>();
    auto conversation_machine = at::state::machine::make();

    // register the state transitions 
    conversation_machine->register_transition(conversation::event::partner_speaks, listening_st);
    conversation_machine->register_transition(conversation::event::you_speak, talking_st);

    // set the initial machine state and begin handling events (duplicate events 
    // will be ignored)
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo")); 
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo2")); 
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo3"));
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa")); 
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa2")); 
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa3")); 
}

TEST(advanced_thread, readme_state_example4) {
    struct events {
        enum op {
            event1,
            event2,
            event3
        };
    };

    struct state1 : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "state1" << std::endl;
            return st::message::make(events::event2);
        }
    };

    struct state2 : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "state2" << std::endl;
            return st::message::make(events::event3);
        }
    };

    struct state3 : public at::state {
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "state3" << std::endl;
            return st::sptr<st::message>();
        }
    };

    auto sm = at::state::machine::make();
    sm->register_transition(events::event1, at::state::make<state1>());
    sm->register_transition(events::event2, at::state::make<state2>());
    sm->register_transition(events::event3, at::state::make<state3>());

    sm->process_event(events::event1);
}

TEST(advanced_thread, readme_state_example5) {
    enum class op {
        trigger_cb1,
        trigger_cb2,
        trigger_final_state
    };

    auto callback1 = [&](st::sptr<st::message> event) {
        std::cout << "We " << std::endl;
        return st::message::make(op::trigger_cb2);
    };

    auto callback2 = [&](st::sptr<st::message> event) {
        std::cout << "made " << std::endl;
        return st::message::make(op::trigger_final_state);
    };

    struct final_state : public at::state { 
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "it!" << std::endl;
            return st::sptr<st::message>();
        }
    };

    auto sm = at::state::machine::make();

    sm->register_callback(op::trigger_cb1, callback1);
    sm->register_callback(op::trigger_cb2, callback2);
    sm->register_transition(op::trigger_final_state, at::state::make<final_state>());

    sm->process_event(op::trigger_cb1);
}
