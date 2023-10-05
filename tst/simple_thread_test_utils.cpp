#include <mutex>
#include "simple_thread_test_utils.hpp"


std::unique_lock<std::mutex> stt::detail::log_lock() {
    static std::mutex mtx;
    return std::unique_lock<std::mutex>(mtx);
}
