#include <gtest/gtest.h>
#include <string>
#include "sthread"

// st::message is the only st::shared_context not relying on a descendent of 
// st::context so specific API is tested here
TEST(simple_thread, message) {
    enum op {
        unknown,
        integer,
        cstring,
        string
    };

    // default message
    {
        st::message msg;
        EXPECT_FALSE(msg);

        msg = st::message::make();
        EXPECT_EQ(0, msg.id());
        EXPECT_TRUE(msg);

        st::message msg2(msg);
        EXPECT_TRUE(msg2);
        EXPECT_EQ(msg2, msg);
        EXPECT_EQ(0, msg.id());
        EXPECT_EQ(0, msg.data().type_code());
    }
    
    // forward message make
    {
        st::message msg(st::message::make());
        st::message msg2(st::message::make(msg));
        EXPECT_TRUE(msg2);
        EXPECT_EQ(msg2, msg);
        EXPECT_EQ(0, msg.id());
        EXPECT_EQ(0, msg.data().type_code());
    }

    // int message
    {
        int i = 14;
        st::message msg = st::message::make(op::integer,i);

        EXPECT_EQ(msg.id(), op::integer);
        EXPECT_NE(msg.id(), op::string);
        EXPECT_EQ(msg.data().type_code(), st::type_code<int>());
        EXPECT_NE(msg.data().type_code(), st::type_code<std::string>());
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

    // int message forward data
    {
        int i = 14;
        st::message msg = st::message::make(op::integer,st::data::make<int>(i));

        EXPECT_EQ(msg.id(), op::integer);
        EXPECT_NE(msg.id(), op::string);
        EXPECT_EQ(msg.data().type_code(), st::type_code<int>());
        EXPECT_NE(msg.data().type_code(), st::type_code<std::string>());
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
   
    // c string message
    {
        std::string s = "codemonkey";
        st::message msg = st::message::make(op::cstring,s);

        EXPECT_EQ(msg.id(), op::cstring);
        EXPECT_NE(msg.id(), op::integer);
        EXPECT_EQ(msg.data().type_code(), st::type_code<std::string>());
        EXPECT_NE(msg.data().type_code(), st::type_code<int>());
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

    // std::string message
    {
        std::string s = "getupgetcoffee";
        st::message msg = st::message::make(op::string,s);

        EXPECT_EQ(msg.id(), op::string);
        EXPECT_NE(msg.id(), op::integer);
        EXPECT_EQ(msg.data().type_code(), st::type_code<std::string>());
        EXPECT_NE(msg.data().type_code(), st::type_code<int>());
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
}
