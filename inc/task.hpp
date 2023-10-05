//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_TASK__
#define __SIMPLE_THREADING_TASK__

#include <functional>

#include "utility.hpp"
#include "context.hpp"
#include "data.hpp"

namespace st {
namespace detail {
namespace task {

struct context {
    context() : evaluate([&]() -> data& { return m_result; }) { }

    template <typename Callable, typename... As>
    context(Callable&& cb, As&&... as) { 
        using isv = typename std::is_void<detail::function_return_type<Callable,As...>>;
        evaluate = create_function(
                std::integral_constant<bool,isv::value>(),
                std::forward<Callable>(cb),
                std::forward<As>(as)...);
    }

    // handle case where Callable returns void
    template <typename Callable, typename... As>
    std::function<data&()> 
    create_function(std::true_type, Callable&& cb, As&&... as) {
        return [=]() mutable -> data& {
            if(!(this->m_flag)) {
                this->m_flag = true;
                cb(std::forward<As>(as)...);
            }

            return this->m_result; // return empty data
        };
    }

    // handle case where Callable returns some value
    template <typename Callable, typename... As>
    std::function<data&()> 
    create_function(std::false_type, Callable&& cb, As&&... as) {
        return [=]() mutable -> data& {
            if(!(this->m_flag)) {
                this->m_flag = true;
                typedef detail::function_return_type<Callable,As...> R;
                this->m_result = data::make<R>(cb(std::forward<As>(as)...));
            }

            return this->m_result;
        };
    }


    bool m_flag = false;
    data m_result;
    std::function<data&()> evaluate;
};

}
}

/**
 * @brief wrap any Callable and optional arguments into an executable task 
 *
 * A `Callable` is any data or object which can be executed like a function:
 * - functions 
 * - function pointers 
 * - functors (ex: std::function)
 * - lambdas 
 *
 * `st:task` accepts no arguments and returns void. It is useful for
 * generically encapsulating operatins which should occur elsewhere.
 *
 * `st::task` objects when invoked will return a reference to an `st::data` 
 * value when invoked containing the returned value of the wrapped `Callable`. 
 * If wrapped `Callable` returns void, the resulting `st::data&` will be empty 
 * and `== false` when used in an `if` statement.
 *
 * `st::task` objects are 'lazy', in that once they have been evaluated once, 
 * further evaluations will immediately return the previously returned value 
 * with no further work.
 */
struct task : public st::shared_context<task, detail::task::context> {
    inline virtual ~task() { }

    /**
     * @brief wrap an arbitrary Callable and optional arguments in a task 
     *
     * @param as any Callable followed by optional arguments
     */
    template <typename... As>
    static task make(As&&... as) {
        task t;
        t.ctx(std::make_shared<detail::task::context>(std::forward<As>(as)...));
        return t;
    }

    inline data& operator()() {
        if(!ctx()) {
            ctx(std::make_shared<detail::task::context>());
        }

        return ctx()->evaluate();
    }
};

}

#endif
