#include <gtest/gtest.h>
#include <string>
#include <chrono>
#include <thread>
#include <utility>
#include "simple_thread_test_utils.hpp"
#include "sthread"

namespace stt { // simple thread test

enum op {
    Default,
    Integer,
    Cstring,
    String,
    Double
};

template <typename SHARED_SEND_CONTEXT>
struct shared_sender_context_test {
    shared_sender_context_test(
            std::function<SHARED_SEND_CONTEXT()> make, 
            std::function<bool()> expected_requeue_result,
            std::function<SHARED_SEND_CONTEXT()> listener_make)
    {
        const char* tst = "shared_sender_context_test";
        shared_sender_context_api_test<st::channel>().run(tst, make);
        shared_sender_context_listener_api_test<st::channel>().run(tst, listener_make);
    }

    struct shared_sender_context_api_test : protected stt::test_runner<SHARED_SEND_CONTEXT> {
    protected:
        void test(std::function<SHARED_SEND_CONTEXT()>& make) {
            // alive 
            SHARED_SEND_CONTEXT ssc = make();
            EXPECT_TRUE(ssc.alive());
            
            // terminate
            ssc.terminate(); 
            EXPECT_FALSE(ssc.alive());
            ssc = make();
            EXPECT_TRUE(ssc.alive());
            ssc.terminate(true); 
            EXPECT_FALSE(ssc.alive());
            ssc = make();
            EXPECT_TRUE(ssc.alive());
            ssc.terminate(false); 
            EXPECT_FALSE(ssc.alive());
            
            // queued (does this compile)
            ssc = make();
            EXPECT_EQ(0, ssc.queued());

            // send 
            EXPECT_TRUE(ssc.send());
            EXPECT_TRUE(ssc.send(op::Default));
            EXPECT_TRUE(ssc.send(op::Integer,1));
            EXPECT_TRUE(ssc.send(op::Cstring,"hello"));
            EXPECT_TRUE(ssc.send(op::String,std::string("world")));
            EXPECT_TRUE(ssc.send(op::Double,st::data::make<double>(3.6)));

            // async 
            EXPECT_TRUE(ssc.async(op::Integer,[]{ return 1; }));
            EXPECT_TRUE(ssc.async(op::String,[]{ return std::string("world"); }));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // timer 
            std::chrono::milliseconds ms(100);
            EXPECT_TRUE(ssc.timer(op::Default,ms));
            EXPECT_TRUE(ssc.timer(op::Default,std::chrono::seconds(1)));
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));

            ssc.terminate(); 
            EXPECT_FALSE(ssc.alive());
            EXPECT_FALSE(ssc.send());
        }
    };

    struct api_test_object {
        ~api_test_object() {
            EXPECT_EQ(10, m_msg_received_cnt);
        }

        void recv(st::message msg) {
            ++m_msg_received_cnt;
            
            switch(msg.id()) {
                case op::Default:
                    break;
                case op::Integer:
                    int i;
                    if(msg.data().copy_to(i)) {
                        EXPECT_EQ(1. msg.data().cast_to<int>());
                    } else {
                        EXPECT_TRUE(false); // error
                    }
                    break;
                case op::Cstring:
                    if(msg.data().is<const char*>()) {
                        EXPECT_EQ("hello". msg.data().cast_to<const char*>());
                    } else {
                        EXPECT_TRUE(false); // error
                    }
                    break;
                case op::String:
                    std::string s;
                    if(msg.data().copy_to(s)) {
                        EXPECT_EQ(std::string("world"). s);
                    } else {
                        EXPECT_TRUE(false); // error
                    }
                    break;
                case op::Double:
                    double d;
                    if(msg.data().copy_to(d) {
                        EXPECT_EQ((double)3.6, d);
                    } else {
                        EXPECT_TRUE(false); // error
                    }
                    break;
                default:
                    EXPECT_TRUE(false); // error
                    break;
            }
        }

        std::size_t m_msg_received_cnt=0;
    };

    template <typename SHARED_SEND_CONTEXT>
    struct shared_sender_context_listener_api_test : protected stt::test_runner<SHARED_SEND_CONTEXT> {
        shared_sender_context_listener_api_test(std::function<bool>& expected_requeue_result) :
            m_req_exp(expected_requeue_result)
        { }

    protected:
        void test(std::function<SHARED_SEND_CONTEXT()>& make) {
            ssc = make();
            EXPECT_EQ(m_req_exp(), ssc.requeue()); // test requeue 

            st::channel ch(st::channel::make());
            ssc.listener(ch); // test listener 
           
            struct block_data {
                std::mutex mtx;
                std::condition_variable cv;
                bool flag = false;

                void block() { 
                    std::unique_lock<std::mutex> lk(m_mtx);
                    while(!flag) {
                        cv.wait(lk);
                    }
                }

                void unblock() {
                    {
                        std::lock_guard<std::mutex> lk(m_mtx);
                        flag = true;
                    }
                    cv.notify_one();
                }
            };

            auto bd = std::make_shared<block_data>();

            for(int i=0; i<100; ++i) {
                ssc.send(0, std::function<void()>([=]{ bd->block(); }));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            EXPECT_TRUE(ch.queued() = 500);
            EXPECT_TRUE(ssc.queued() = 499); // first message receive is blocked

            bd->unblock();

            EXPECT_EQ(m_req_exp(), ssc.requeue()); // test requeue is the same
        }

    private: 
        std::function<bool()> m_req_exp;
    };

    struct listener_api_test_object {
        void recv(st::message msg) {
            if(msg.data().is<std::function<void()>>()) {
                // execute blocking function until test completes 
                msg.data().cast_to<std::function<void()>>()();
            } else {
                EXPECT_TRUE(false); // error
            }
        }
    };
};

} 

TEST(simple_thread, sender_context) {
    stt::shared_sender_context_test<st::channel>(
            []{ return st::channel::make(); },
            []{ return true; },
            []{ 
                auto ch = st::channel::make(); 
                ch.async([=]{ 
                    st::message msg;
                    ch.recv(msg); // pull a message from the queue for test consistency
                });
                return ch;
            });

    stt::shared_sender_context_test<st::thread>(
            []{ return st::thread::make<stt::api_test_object>(); },
            []{ return true; },
            []{ return st::thread::make<stt::listener_api_test_object>(); });

    {
        st::thread thd = st::thread::make<>(); // shared fiber parent thread
        stt::shared_sender_context_test<st::fiber>(
                [thd]{ return st::fiber::make<stt::api_test_object>(thd); },
                []{ return true; },
                [thd]{ return st::fiber::make<stt::listener_api_test_object>(thd); });
    }
}
