#ifndef __SIMPLE_THREAD_TEST_UTILS__
#define __SIMPLE_THREAD_TEST_UTILS__ 

#include <iostream>

namespace stt { // simple thread test 

template <typename CONTEXT_WRAPPER_T>
struct test_runner {
    test_runner(const char* test_name) : 
        m_test_name(test_name),
        m_type_name(typeid(CONTEXT_WRAPPER_T).name())
    { }

    inline void run() {
        std::cout << m_test_name << "<" << m_type_name << "> --- TEST BEGIN ---" << std::endl;
        test();
        std::cout << m_test_name << "<" << m_type_name << "> --- TEST END ---" << std::endl;
    }

    inline void log(const char* s) {
        std::cout << m_test_name << "<" << m_type_name << "> " << s << std::endl;
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
