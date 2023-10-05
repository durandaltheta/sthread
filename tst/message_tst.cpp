#include <gtest/gtest.h>
#include <string>
#include "sthread"
#include "simple_thread_test_utils.hpp"

namespace stt {
namespace message {
    enum op {
        unknown,
        integer,
        cstring,
        string
    };
}
}

// st::message is the only st::shared_context not relying on a descendent of 
// st::context so specific API is tested here
TEST(simple_thread, message_default) {
    st::message msg;
    EXPECT_FALSE(msg);

    msg = st::message::make();
    EXPECT_EQ(0, msg.id());
    EXPECT_TRUE(msg);

    st::message msg2(msg);
    EXPECT_TRUE(msg2);
    EXPECT_EQ(msg2, msg);
    EXPECT_EQ(0, msg.id());
    EXPECT_EQ(typeid(st::data::unset), msg.data().type_info());
}
    
TEST(simple_thread, message_forward) {
    st::message msg(st::message::make());
    st::message msg2(st::message::make(msg));
    EXPECT_TRUE(msg2);
    EXPECT_EQ(msg2, msg);
    EXPECT_EQ(0, msg.id());
    EXPECT_EQ(typeid(st::data::unset), msg.data().type_info());
}

TEST(simple_thread, message_int) {
    int i = 14;
    st::message msg = st::message::make(stt::message::op::integer,i);

    EXPECT_EQ(msg.id(), stt::message::op::integer);
    EXPECT_NE(msg.id(), stt::message::op::string);
    EXPECT_EQ(msg.data().type_info(), typeid(int));
    EXPECT_NE(msg.data().type_info(), typeid(std::string));
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

TEST(simple_thread, message_forward_data) {
    int i = 14;
    st::message msg = st::message::make(stt::message::op::integer,st::data::make<int>(i));

    EXPECT_EQ(msg.id(), stt::message::op::integer);
    EXPECT_NE(msg.id(), stt::message::op::string);
    EXPECT_EQ(msg.data().type_info(), typeid(int));
    EXPECT_NE(msg.data().type_info(), typeid(std::string));
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
   
TEST(simple_thread, message_c_string) {
    const char* s = "codemonkey";
    st::message msg = st::message::make(stt::message::op::cstring,s);

    EXPECT_EQ(msg.id(), stt::message::op::cstring);
    EXPECT_NE(msg.id(), stt::message::op::integer);
    EXPECT_EQ(msg.data().type_info(), typeid(const char*));
    EXPECT_NE(msg.data().type_info(), typeid(int));
    EXPECT_TRUE(msg.data().is<const char*>());
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
        const char* s2 = "";
        EXPECT_TRUE(msg.data().copy_to(s2));
        EXPECT_EQ(s, s2);
    }

    {
        const char* s2 = "";
        EXPECT_TRUE(msg.data().move_to(s2));
        EXPECT_EQ(s, s2);
    }

    // 2nd successful move should prove that data was swapped with 1st move
    {
        const char* s2 = "";
        EXPECT_TRUE(msg.data().move_to(s2));
        EXPECT_NE(s, s2);
        EXPECT_EQ(s2, "");
    }
}

TEST(simple_thread, message_std_string) {
    std::string s = "getupgetcoffee";
    st::message msg = st::message::make(stt::message::op::string,s);

    EXPECT_EQ(msg.id(), stt::message::op::string);
    EXPECT_NE(msg.id(), stt::message::op::integer);
    EXPECT_EQ(msg.data().type_info(), typeid(std::string));
    EXPECT_NE(msg.data().type_info(), typeid(int));
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
