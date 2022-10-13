//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_SCHEDULER__
#define __SIMPLE_THREADING_SCHEDULER__

#include "sender_context.hpp"

namespace st { // simple thread

/**
 * @brief parent of `st::context`s which can schedule code for execution
 */
struct scheduler_context : public sender_context {
    virtual ~scheduler_context() { }

    /**
     * @brief schedule a generic task for execution 
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being terminated
     */
    virtual bool schedule(std::function<void()> f) = 0;
};

/**
 * @brief interface for objects which have shared `st::scheduler_context`s and 
 * are therefore capable of scheduling arbitrary code for execution.
 *
 * CRTP: Curiously Recurring Template Pattern
 */
template <typename CRTP>
struct shared_scheduler_context : public shared_sender_context<CRTP> {
    virtual ~shared_scheduler_context() { }

    /**
     * @brief conversion operator to context shared pointer
     * @return shared pointer to `st::scheduler_context`
     */
    inline operator std::shared_ptr<st::scheduler_context>() const {
        return std::dynamic_pointer_cast<st::scheduler_context>(ctx());
    }

    /**
     * @brief conversion operator to context weak pointer
     * @return shared pointer to `st::scheduler_context`
     */
    inline operator std::weak_ptr<st::scheduler_context>() const {
        return std::dynamic_pointer_cast<st::scheduler_context>(ctx());
    }

    /**
     * @brief schedule a generic task for execution 
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being terminated
     */
    inline bool schedule(std::function<void()> f) {
        return this->ctx()->template cast<scheduler_context>().schedule(std::move(f));
    }

    /**
     * @brief wrap user function and arguments then schedule as a generic task for execution
     *
     * @param f function to execute on target sender 
     * @param a first argument for argument function
     * @param as optional remaining arguments for argument function
     * @return `true` on success, `false` on failure due to object being terminated
     */
    template <typename F, typename A, typename... As>
    bool schedule(F&& f, A&& a, As&&... as) {
        return schedule(std::function<void()>([=]() mutable { 
            f(std::forward<A>(a), std::forward<As>(as)...); 
        }));
    }
};

}

#endif
