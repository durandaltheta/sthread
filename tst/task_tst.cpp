#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include "sthread"
#include "simple_thread_test_utils.hpp" 

namespace stt {
namespace task {

int val=0;

void reset_val() { val = 0; }

void thunk() { ++val; }
int fint() { ++val; return val; }
std::string fstring() { ++val; return std::to_string(val); }

struct thunk_functor {
    void operator()() { ++val; }
};

struct fint_functor {
    int operator()() { ++val; return val; }
};

struct fstring_functor {
    std::string operator()() { ++val; return std::to_string(val); }
};

void msg_loop(st::channel ch) {
    for(auto msg : ch) {
        if(msg.data().is<st::task>()) {
            msg.data().cast_to<st::task>()();
        }
    }
}

}
}

TEST(simple_thread, task_default) {
    st::task t;
    EXPECT_FALSE(t);
    st::data& d = t();
    EXPECT_EQ(nullptr, d.get());
    EXPECT_FALSE(d);
    EXPECT_EQ(typeid(st::data::unset), t().type_info());
    EXPECT_FALSE(t());
    EXPECT_EQ(d.get(), t().get());
    EXPECT_EQ(nullptr, d.get());
    EXPECT_EQ(typeid(st::data::unset), t().type_info());
}

TEST(simple_thread, task_thunk) {
    stt::task::reset_val();
    auto t = st::task::make(stt::task::thunk);
    EXPECT_TRUE(t);
    void* v = t().get();
    void* v2 = v;
    void* v3 = v;
    EXPECT_FALSE(t());
    EXPECT_EQ(typeid(st::data::unset), t().type_info());
    EXPECT_EQ(1, stt::task::val);

    t = st::task::make(stt::task::thunk_functor());
    EXPECT_TRUE(t);
    v2 = t().get();
    EXPECT_EQ(v, v2);
    EXPECT_FALSE(t());
    EXPECT_EQ(typeid(st::data::unset), t().type_info());
    EXPECT_EQ(2, stt::task::val);

    t = st::task::make([&]{ ++stt::task::val; });
    EXPECT_TRUE(t);
    v3 = t().get();
    EXPECT_EQ(v, v3);
    EXPECT_FALSE(t());
    EXPECT_EQ(typeid(st::data::unset), t().type_info());
    EXPECT_EQ(3, stt::task::val);
}

TEST(simple_thread, task_ret_int) {
    stt::task::reset_val();
    auto t = st::task::make(stt::task::fint);
    EXPECT_TRUE(t);
    void* v = t().get();
    void* v2 = v;
    void* v3 = v;
    EXPECT_TRUE(t());
    EXPECT_TRUE(t().is<int>());
    EXPECT_EQ(1, t().cast_to<int>());
    EXPECT_EQ(1, stt::task::val);

    auto old_t = t;
    t = st::task::make(stt::task::fint_functor());
    EXPECT_TRUE(t);
    v2 = t().get();
    EXPECT_NE(v, v2);
    EXPECT_EQ(v2, t().get());
    EXPECT_TRUE(t());
    EXPECT_TRUE(t().is<int>());
    EXPECT_EQ(2, t().cast_to<int>());
    EXPECT_EQ(2, stt::task::val);

    auto old_t2 = t;
    t = st::task::make([&]{ ++stt::task::val; return stt::task::val; });
    EXPECT_TRUE(t);
    v3 = t().get();
    EXPECT_NE(v, v3);
    EXPECT_EQ(v3, t().get());
    EXPECT_TRUE(t());
    EXPECT_TRUE(t().is<int>());
    EXPECT_EQ(3, t().cast_to<int>());
    EXPECT_EQ(3, stt::task::val);
}

TEST(simple_thread, task_ret_string) {
    stt::task::reset_val();
    auto t = st::task::make(stt::task::fstring);
    EXPECT_TRUE(t);
    void* v = t().get();
    void* v2 = v;
    void* v3 = v;
    EXPECT_TRUE(t());
    EXPECT_TRUE(t().is<std::string>());
    EXPECT_EQ(std::string("1"), t().cast_to<std::string>());
    EXPECT_EQ(1, stt::task::val);

    auto old_t = t;
    t = st::task::make(stt::task::fstring_functor());
    EXPECT_TRUE(t);
    v2 = t().get();
    EXPECT_NE(v, v2);
    EXPECT_EQ(v2, t().get());
    EXPECT_TRUE(t());
    EXPECT_TRUE(t().is<std::string>());
    EXPECT_EQ(std::string("2"), t().cast_to<std::string>());
    EXPECT_EQ(2, stt::task::val);

    auto old_t2 = t;
    t = st::task::make([&]{ 
        ++stt::task::val; 
        return std::to_string(stt::task::val); 
    });
    EXPECT_TRUE(t);
    v3 = t().get();
    EXPECT_NE(v, v3);
    EXPECT_EQ(v3, t().get());
    EXPECT_TRUE(t());
    EXPECT_TRUE(t().is<std::string>());
    EXPECT_EQ(std::string("3"), t().cast_to<std::string>());
    EXPECT_EQ(3, stt::task::val);
}

TEST(simple_thread, executor) {
    stt::task::reset_val();
    EXPECT_EQ(0, stt::task::val);
    auto ch = st::channel::make();
    std::thread executor(stt::task::msg_loop, ch);

    ch.send(0, st::task::make(stt::task::fint));
    ch.send(0, st::task::make(stt::task::fint_functor()));
    ch.send(0, st::task::make([&]{ ++stt::task::val; return stt::task::val; }));

    ch.close();
    executor.join();
    EXPECT_EQ(3, stt::task::val);
}
