//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis
#include <gtest/gtest.h>
#include <thread>
#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <tuple>
#include <vector>
#include <map>
#include <functional>
#include "sthread"

TEST(simple_thread, message) {
    enum op {
        unknown,
        integer,
        cstring,
        string
    };

    // int message
    {
        int i = 14;
        st::message msg = st::message::make(op::integer,i);

        EXPECT_EQ(msg.id(), op::integer);
        EXPECT_NE(msg.id(), op::string);
        EXPECT_EQ(msg.data().type_code(), st::code<int>());
        EXPECT_NE(msg.data().type_code(), st::code<std::string>());
        EXPECT_TRUE(msg.data().is<int>());
        EXPECT_FALSE(msg.data().is<std::string>());

        {
            std::string s = "";
            EXPECT_FALSE(msg.data().copy_to(s));
        }

        {
            std::string s = "";
            EXPECT_FALSE(msg.data().move_to(s));
        }

        {
            int i2 = 0;
            EXPECT_TRUE(msg.data().copy_to(i2));
            EXPECT_EQ(i, i2);
        }

        {
            int i2 = 0;
            EXPECT_TRUE(msg.data().move_to(i2));
            EXPECT_EQ(i, i2);
        }

        // 2nd successful move should prove that data was swapped with 1st move
        {
            int i2 = 0;
            EXPECT_TRUE(msg.data().move_to(i2));
            EXPECT_NE(i, i2);
            EXPECT_EQ(i2, 0);
        }
    }
   
    // c string message
    {
        const char* s = "codemonkey";
        st::message msg = st::message::make(op::cstring,s);

        EXPECT_EQ(msg.id(), op::cstring);
        EXPECT_NE(msg.id(), op::integer);
        EXPECT_EQ(msg.data().type_code(), st::code<std::string>());
        EXPECT_NE(msg.data().type_code(), st::code<int>());
        EXPECT_TRUE(msg.data().is<std::string>());
        EXPECT_FALSE(msg.data().is<int>());

        {
            int i = 0;
            EXPECT_FALSE(msg.data().copy_to(i));
        }

        {
            int i = 0;
            EXPECT_FALSE(msg.data().move_to(i));
        }

        {
            std::string s2 = "";
            EXPECT_TRUE(msg.data().copy_to(s2));
            EXPECT_EQ(s, s2);
        }

        {
            std::string s2 = "";
            EXPECT_TRUE(msg.data().move_to(s2));
            EXPECT_EQ(s, s2);
        }

        // 2nd successful move should prove that data was swapped with 1st move
        {
            std::string s2 = "";
            EXPECT_TRUE(msg.data().move_to(s2));
            EXPECT_NE(s, s2);
            EXPECT_EQ(s2, "");
        }
    }

    // std::string message
    {
        std::string s = "getupgetcoffee";
        st::message msg = st::message::make(op::string,s);

        EXPECT_EQ(msg.id(), op::string);
        EXPECT_NE(msg.id(), op::integer);
        EXPECT_EQ(msg.data().type_code(), st::code<std::string>());
        EXPECT_NE(msg.data().type_code(), st::code<int>());
        EXPECT_TRUE(msg.data().is<std::string>());
        EXPECT_FALSE(msg.data().is<int>());

        {
            int i = 0;
            EXPECT_FALSE(msg.data().copy_to(i));
        }

        {
            int i = 0;
            EXPECT_FALSE(msg.data().move_to(i));
        }

        {
            std::string s2 = "";
            EXPECT_TRUE(msg.data().copy_to(s2));
            EXPECT_EQ(s, s2);
        }

        {
            std::string s2 = "";
            EXPECT_TRUE(msg.data().move_to(s2));
            EXPECT_EQ(s, s2);
        }

        // 2nd successful move should prove that data was swapped with 1st move
        {
            std::string s2 = "";
            EXPECT_TRUE(msg.data().move_to(s2));
            EXPECT_NE(s, s2);
            EXPECT_EQ(s2, "");
        }
    }
}

