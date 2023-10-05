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
namespace detail {

// typedef representing the unqualified type of T
template <typename T>
using base = typename std::decay<T>::type;

// template typing assistance object
template <typename T> struct hint { };

// get a function's return type via SFINAE
// handle pre and post c++17 
#if __cplusplus >= 201703L
template <typename F, typename... As>
using function_return_type = typename std::invoke_result<base<F>,As...>::type;
#else 
template <typename F, typename... As>
using function_return_type = typename std::result_of<base<F>(As...)>::type;
#endif

}
}

#endif
