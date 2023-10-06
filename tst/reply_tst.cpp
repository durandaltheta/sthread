#include <gtest/gtest.h>
#include <string>
#include <iostream>
#include "sthread"
#include "simple_thread_test_utils.hpp"

TEST(simple_thread, reply_self) {
    st::message msg;
    auto ch = st::channel::make();
    auto reply = st::reply::make(ch, 1);
    reply.send(std::string("hello"));
    EXPECT_TRUE(ch.recv(msg));
    EXPECT_EQ(1, msg.id());
    EXPECT_TRUE(msg.data().is<std::string>());
    EXPECT_EQ(std::string("hello"), msg.data().cast_to<std::string>());
}

TEST(simple_thread, reply_from_thread) {
    auto ch = st::channel::make();
    auto ch2 = st::channel::make();
    ch.send(1, st::reply::make(ch2, 2));

    EXPECT_EQ(1, ch.queued());
    EXPECT_EQ(0, ch2.queued());
    
    std::thread thd([](st::channel ch) { 
        for(auto msg : ch) {
            EXPECT_EQ(1, msg.id());
            EXPECT_TRUE(msg.data().is<st::reply>());
            EXPECT_TRUE(msg.data().cast_to<st::reply>().send(std::string("world")));
        }
    },
    ch);

    st::message msg;
    EXPECT_TRUE(ch2.recv(msg));
    EXPECT_EQ(2, msg.id());
    EXPECT_TRUE(msg.data().is<std::string>());
    EXPECT_EQ(std::string("world"), msg.data().cast_to<std::string>());
    ch.close();
    thd.join();
}

TEST(simple_thread, reply_between_3_threads) {
    auto ch = st::channel::make();
    auto ch2 = st::channel::make();
    auto ch3 = st::channel::make();
    ch.send(1, st::reply::make(ch2, 2));
    
    std::thread thd([](st::channel ch) { 
        for(auto msg : ch) {
            EXPECT_EQ(1, msg.id());
            EXPECT_TRUE(msg.data().is<st::reply>());
            EXPECT_TRUE(msg.data().cast_to<st::reply>().send(std::string("foo")));
        }
    }, 
    ch);

    std::thread thd2([](st::channel ch, st::channel final_ch) { 
        for(auto msg : ch) {
            EXPECT_EQ(2, msg.id());
            EXPECT_TRUE(msg.data().is<std::string>());
            EXPECT_EQ(std::string("foo"), msg.data().cast_to<std::string>());
            final_ch.send(3, msg.data().cast_to<std::string>() + std::string("faa"));
        }
    },
    ch2,
    ch3);

    st::message msg;
    EXPECT_TRUE(ch3.recv(msg));
    EXPECT_EQ(3, msg.id());
    EXPECT_TRUE(msg.data().is<std::string>());
    EXPECT_EQ(std::string("foofaa"), msg.data().cast_to<std::string>());

    ch.close();
    ch2.close();
    thd.join();
    thd2.join();
}

