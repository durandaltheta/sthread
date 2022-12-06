//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_UTILITY__
#define __SIMPLE_THREADING_UTILITY__

#include <memory>
#include <typeinfo>
#include <functional>
#include <mutex>
#include <iostream>

namespace st { // simple thread 

/**
 * @brief typedef representing the unqualified type of T
 */
template <typename T>
using base = typename std::decay<T>::type;

/**
 * The data type value is acquired by removing const and volatile 
 * qualifiers and then by acquiring the type_info::hash_type_code().
 *
 * @return an unsigned integer representing a data type.
 */
template <typename T>
static constexpr std::size_t type_code() {
    return typeid(base<T>).hash_code();
}

/**
 * The data type compiler name is acquired by removing const and volatile 
 * qualifiers and then by acquiring the type_info::name().
 *
 * @return an unsigned integer representing a data type.
 */
template <typename T>
static constexpr const char* type_name() {
    return typeid(base<T>).name();
}

namespace detail {

// @brief template typing assistance object
template <typename T> struct hint { };

//template assistance for unusable parent type
struct null_parent { };

// get a function's return type via SFINAE
// handle pre and post c++17 
#if __cplusplus >= 201703L
template <typename F, typename... As>
using function_return_type = typename std::invoke_result<base<F>,As...>::type;
#else 
template <typename F, typename... As>
using function_return_type = typename std::result_of<base<F>(As...)>::type;
#endif

/*
 * A utility struct which will store the current value of an argument reference,
 * and restore that value to said reference when this object goes out of scope.
 *
 * One advantage of using this over manual commands is that the destructor of 
 * this object will still trigger when an exception is raised.
 */
template <typename T>
struct hold_and_restore {
    hold_and_restore() = delete; // cannot create empty value
    hold_and_restore(const hold_and_restore&) = delete; // cannot copy
    hold_and_restore(hold_and_restore&& rhs) = delete; // cannot move
    inline hold_and_restore(T& t) : m_ref(t), m_old(t) { }
    inline ~hold_and_restore() { m_ref = m_old; }
    
    T& m_ref;
    T m_old;
};

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

}

#endif