// show channels generally work
TEST(simple_thread, channel) {
    struct garbazoo { }; // random type unknown to fiber

    enum op {
        print_int,
        print_string,
        unknown
    };
        
    // used for fiber behavior confirmation purposes
    ret_ch = st::channel::make();
    ch = st::channel::make();

    std::thread thd([]( ch,  ret_ch) {
        st::message msg;

        while(ch.recv(msg)) {
            switch(msg.id()) {
                case op::print_int:
                {
                    int i;

                    if(msg.data().copy_to<int>(i)) {
                        std::cout << "int: " << i << std::endl;
                        ret_ch.send(op::print_int);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        ret_ch.send(op::unknown);
                    }
                    break;
                }
                case op::print_string:
                {
                    std::string s;

                    if(msg.data().copy_to<std::string>(s)) {
                        std::cout << "string: " << s << std::endl;
                        ret_ch.send(op::print_string);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        ret_ch.send(op::unknown);
                    }
                    break;
                }
                default:
                    std::cout << "unknown" << std::endl;;
                    ret_ch.send(op::unknown);
                    break;
            }
        }
    }, ch, ret_ch);

    st::message msg;
    int i = 3;
    std::string s = "hello";
    garbazoo g;

    // print_int
    EXPECT_TRUE(ch.send(op::print_int, i));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::print_int);

    {
        // ensure `st::fiber::send(st::message)` works
        msg = st::message::make(op::print_int, i);
        EXPECT_TRUE(ch.send(msg));
        EXPECT_TRUE(ret_ch.recv(msg));
        EXPECT_EQ(msg.id(), op::print_int);
    }

    EXPECT_TRUE(ch.send(op::print_int, s));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::unknown);

    EXPECT_TRUE(ch.send(op::print_int, g));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::unknown);

    // print_string
    EXPECT_TRUE(ch.send(op::print_string, s));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::print_string);

    {
        // ensure `st::fiber::send(st::message)` works
        msg = st::message::make(op::print_string, s);
        EXPECT_TRUE(ch.send(op::print_string, s));
        EXPECT_TRUE(ret_ch.recv(msg));
        EXPECT_EQ(msg.id(), op::print_string);
    }

    EXPECT_TRUE(ch.send(op::print_string, i));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::unknown);

    EXPECT_TRUE(ch.send(op::print_string, g));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::unknown);

    // print_unknown
    EXPECT_TRUE(ch.send(op::unknown, i));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::unknown);

    {
        // ensure `st::fiber::send(std::size_t)` works
        EXPECT_TRUE(ch.send(op::unknown));
        EXPECT_TRUE(ret_ch.recv(msg));
        EXPECT_EQ(msg.id(), op::unknown);
    }

    EXPECT_TRUE(ch.send(op::unknown, s));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::unknown);

    EXPECT_TRUE(ch.send(op::unknown, g));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), op::unknown);

    ch.close();
    EXPECT_TRUE(ch.closed());
    thd.join();
}

