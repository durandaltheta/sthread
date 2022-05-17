//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis
#include <gtest/gtest.h>
#include "simple_thread.hpp"
#include <thread>
#include <string>
#include <iostream>
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
        std::shared_ptr<st::message> msg = st::message::make(op::integer,i);

        EXPECT_EQ(msg->id(), op::integer);
        EXPECT_NE(msg->id(), op::string);
        EXPECT_EQ(msg->type(), st::message::code<int>());
        EXPECT_NE(msg->type(), st::message::code<std::string>());
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
        std::shared_ptr<st::message> msg = st::message::make(op::string,s);

        EXPECT_EQ(msg->id(), op::string);
        EXPECT_NE(msg->id(), op::integer);
        EXPECT_EQ(msg->type(), st::message::code<std::string>());
        EXPECT_NE(msg->type(), st::message::code<int>());
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
    std::shared_ptr<st::channel> ret_ch = st::channel::make();

    std::shared_ptr<st::channel> ch = st::channel::make();
    std::thread thd([](std::shared_ptr<st::channel> ch, std::shared_ptr<st::channel> ret_ch) {
        std::shared_ptr<st::message> msg;

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

    std::shared_ptr<st::message> msg;
    int i = 3;
    std::string s = "hello";
    garbazoo g;

    // print_int
    EXPECT_TRUE(ch->send(op::print_int, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), op::print_int);

    {
        // ensure `st::worker::send(std::shared_ptr<st::message>)` works
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
        // ensure `st::worker::send(std::shared_ptr<st::message>)` works
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
    thd.join();
}

// show worker lifecycle behavior generally works
TEST(simple_thread, worker_lifecycle) {
    struct hdl {
        hdl(bool* running_ptr) : m_running_ptr(running_ptr) { *m_running_ptr = true; }
        ~hdl() { *m_running_ptr = false; }

        inline void operator()(std::shared_ptr<st::message> msg) { 
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bool* m_running_ptr;
    };


    // shutdown via `st::worker::shutdown()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());
        EXPECT_EQ(wkr->queued(),0);
    }

    // hard shutdown via `st::worker::shutdown(false)`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());
        EXPECT_NE(wkr->queued(),0);
    }

    // shutdown via `st::~worker()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

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
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());
        EXPECT_EQ(wkr->queued(),0);
    }

    // restart & shutdown via `st::worker::restart()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());
        EXPECT_EQ(wkr->queued(),0);

        wkr->restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());
        EXPECT_EQ(wkr->queued(),0);
    }

    // hard restart & shutdown via `st::worker::restart(false)`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        // fill message q
        for(int i=0; i<10; i++) {
            wkr->send(0,0);
        }

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown(false);

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());
        EXPECT_NE(wkr->queued(),0);

        wkr->restart(false);

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());
        EXPECT_EQ(wkr->queued(),0);
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

        hdl(std::shared_ptr<st::channel> ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(std::shared_ptr<st::message> msg) {
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
            
        std::shared_ptr<st::channel> m_ret_ch; 
    };

    struct garbazoo { }; // random type unknown to worker
        
    // used for worker behavior confirmation purposes
    std::shared_ptr<st::channel> ret_ch = st::channel::make();
    std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(ret_ch);
    std::shared_ptr<st::message> msg;
    int i = 3;
    std::string s = "hello";
    garbazoo g;

    // print_int
    EXPECT_TRUE(wkr->send(hdl::op::print_int, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::print_int);

    {
        // ensure `st::worker::send(std::shared_ptr<st::message>)` works
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
        // ensure `st::worker::send(std::shared_ptr<st::message>)` works
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

        hdl(std::shared_ptr<st::channel> ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(std::shared_ptr<st::message> msg) {
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
                    
                    m_ret_ch->send(msg->type());
                    break;
                }
                default:
                    ADD_FAILURE();
                    break;
            }
        }

        std::shared_ptr<st::channel> m_ret_ch;
    };

    // used for worker behavior confirmation purposes
    std::shared_ptr<st::channel> ret_ch = st::channel::make();
    std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(ret_ch);
    std::shared_ptr<st::message> msg;
    int i = 0;
    std::string s = "Hello, my baby";
    hdl::intstring_t is{1,"Hello, my honey"};
    hdl::stringint_t si{"Hello, my ragtime gal", 2};

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, is));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::message::code<hdl::intstring_t>());

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, s));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::message::code<std::string>());

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, i));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::message::code<int>());

    EXPECT_TRUE(wkr->send(hdl::op::discern_type, si));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_EQ(msg->id(), st::message::code<hdl::stringint_t>());
}

