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
struct shared_sender_context_test {
    shared_sender_context_test(
            std::function<SHARED_SEND_CONTEXT()> make, 
            std::function<bool()> expected_requeue_result,
            std::function<SHARED_SEND_CONTEXT()> listener_make)
    {
        api_test_runner(m_test_name).run(make);
        listener_api_test_runner(m_test_name, expected_requeue_result).run(listener_make);
    }

    struct api_test_runner : public stt::test_runner<SHARED_SEND_CONTEXT> {
        api_test_runner(const char* test_name) : 
            stt::test_runner<SHARED_SEND_CONTEXT>(test_name) 
        { }
        
    protected:
        void test(std::function<SHARED_SEND_CONTEXT()>& make) {
            // alive 
            this->log("alive");
            SHARED_SEND_CONTEXT ssc = make();
            EXPECT_TRUE(ssc.alive());
            
            // terminate
            this->log("terminate");
            ssc.terminate(); 
            EXPECT_FALSE(ssc.alive());
            this->log("terminate2");
            ssc = make();
            this->log("terminate2.1");
            EXPECT_TRUE(ssc.alive());
            this->log("terminate3");
            ssc.terminate(true); 
            EXPECT_FALSE(ssc.alive());
            this->log("terminate4");
            ssc = make();
            EXPECT_TRUE(ssc.alive());
            this->log("terminate5");
            ssc.terminate(false); 
            EXPECT_FALSE(ssc.alive());
            
            // queued (does this compile)
            this->log("queued");
            ssc = make();
            EXPECT_EQ(0, ssc.queued());

            // send 
            this->log("send");
            EXPECT_TRUE(ssc.send());
            EXPECT_TRUE(ssc.send(op::Default));
            EXPECT_TRUE(ssc.send(op::Integer,1));
            EXPECT_TRUE(ssc.send(op::Cstring,"hello"));
            EXPECT_TRUE(ssc.send(op::String,std::string("world")));
            EXPECT_TRUE(ssc.send(op::Double,st::data::make<double>(3.6)));

            // async 
            this->log("async");
            ssc.async(op::Integer,[]{ return 1; }); // return int
            ssc.async(op::String,[]{ return std::string("world"); }); // return string
            ssc.async(op::Void,[]{}); // return void
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    };

    struct listener_api_test_runner : public stt::test_runner<SHARED_SEND_CONTEXT> {
        listener_api_test_runner(const char* test_name, std::function<bool()>& expected_requeue_result) :
            stt::test_runner<SHARED_SEND_CONTEXT>(test_name),
            m_req_exp(expected_requeue_result)
        { }

    protected:
        void test(std::function<SHARED_SEND_CONTEXT()>& make) {
            SHARED_SEND_CONTEXT ssc = make();

            // shared pointer sender context conversion
            std::shared_ptr<st::sender_context> sptr_sc(ssc); 
            EXPECT_EQ(m_req_exp(), sptr_sc->requeue()); // test requeue 

            st::channel ch(st::channel::make());
            ssc.listener(ch); // test listener 
           
            struct block_data {
                std::mutex mtx;
                std::condition_variable cv;
                bool flag = false;

                void block() { 
                    std::unique_lock<std::mutex> lk(mtx);
                    while(!flag) {
                        cv.wait(lk);
                    }
                }

                void unblock() {
                    {
                        std::lock_guard<std::mutex> lk(mtx);
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
            EXPECT_TRUE(ch.queued() == 500);
            EXPECT_TRUE(ssc.queued() == 499); // first message receive is blocked

            bd->unblock();

            EXPECT_EQ(m_req_exp(), sptr_sc->requeue()); // test requeue is the same
        }

    private: 
        std::function<bool()> m_req_exp;
    };
        
    const char* m_test_name = "shared_sender_context_test";
};

struct object {
    ~object() {
        EXPECT_EQ(9, m_msg_received_cnt);
    }

    void recv(st::message msg) {
        ++m_msg_received_cnt;
        
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

    std::size_t m_msg_received_cnt=0;
};

struct listener_object {
    void recv(st::message msg) {
        if(msg.data().is<std::function<void()>>()) {
            // execute blocking function until test completes 
            msg.data().cast_to<std::function<void()>>()();
        } else {
            EXPECT_TRUE(false); // error
        }
    }
};

} 

TEST(simple_thread, sender_context) {
    stt::shared_sender_context_test<st::channel>(
            []{ 
                std::cout << "channel make1" << std::endl;
                auto ch = st::channel::make(); 
                std::cout << "channel make2" << std::endl;
                return ch;
            },
            []{ return true; },
            []{ 
                auto ch = st::channel::make(); 
                ch.async(0, [=]() mutable { 
                    st::message msg;
                    ch.recv(msg); // pull a message from the queue for test consistency
                });
                return ch;
            });

    stt::shared_sender_context_test<st::thread>(
            []{ return st::thread::make<stt::object>(); },
            []{ return true; },
            []{ return st::thread::make<stt::listener_object>(); });
}
