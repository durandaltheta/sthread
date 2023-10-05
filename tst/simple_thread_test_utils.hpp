#ifndef __SIMPLE_THREAD_TEST_UTILS__
#define __SIMPLE_THREAD_TEST_UTILS__ 

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include "sthread"

namespace stt { // simple thread test  
namespace detail {

std::unique_lock<std::mutex> log_lock();

inline void log() {
    std::cout << std::endl << std::flush;
}

template <typename T, typename... As>
inline void log(T&& t, As&&... as) {
    std::cout << t;
    log(std::forward<As>(as)...);
}

}

template <typename... As>
inline void log(const char* func, As&&... as) {
    auto lk = detail::log_lock();
    detail::log("[", func, "] ", std::forward<As>(as)...);
}


template <typename CONTEXT_WRAPPER_T>
struct test_runner {
    test_runner() { }

    inline void run() {
        std::stringstream ss;
        std::string test_str = ss.str();
        test();
    }

protected:
    /*
     * @brief user test implementation
     */
    virtual void test() = 0;
};

}

#endif
