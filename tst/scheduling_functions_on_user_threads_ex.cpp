#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>
#include "sthread"

static int foo(int a) {
    std::cout << "foo: " << a << std::endl;
    return a + 1;
}

TEST(example, scheduling_functions_on_user_threads_1) {
    auto foo_task = st::task::make(foo, 3);
    if(foo_task().is<int>()) {
        // can safely invoke task again because it will immediately return its
        // previous result
        std::cout << "result: " << foo_task().cast_to<int>() << std::endl;
    }
}

static void print(const char* s) {
    std::cout << s << std::endl;
}

struct PrintFunctor { 
    void operator()(const char* s) {
        std::cout << s << std::endl;
    }
};

static void executor(st::channel ch) {
    for(auto msg : ch) { 
        // execute any received tasks
        if(msg.data().is<st::task>()) {
            msg.data().cast_to<st::task>()();
        }
    } 
}

TEST(example, scheduling_functions_on_user_threads_2) {
    auto printer_lambda = []{ std::cout << "what a beautiful sunset" << std::endl; };
    auto ch = st::channel::make();
    std::thread thd(executor, ch); 

    // in this example, message id's are arbitrary
    ch.send(0, st::task::make(print, "what a beautiful day"));
    ch.send(0, st::task::make(PrintFunctor(), "looks like rain"));
    ch.send(0, st::task::make(printer_lambda));

    ch.close();
    thd.join();
}
