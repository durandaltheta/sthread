#include <gtest/gtest.h>
#include <type_traits>
#include <iostream>
#include "sthread"
#include "simple_thread_test_utils.hpp"

namespace stt { // simple thread test  

template <typename SHARED_CONTEXT>
struct shared_context_test : public test_runner<SHARED_CONTEXT> {
    shared_context_test(std::function<SHARED_CONTEXT()> make) : m_make(make)
    { }

protected:
    virtual void test() {
        // construction and bool conversion
        SHARED_CONTEXT sctx; // default constructor
        SHARED_CONTEXT sctx2(m_make()); // rvalue constructor
        SHARED_CONTEXT sctx3(sctx2); // lvalue constructor

        // bool conversion
        EXPECT_FALSE(sctx); 
        EXPECT_TRUE(sctx2);

        // assignment
        sctx = sctx2; // lvalue assignment
        
        // equality 
        EXPECT_TRUE(sctx);
        EXPECT_TRUE(sctx == sctx2); // lvalue comparision
        EXPECT_TRUE(sctx == std::move(sctx2)); // rvalue comparision
        EXPECT_TRUE(sctx == sctx3); 

        // inequality
        sctx = m_make(); // rvalue assignment
        EXPECT_TRUE(sctx != sctx2); // lvalue compiler created not comparison
        EXPECT_TRUE(sctx != std::move(sctx2)); // rvalue compiler created not comparison
        EXPECT_TRUE(sctx != sctx3); 

        // greater and less
        SHARED_CONTEXT sctx4;
        EXPECT_TRUE(sctx4 < sctx); // less than comparison
        EXPECT_TRUE(sctx4 < sctx2);
        EXPECT_TRUE(sctx4 < sctx3);
        EXPECT_TRUE(sctx > sctx4); // greater than comparison
        EXPECT_TRUE(sctx >= sctx4); // greater than or equal comparison
        EXPECT_TRUE(sctx >= sctx2); // greather than or equal comparison 
    };

    std::function<SHARED_CONTEXT()> m_make;
};

}

TEST(simple_thread, shared_context_message) {
    stt::shared_context_test<st::message>([]{ return st::message::make(); }).run();
}

TEST(simple_thread, shared_context_channel) {
    stt::shared_context_test<st::channel>([]{ return st::channel::make();}).run();
}

TEST(simple_thread, shared_context_reply) {
    stt::shared_context_test<st::reply>(
            []{ return st::reply::make( st::channel::make(), 0); }).run();
}

TEST(simple_thread, shared_context_task) {
    stt::shared_context_test<st::task>([]{ return st::task::make([]{}); }).run();
}