// show fiber lifecycle behavior generally works
TEST(simple_thread, fiber_lifecycle) {
    struct hdl {
        hdl(bool* running_ptr) : m_running_ptr(running_ptr) { *m_running_ptr = true; }
        ~hdl() { *m_running_ptr = false; }

        inline void operator()(st::message msg) { 
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bool* m_running_ptr;
    };


    // shutdown via `st::fiber::shutdown()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        auto wt = fib.get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // double shutdown (check for breakages due to incorrect state transition)
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown();
        fib.shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        auto wt = fib.get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // hard shutdown via `st::fiber::shutdown(false)`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        auto wt = fib.get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // double hard shutdown (check for breakages due to incorrect state transition)
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown(false);
        fib.shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        auto wt = fib.get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // shutdown via `st::~fiber()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.reset();

        EXPECT_FALSE(thread_running);
        // cannot call `st::fiber::running()` without valid pointer
    }
    
    // restart via `st::fiber::restart()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        auto wt = fib.get_weight();
        EXPECT_TRUE(wt.empty());
        EXPECT_EQ(wt.queued, 0);
        EXPECT_FALSE(wt.executing);
    }

    // restart & shutdown via `st::fiber::restart()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        {
            auto wt = fib.get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        fib.restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        {
            auto wt = fib.get_weight();
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
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown();
        fib.shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        {
            auto wt = fib.get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        fib.restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        {
            auto wt = fib.get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }
    }

    // hard restart & shutdown via `st::fiber::restart(false)`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        {
            auto wt = fib.get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        fib.restart(false);

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        {
            auto wt = fib.get_weight();
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
        st::fiber fib = st::fiber::thread<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            fib.send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        fib.shutdown(false);
        fib.shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(fib.running());

        {
            auto wt = fib.get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }

        fib.restart(false);

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(fib.running());

        {
            auto wt = fib.get_weight();
            EXPECT_TRUE(wt.empty());
            EXPECT_EQ(wt.queued, 0);
            EXPECT_FALSE(wt.executing);
        }
    }
}

// showcase simple fiber thread launching and message handling
TEST(simple_thread, fiber_messaging) {
    // short for "handler"
    struct hdl {
        enum op {
            print_int,
            print_string,
            unknown
        };

        hdl( ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(st::message msg) {
            switch(msg.id()) {
                case op::print_int:
                {
                    int i;

                    if(msg.data().copy_to<int>(i)) {
                        std::cout << "int: " << i << std::endl;
                        m_ret_ch.send(op::print_int);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        m_ret_ch.send(op::unknown);
                    }
                    break;
                }
                case op::print_string:
                {
                    std::string s;

                    if(msg.data().copy_to<std::string>(s)) {
                        std::cout << "string: " << s << std::endl;
                        m_ret_ch.send(op::print_string);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        m_ret_ch.send(op::unknown);
                    }
                    break;
                }
                default:
                    std::cout << "unknown" << std::endl;;
                    m_ret_ch.send(op::unknown);
                    break;
            }
        }
            
         m_ret_ch; 
    };

    struct garbazoo { }; // random type unknown to fiber
        
    // used for fiber behavior confirmation purposes
     ret_ch = st::channel::make();
    st::fiber fib = st::fiber::thread<hdl>(ret_ch);
    st::message msg;
    int i = 3;
    std::string s = "hello";
    garbazoo g;

    // print_int
    EXPECT_TRUE(fib.send(hdl::op::print_int, i));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::print_int);

    {
        // ensure `st::fiber::send(st::message)` works
        msg = st::message::make(hdl::op::print_int, i);
        EXPECT_TRUE(fib.send(msg));
        EXPECT_TRUE(ret_ch.recv(msg));
        EXPECT_EQ(msg.id(), hdl::op::print_int);
    }

    EXPECT_TRUE(fib.send(hdl::op::print_int, s));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::unknown);

    EXPECT_TRUE(fib.send(hdl::op::print_int, g));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::unknown);

    // print_string
    EXPECT_TRUE(fib.send(hdl::op::print_string, s));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::print_string);

    {
        // ensure `st::fiber::send(st::message)` works
        msg = st::message::make(hdl::op::print_string, s);
        EXPECT_TRUE(fib.send(hdl::op::print_string, s));
        EXPECT_TRUE(ret_ch.recv(msg));
        EXPECT_EQ(msg.id(), hdl::op::print_string);
    }

    EXPECT_TRUE(fib.send(hdl::op::print_string, i));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::unknown);

    EXPECT_TRUE(fib.send(hdl::op::print_string, g));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::unknown);

    // print_unknown
    EXPECT_TRUE(fib.send(hdl::op::unknown, i));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::unknown);

    {
        // ensure `st::fiber::send(std::size_t)` works
        EXPECT_TRUE(fib.send(hdl::op::unknown));
        EXPECT_TRUE(ret_ch.recv(msg));
        EXPECT_EQ(msg.id(), hdl::op::unknown);
    }

    EXPECT_TRUE(fib.send(hdl::op::unknown, s));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::unknown);

    EXPECT_TRUE(fib.send(hdl::op::unknown, g));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), hdl::op::unknown);
}