TEST(simple_thread, this_worker) {
    struct hdl {
        enum op { req_self };

        hdl(std::shared_ptr<st::channel> ret_ch) : m_ret_ch(ret_ch) { }

        inline void operator()(std::shared_ptr<st::message> msg) {
            switch(msg->id()) {
                case op::req_self:
                {
                    std::weak_ptr<st::worker> self = st::worker::this_worker();
                    m_ret_ch->send(0,self);
                    break;
                }
            }
        }

        std::shared_ptr<st::channel> m_ret_ch;
    };

    std::shared_ptr<st::channel> ret_ch = st::channel::make();
    std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(ret_ch);
    std::shared_ptr<st::message> msg;
    std::weak_ptr<st::worker> wp;

    EXPECT_TRUE(wkr->send(hdl::op::req_self));
    EXPECT_TRUE(ret_ch->recv(msg));
    EXPECT_TRUE(msg->copy_data_to(wp));
    EXPECT_EQ(wp.use_count(), 1);

    std::shared_ptr<st::worker> new_wkr = wp.lock();

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
        hdl(std::shared_ptr<st::channel> wait_ch) : m_wait_ch(wait_ch) { }

        inline void operator()(std::shared_ptr<st::message> msg) {
            m_wait_ch->recv(msg);
        }

        std::shared_ptr<st::channel> m_wait_ch;
    };

    auto wait_ch = st::channel::make();
    auto wkr1 = st::worker::make<hdl>(wait_ch);
    auto wkr2 = st::worker::make<hdl>(wait_ch);
    auto wkr3 = st::worker::make<hdl>(wait_ch);
    auto wkr4 = st::worker::make<hdl>(wait_ch);

    auto send_msgs = [](std::shared_ptr<st::worker> wkr, int max) {
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
}

// README EXAMPLES 
TEST(simple_thread, readme_example1) {
    struct MyClass {
        enum op {
            hello,
            world
        };

        inline void operator()(std::shared_ptr<st::message> msg) {
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

    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    my_worker->send(MyClass::op::hello);
    my_worker->send(MyClass::op::world);
}

TEST(simple_thread, readme_example2) {
    struct MyClass {
        enum op {
            print
        };

        inline void operator()(std::shared_ptr<st::message> msg) {
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

    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    std::string s("hello again");
    my_worker->send(MyClass::op::print, s);
}

TEST(simple_thread, readme_example3) {
    struct MyClass {
        enum op {
            print
        };

        inline void operator()(std::shared_ptr<st::message> msg) {
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

    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

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

        inline void operator()(std::shared_ptr<st::message> msg) { }

        std::string m_destructor_string;
    };

    std::cout << std::this_thread::get_id() << ":" <<  "parent thread started" << std::endl;
    std::shared_ptr<st::worker> wkr = st::worker::make<MyClass>("hello", "goodbye");
}

TEST(simple_thread, readme_example5) {
    struct MyClass {
        enum op {
            forward
        };

        MyClass(std::shared_ptr<st::channel> fwd_ch) : m_fwd_ch(fwd_ch) { }

        inline void operator()(std::shared_ptr<st::message> msg) {
            switch(msg->id()) {
                case op::forward:
                    m_fwd_ch->send(msg);
                    break;
            }
        }

        std::shared_ptr<st::channel> m_fwd_ch;
    };

    std::shared_ptr<st::channel> my_channel = st::channel::make();
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>(my_channel);

    my_worker->send(MyClass::op::forward, std::string("forward this string"));
    
    std::shared_ptr<st::message> msg;
    my_channel->recv(msg);

    std::string s;
    if(msg->copy_data_to(s)) {
        std::cout << s << std::endl;
    }
}

TEST(simple_thread, readme_example6) {
    auto looping_recv = [](std::shared_ptr<st::channel> ch) {
        std::shared_ptr<st::message> msg;

        while(ch->recv(msg)) {
            std::string s;
            if(msg->copy_data_to(s)) {
                std::cout << s << std::endl;
            }
        }

        std::cout << "thread done" << std::endl;
    };

    std::shared_ptr<st::channel> my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);
    std::shared_ptr<st::message> msg;

    my_channel->send(0, std::string("You say goodbye"));
    my_channel->send(0, std::string("And I say hello"));

    my_channel->close(); // end thread looping 
    my_thread.join(); // join thread
}
