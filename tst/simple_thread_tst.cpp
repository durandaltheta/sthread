#include <gtest/gtest.h>
#include "simple_thread.hpp"
#include <tuple>

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

        inline void operator()(std::shared_ptr<st::message> msg) { }
        bool* m_running_ptr;
    };

    // shutdown via `st::worker::shutdown()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());
    }

    // shutdown via `st::~worker()`
    {
        // prove hdl destructor was called
        bool thread_running = true;
        std::shared_ptr<st::worker> wkr = st::worker::make<hdl>(&thread_running);

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

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());

        wkr->restart();

        EXPECT_TRUE(thread_running);
        EXPECT_TRUE(wkr->running());

        wkr->shutdown();

        EXPECT_FALSE(thread_running);
        EXPECT_FALSE(wkr->running());
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

// show service lifecycle behavior generally works
TEST(simple_thread, service_lifecycle) {
    struct hdl {
        static bool& thread_running() {
            static bool running = false;
            return running;
        }

        hdl() { hdl::thread_running() = true; }
        ~hdl() { hdl::thread_running()  = false; }

        inline void operator()(std::shared_ptr<st::message> msg) { }
    };

    // ensure service exists by calling an instance of it 
    st::service<hdl>();

    // shutdown & restart via worker API
    EXPECT_TRUE(hdl::thread_running());
    EXPECT_TRUE(st::service<hdl>().running());

    st::service<hdl>().shutdown();

    EXPECT_FALSE(hdl::thread_running());
    EXPECT_FALSE(st::service<hdl>().running());

    st::service<hdl>().restart();

    EXPECT_TRUE(hdl::thread_running());
    EXPECT_TRUE(st::service<hdl>().running());

    // shutdown & restart via global service API
    st::shutdown_all_services();

    EXPECT_FALSE(hdl::thread_running());
    EXPECT_FALSE(st::service<hdl>().running());
    
    st::restart_all_services();

    EXPECT_TRUE(hdl::thread_running());
    EXPECT_TRUE(st::service<hdl>().running());
   
    // end excess threads for test sanity reasons
    st::shutdown_all_services();
}

// showcase simple service thread launching and message handling
TEST(simple_thread, service_messaging) {
    // short for "handler"
    struct hdl {
        enum op {
            print_int,
            print_string,
            unknown
        };

        // used for worker behavior confirmation purposes
        static st::channel& ret_ch() {
            static auto c = st::channel::make();
            return *c;
        }

        inline void operator()(std::shared_ptr<st::message> msg) {
            switch(msg->id()) {
                case op::print_int:
                {
                    int i;

                    if(msg->copy_data_to<int>(i)) {
                        std::cout << "int: " << i << std::endl;
                        hdl::ret_ch().send(op::print_int);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        hdl::ret_ch().send(op::unknown);
                    }
                    break;
                }
                case op::print_string:
                {
                    std::string s;

                    if(msg->copy_data_to<std::string>(s)) {
                        std::cout << "string: " << s << std::endl;
                        hdl::ret_ch().send(op::print_string);
                    } else {
                        std::cout << "unknown" << std::endl;;
                        hdl::ret_ch().send(op::unknown);
                    }
                    break;
                }
                default:
                    std::cout << "unknown" << std::endl;;
                    hdl::ret_ch().send(op::unknown);
                    break;
            }
        }
    };

    struct garbazoo { }; // random type unknown to worker
        
    st::service<hdl>();
    std::shared_ptr<st::message> msg;
    int i = 3;
    std::string s = "hello";
    garbazoo g;

    // print_int
    EXPECT_TRUE(st::service<hdl>().send(hdl::op::print_int, i));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::print_int);

    {
        // ensure `st::worker::send(std::shared_ptr<st::message>)` works
        msg = st::message::make(hdl::op::print_int, i);
        EXPECT_TRUE(st::service<hdl>().send(msg));
        EXPECT_TRUE(hdl::ret_ch().recv(msg));
        EXPECT_EQ(msg->id(), hdl::op::print_int);
    }

    EXPECT_TRUE(st::service<hdl>().send(hdl::op::print_int, s));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    EXPECT_TRUE(st::service<hdl>().send(hdl::op::print_int, g));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    // print_string
    EXPECT_TRUE(st::service<hdl>().send(hdl::op::print_string, s));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::print_string);

    {
        // ensure `st::worker::send(std::shared_ptr<st::message>)` works
        msg = st::message::make(hdl::op::print_string, s);
        EXPECT_TRUE(st::service<hdl>().send(hdl::op::print_string, s));
        EXPECT_TRUE(hdl::ret_ch().recv(msg));
        EXPECT_EQ(msg->id(), hdl::op::print_string);
    }

    EXPECT_TRUE(st::service<hdl>().send(hdl::op::print_string, i));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    EXPECT_TRUE(st::service<hdl>().send(hdl::op::print_string, g));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    // print_unknown
    EXPECT_TRUE(st::service<hdl>().send(hdl::op::unknown, i));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    {
        // ensure `st::worker::send(std::size_t)` works
        EXPECT_TRUE(st::service<hdl>().send(hdl::op::unknown));
        EXPECT_TRUE(hdl::ret_ch().recv(msg));
        EXPECT_EQ(msg->id(), hdl::op::unknown);
    }

    EXPECT_TRUE(st::service<hdl>().send(hdl::op::unknown, s));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);

    EXPECT_TRUE(st::service<hdl>().send(hdl::op::unknown, g));
    EXPECT_TRUE(hdl::ret_ch().recv(msg));
    EXPECT_EQ(msg->id(), hdl::op::unknown);
   
    // end excess threads for test sanity reasons
    st::shutdown_all_services();
}