/*
The purpose of this test is to showcase trivial message data payload type 
detection. 
 */
TEST(simple_thread, fiber_multiple_payload_types) {
    struct hdl {
        // convenience typing
        typedef std::tuple<int, std::string> intstring_t;
        typedef std::tuple<std::string, int> stringint_t;

        // in this test we only want 1 valid message id
        enum op { discern_type }; 

        hdl( ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(st::message msg) {
            switch(msg.id()) {
                case op::discern_type:
                {
                    if(msg.data().is<int>()) {
                        int i=0;
                        msg.data().copy_to(i);
                        std::cout << "int: " << i << std::endl;
                    } else if(msg.data().is<std::string>()) {
                        std::string s;
                        msg.data().copy_to(s);
                        std::cout << "string: " << s << std::endl;
                    } else if(msg.data().is<intstring_t>()) {
                        intstring_t is;
                        msg.data().copy_to(is);
                        std::cout << "int: " << std::get<0>(is) 
                                  << ", string: " << std::get<1>(is) 
                                  << std::endl;
                    } else if(msg.data().is<stringint_t>()) {
                        stringint_t si;
                        msg.data().copy_to(si);
                        std::cout << "int: " << std::get<0>(si) 
                                  << ", string: " << std::get<1>(si) 
                                  << std::endl;
                    } else {
                        ADD_FAILURE(); 
                    }
                    
                    m_ret_ch.send(msg.data().type_code());
                    break;
                }
                default:
                    ADD_FAILURE();
                    break;
            }
        }

         m_ret_ch;
    };

    // used for fiber behavior confirmation purposes
     ret_ch = st::channel::make();
    st::fiber fib = st::fiber::thread<hdl>(ret_ch);
    st::message msg;
    int i = 0;
    std::string s = "Hello, my baby";
    hdl::intstring_t is{1,"Hello, my honey"};
    hdl::stringint_t si{"Hello, my ragtime gal", 2};

    EXPECT_TRUE(fib.send(hdl::op::discern_type, is));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), st::code<hdl::intstring_t>());

    EXPECT_TRUE(fib.send(hdl::op::discern_type, s));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), st::code<std::string>());

    EXPECT_TRUE(fib.send(hdl::op::discern_type, i));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), st::code<int>());

    EXPECT_TRUE(fib.send(hdl::op::discern_type, si));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_EQ(msg.id(), st::code<hdl::stringint_t>());
}

