#include <gtest/gtest.h>
#include <string>
#include "sthread"
#include "simple_thread_test_utils.hpp"

TEST(simple_thread, data_default) {
    st::data d;
    EXPECT_FALSE(d);
    EXPECT_FALSE(d.is<int>());
    EXPECT_FALSE(d.is<const char*>());
    EXPECT_FALSE(d.is<std::string>());
    EXPECT_EQ(typeid(st::data::unset), d.type_info());
}

TEST(simple_thread, data_int) {
    int i=14;
    st::data d = st::data::make<int>(i);

    EXPECT_EQ(d.type_info(), typeid(int));
    EXPECT_NE(d.type_info(), typeid(std::string));
    EXPECT_TRUE(d.is<int>());
    EXPECT_FALSE(d.is<std::string>());

    {
        std::string s = "";
        EXPECT_FALSE(d.copy_to(s));
    }

    {
        std::string s = "";
        EXPECT_FALSE(d.move_to(s));
    }

    {
        int i2 = 0;
        EXPECT_TRUE(d.copy_to(i2));
        EXPECT_EQ(i, i2);
    }

    {
        int i2 = 0;
        EXPECT_TRUE(d.move_to(i2));
        EXPECT_EQ(i, i2);
    }

    // 2nd successful move should prove that data was swapped with 1st move
    {
        int i2 = 0;
        EXPECT_TRUE(d.move_to(i2));
        EXPECT_NE(i, i2);
        EXPECT_EQ(i2, 0);
    }
}
    
TEST(simple_thread, data_forward_int) {
    int i=14;
    st::data d(st::data::make<int>(i));

    EXPECT_EQ(d.type_info(), typeid(int));
    EXPECT_NE(d.type_info(), typeid(std::string));
    EXPECT_TRUE(d.is<int>());
    EXPECT_FALSE(d.is<std::string>());

    {
        std::string s = "";
        EXPECT_FALSE(d.copy_to(s));
    }

    {
        std::string s = "";
        EXPECT_FALSE(d.move_to(s));
    }

    {
        int i2 = 0;
        EXPECT_TRUE(d.copy_to(i2));
        EXPECT_EQ(i, i2);
    }

    {
        int i2 = 0;
        EXPECT_TRUE(d.move_to(i2));
        EXPECT_EQ(i, i2);
    }

    // 2nd successful move should prove that data was swapped with 1st move
    {
        int i2 = 0;
        EXPECT_TRUE(d.move_to(i2));
        EXPECT_NE(i, i2);
        EXPECT_EQ(i2, 0);
    }
}
   
TEST(simple_thread, data_c_string) {
    const char* s = "codemonkey";
    st::data d = st::data::make<const char*>(s);

    EXPECT_EQ(d.type_info(), typeid(const char*));
    EXPECT_NE(d.type_info(), typeid(int));
    EXPECT_TRUE(d.is<const char*>());
    EXPECT_FALSE(d.is<int>());

    {
        int i = 0;
        EXPECT_FALSE(d.copy_to(i));
    }

    {
        int i = 0;
        EXPECT_FALSE(d.move_to(i));
    }

    {
        const char* s2 = "";
        EXPECT_TRUE(d.copy_to(s2));
        EXPECT_EQ(s, s2);
    }

    {
        const char* s2 = "";
        EXPECT_TRUE(d.move_to(s2));
        EXPECT_EQ(s, s2);
    }

    // 2nd successful move should prove that data was swapped with 1st move
    {
        const char* s2 = "";
        EXPECT_TRUE(d.move_to(s2));
        EXPECT_NE(s, s2);
        EXPECT_EQ(s2, "");
    }
}

TEST(simple_thread, data_std_string) {
    std::string s = "getupgetcoffee";
    st::data d = st::data::make<std::string>(s);

    EXPECT_EQ(d.type_info(), typeid(std::string));
    EXPECT_NE(d.type_info(), typeid(int));
    EXPECT_TRUE(d.is<std::string>());
    EXPECT_FALSE(d.is<int>());

    {
        int i = 0;
        EXPECT_FALSE(d.copy_to(i));
    }

    {
        int i = 0;
        EXPECT_FALSE(d.move_to(i));
    }

    {
        std::string s2 = "";
        EXPECT_TRUE(d.copy_to(s2));
        EXPECT_EQ(s, s2);
    }

    {
        std::string s2 = "";
        EXPECT_TRUE(d.move_to(s2));
        EXPECT_EQ(s, s2);
    }

    // 2nd successful move should prove that data was swapped with 1st move
    {
        std::string s2 = "";
        EXPECT_TRUE(d.move_to(s2));
        EXPECT_NE(s, s2);
        EXPECT_EQ(s2, "");
    }
}
