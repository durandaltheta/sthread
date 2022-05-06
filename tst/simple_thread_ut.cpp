#include <gtest/gtest.h>
#include "simple_thread.hpp"
#include <tuple>

/*
The purpose of this test is to showcase trivial message data payload type 
detection. IE, that the data type of a message's payload can be handled in 
constant type with a simple switch statement.
 */
TEST(simple_thread, switch_on_type) {
    bool thread_running = true;

    struct hdl {
        // convenience typing
        typedef std::tuple<int, std::string> intstring_t;
        typedef std::tuple<std::string, int> stringint_t;

        // in this test we only want 1 valid message id
        enum op { discern_type }; 

        hdl(std::shared_ptr<st::channel> ret_ch, bool* running_ptr) : 
            m_ret_ch(ret_ch),
            m_running_ptr(running_ptr)
        { }

        ~hdl() {
            *m_running_ptr = false;
        }

        /*
        inline void operator()(std::shared_ptr<st::message> msg) {
            switch(msg->id()) {
                case op::discern_type:
                    switch(msg->type()) {
                        case st::message::code<int>():
                            int i=0;
                            msg->copy_data_to(i);
                            std::cout << "int: " << i << std::endl;
                            break;
                        case st::message::code<std::string>():
                            std::string s;
                            msg->copy_data_to(s);
                            std::cout << "string: " << s << std::endl;
                            break;
                        case st::message::code<intstring_t>():
                            intstring_t is;
                            msg->copy_data_to(is);
                            std::cout << "int: " << std::get<0>() 
                                      << "string: " << std::get<1>() 
                                      << std::endl;
                            break;
                        case st::message::code<stringint_t>():
                            intstring_t is;
                            msg->copy_data_to(is);
                            std::cout << "int: " << std::get<0>() 
                                      << "string: " << std::get<1>() 
                                      << std::endl;
                            break;
                        default:
                            ADD_FAILURE();
                            break;
                    }
                    m_ret_ch->send(msg->type());
                    break;
                default:
                    ADD_FAILURE();
                    break;
            }
        }
        */

        inline void operator()(std::shared_ptr<st::message> msg) {
            switch(msg->id()) {
                case op::discern_type:
                {
                    st::select<>(msg->type())
                    .clause(
                        st::message::code<int>(),
                        [&]{
                            int i=0;
                            msg->copy_data_to(i);
                            std::cout << "int: " << i << std::endl;
                        })
                    .clause(
                        st::message::code<std::string>(),
                        [&]{
                            std::string s;
                            msg->copy_data_to(s);
                            std::cout << "string: " << s << std::endl;
                        })
                    .clause(
                        st::message::code<intstring_t>(),
                        [&]{
                            intstring_t is;
                            msg->copy_data_to(is);
                            std::cout << "int: " << std::get<0>(is) 
                                      << "string: " << std::get<1>(is) 
                                      << std::endl;
                        })
                    .clause(
                        st::message::code<stringint_t>(),
                        [&]{
                            stringint_t si;
                            msg->copy_data_to(si);
                            std::cout << "int: " << std::get<0>(si) 
                                      << "string: " << std::get<1>(si) 
                                      << std::endl;
                        })
                    .other(
                        []{ 
                            ADD_FAILURE(); 
                        }
                    );
                    
                    m_ret_ch->send(msg->type());
                    break;
                }
                default:
                    ADD_FAILURE();
                    break;
            }
        }

        std::shared_ptr<st::channel> m_ret_ch;
        bool* m_running_ptr;
    };

    std::shared_ptr<st::message> msg;
    std::shared_ptr<st::channel> ret_ch = st::channel::make();
    int i = 0;
    std::string s = "Hello, my baby";
    hdl::intstring_t is{1,"Hello, my honey"};
    hdl::stringint_t si{"Hello, my ragtime gal", 2};

    auto thd = st::worker::make<hdl>(ret_ch, &thread_running);

    thd->send(hdl::op::discern_type, is);
    ret_ch->recv(msg);
    EXPECT_EQ(msg->id(), st::message::code<hdl::intstring_t>());

    thd->send(hdl::op::discern_type, s);
    ret_ch->recv(msg);
    EXPECT_EQ(msg->id(), st::message::code<std::string>());

    thd->send(hdl::op::discern_type, i);
    ret_ch->recv(msg);
    EXPECT_EQ(msg->id(), st::message::code<int>());

    thd->send(hdl::op::discern_type, si);
    ret_ch->recv(msg);
    EXPECT_EQ(msg->id(), st::message::code<hdl::stringint_t>());

    ret_ch->close();
    thd->shutdown();
    EXPECT_FALSE(thread_running);
}