TEST(simple_thread, this_fiber) {
    struct hdl {
        enum op { req_self };

        hdl( ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(st::message msg) {
            switch(msg.id()) {
                case op::req_self:
                {
                    std::weak_ptr<st::fiber self = st::fiber::this_fiber();
                    m_ret_ch.send(0,self);
                    break;
                }
            }
        }

         m_ret_ch;
    };

     ret_ch = st::channel::make();
    st::fiber fib = st::fiber::thread<hdl>(ret_ch);
    st::message msg;
    std::weak_ptr<st::fiber wp;

    EXPECT_TRUE(fib.send(hdl::op::req_self));
    EXPECT_TRUE(ret_ch.recv(msg));
    EXPECT_TRUE(msg.data().copy_to(wp));
    EXPECT_EQ(wp.use_count(), 1);

    st::fiber new_fib = wp.lock();

    EXPECT_TRUE(fib);
    EXPECT_TRUE(new_fib);
    EXPECT_EQ(wp.use_count(), 2);
    EXPECT_EQ(new_fib.use_count(), 2);

    new_fib.reset();

    EXPECT_TRUE(fib);
    EXPECT_FALSE(new_fib);
    EXPECT_EQ(wp.use_count(), 1);
    EXPECT_EQ(new_fib.use_count(), 0);
}

TEST(simple_thread, weight) {
    struct hdl {
        hdl( wait_ch) : m_wait_ch(wait_ch) { }

        inline void operator()(st::message msg) {
            m_wait_ch.recv(msg);
        }

         m_wait_ch;
    };

    auto wait_ch = st::channel::make();
    auto fib1 = st::fiber::thread<hdl>(wait_ch);
    auto fib2 = st::fiber::thread<hdl>(wait_ch);
    auto fib3 = st::fiber::thread<hdl>(wait_ch);
    auto fib4 = st::fiber::thread<hdl>(wait_ch);

    auto send_msgs = [](st::fiber fib, int max) {
        for(int i=0; i<max; i++) {
            fib.send(0);
        }
    };

    send_msgs(fib1, 1);
    send_msgs(fib2, 3);
    send_msgs(fib3, 2);

    auto get_lightest_weight = [](std::vector<st::fiber::weight> weights) . int {
        typedef std::vector<st::fiber::weight>::size_type vint;
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

    auto get_lightest = [](std::vector<st::fiber*> fibs) . st::fiber* {
        std::map<st::fiber::weight, st::fiber*> fib_map;

        for(auto& w : fibs) {
            fib_map[w.get_weight()] = w;
        }

        return fib_map.begin().second;
    };

    {
        std::vector<st::fiber::weight> v{
            fib1.get_weight(), 
            fib2.get_weight(),
            fib3.get_weight(),
            fib4.get_weight()
        };
        EXPECT_EQ(get_lightest_weight(v), 3);
    }

    {
        std::vector<st::fiber::weight> v{
            fib1.get_weight(), 
            fib2.get_weight(),
            fib3.get_weight()
        };
        EXPECT_EQ(get_lightest_weight(v), 0);
    }

    {
        std::vector<st::fiber::weight>v{
            fib2.get_weight(),
            fib3.get_weight()
        };
        EXPECT_EQ(get_lightest_weight(v), 1);
    }

    {
        std::vector<st::fiber*> v{
            fib1.get(), 
            fib2.get(),
            fib3.get(),
            fib4.get()
        };
        EXPECT_EQ(get_lightest(v), fib4.get());
    }

    {
        std::vector<st::fiber*> v{
            fib1.get(), 
            fib2.get(),
            fib3.get()
        };
        EXPECT_EQ(get_lightest(v), fib1.get());
    }

    {
        std::vector<st::fiber*> v{
            fib2.get(),
            fib3.get()
        };
        EXPECT_EQ(get_lightest(v), fib3.get());
    }

    wait_ch.close();
    EXPECT_TRUE(wait_ch.closed());
}

TEST(simple_thread_readme, example1) {
    struct MyClass {
        enum op {
            hello,
            world
        };

        void operator()(st::message msg) {
            switch(msg.id()) {
                case op::hello:
                    std::cout << "hello " << std::endl;
                    break;
                case op::world:
                    std::cout << "world" << std::endl;
                    break;
            }
        }
    };

    st::fiber my_thread = st::fiber::thread<MyClass>();
    my_thread.send(MyClass::op::hello);
    my_thread.send(MyClass::op::world);
}

TEST(simple_thread_readme, example2) {
    struct MyClass {
        enum op {
            print
        };

        void operator()(st::message msg) {
            switch(msg.id()) {
                case op::print:
                {
                    std::string s;
                    if(msg.data().copy_to(s)) {
                        std::cout << s << std::endl;
                    } else {
                        std::cout << "message data was not a string" << std::endl;
                    }
                    break;
                }
            }
        }
    };

    st::fiber my_thread = st::fiber::thread<MyClass>();

    my_thread.send(MyClass::op::print, std::string("hello again"));
    my_thread.send(MyClass::op::print, 14);
}
