#include <gtest/gtest.h>
#include <type_traits>
#include <iostream>
#include "sthread"
#include "simple_thread_test_utils.hpp"

namespace stt { // simple thread test  

template <typename SHARED_CONTEXT>
struct shared_context_test : public test_runner<SHARED_CONTEXT> {
    shared_context_test(
            const char* test_name, 
            std::function<SHARED_CONTEXT()> make) : 
        stt::test_runner<SHARED_CONTEXT>(test_name),
        m_make(make)
    { }

protected:
    virtual void test() {
        // construction and bool conversion
        SHARED_CONTEXT sctx; // default constructor
        SHARED_CONTEXT sctx2(m_make()); // rvalue constructor
        SHARED_CONTEXT sctx3(sctx2); // lvalue constructor

        EXPECT_FALSE(sctx); // bool conversion
        EXPECT_TRUE(sctx2);

        // equality and assignment
        sctx = sctx2; // lvalue assignment
        EXPECT_TRUE(sctx);
        EXPECT_TRUE(sctx == sctx2); // lvalue comparision
        EXPECT_TRUE(sctx == std::move(sctx2)); // rvalue comparision
        EXPECT_TRUE(sctx == sctx3); 

        EXPECT_TRUE(sctx.ctx()); // context getter is accessible
        sctx.ctx(std::shared_ptr<st::context>()); // context setter is accessible
        EXPECT_FALSE(sctx.ctx()); 

        sctx = m_make(); // rvalue assignment
        EXPECT_TRUE(sctx != sctx2); // lvalue compiler created not comparison
        EXPECT_TRUE(sctx != std::move(sctx2)); // rvalue compiler created not comparison
        EXPECT_TRUE(sctx != sctx3); 

        // comparison
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

TEST(simple_thread, shared_context) {
    const char* tst = "shared_context_test";
    stt::shared_context_test<st::message>(tst, []{ return st::message::make(); }).run();
    stt::shared_context_test<st::channel>(tst, []{ return st::channel::make();}).run();
    stt::shared_context_test<st::reply>(tst, []{ return st::reply::make( st::channel::make(), 0); }).run();
    stt::shared_context_test<st::thread>(tst, []{ return st::thread::make<>(); }).run();
}
