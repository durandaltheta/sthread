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
    test_runner(const char* test_name) : 
        m_test_name(test_name),
        m_type_name(typeid(CONTEXT_WRAPPER_T).name())
    { }

    inline void run() {
        std::stringstream ss;
        ss << m_test_name << "<" << m_type_name << ">";
        std::string test_str = ss.str();

        stt::log(test_str.c_str(), "--- TEST BEGIN ---");
        test();
        stt::log(test_str.c_str(), "--- TEST END ---");
    }

    template <typename... As>
    inline void log(const char* s, As&&... as) {
        std::stringstream ss;
        ss << m_test_name << "<" << m_type_name << ">";
        std::string test_str = ss.str();
        stt::log(test_str.c_str(), s, std::forward<As>(as)...);
    }

protected:

    const char* m_test_name;
    const char* m_type_name;

    /*
     * @brief user test implementation
     */
    virtual void test() = 0;
};

}

#endif
