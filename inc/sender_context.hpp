//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_SENDER__
#define __SIMPLE_THREADING_SENDER__

#include <type_traits>
#include <thread>
#include <future>

#include "utility.hpp"
#include "context.hpp"
#include "message.hpp"

namespace st { // simple thread 

/**
 * @brief parent of `st::context`s which can be sent messages
 *
 * Adds the ability to manage the context's lifecycle 
 */
struct sender_context : public context {
    virtual ~sender_context(){ terminate(); }

    /**
     * @return `false` if object has been terminated, else `true`
     */
    virtual bool alive() const = 0;

    /**
     * @brief end operations on the object 
     * @param soft `true` to allow object to process remaining operations, else `false`
     */
    virtual void terminate(bool soft) = 0;

    /**
     * @brief default behavior for ending operations on the object
     */
    virtual inline void terminate() {
        terminate(true);
    }

    /** 
     * @return count of messages in the `st::shared_sender_context's queue
     */
    virtual std::size_t queued() const = 0;
    
    /**
     * @brief directly send an `st::message` to the implementor 
     * @param msg `st::message` to send to the implementor
     * @return `true` on success, `false` if sender_context is terminated
     */
    virtual bool send(st::message msg) = 0;

    /**
     * @brief register a weak pointer to an `st::sender_context` as a listener to `st::message`s sent over this `st::sender_context`
     * @param snd any object implementing `st::sender_context` to send `st::message` back to 
     * @return `true` on success, `false` if sender_context is terminated
     */
    virtual bool listener(std::weak_ptr<st::sender_context> snd) = 0;
    
    /**
     * @return `true` if listener should be requeued to continue listening after successfully sending an `st::message`, else `false`
     */
    virtual inline bool requeue() const {
        return true;
    }
};

/**
 * @brief interface for objects which have shared `st::sender_context`s
 *
 * CRTP: Curiously Recurring Template Pattern
 */
template <typename CRTP>
struct shared_sender_context : public shared_context<CRTP> {
    virtual ~shared_sender_context(){ }

    /**
     * @brief conversion operator to context shared pointer
     * @return shared pointer to `st::sender_context`
     */
    inline operator std::shared_ptr<st::sender_context>() const {
        return std::dynamic_pointer_cast<st::sender_context>(this->ctx());
    }

    /**
     * @brief conversion operator to context weak pointer
     * @return shared pointer to `st::sender_context`
     */
    inline operator std::weak_ptr<st::sender_context>() const {
        return std::dynamic_pointer_cast<st::sender_context>(this->ctx());
    }

    inline bool alive() const {
        return this->ctx()->template cast<st::sender_context>().alive();
    }

    template <typename... As>
    inline void terminate(As&&... as) {
        return this->ctx()->template cast<st::sender_context>().terminate(std::forward<As>(as)...);
    }
   
    /** 
     * @return count of currently unhandled messages sent to the `st::shared_sender_context's queue
     */
    inline std::size_t queued() const {
        return this->ctx()->template cast<st::sender_context>().queued();
    }

    /**
     * @brief send an `st::message` with given parameters
     *
     * @param as arguments passed to `st::message::make()`
     * @return `true` on success, `false` if sender_context is terminated
     * */
    template <typename... As>
    bool send(As&&... as) {
        return this->ctx()->template cast<st::sender_context>().send(
            st::message::make(std::forward<As>(as)...));
    }

    /**
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation to this `st::shared_sender_context<CRTP>`
     *
     * Internally calls `std::async` to asynchronously execute user function.
     * If function returns no value, then `st::message::data()` will be 
     * unallocated. Otherwise, `st::message::data()` will contain the value 
     * returned by Callable `f`.
     *
     * @param resp_id id of message that will be sent back to the this `st::shared_sender_context<CRTP>` when `std::async` completes 
     * @param f function to execute on another system thread
     * @param as optional arguments for `f`
     */
    template <typename F, typename... As>
    void async(std::size_t resp_id, F&& f, As&&... as) {
        using isv = typename std::is_void<detail::function_return_type<F,As...>>;
        async_impl(std::integral_constant<bool,isv::value>(),
                   resp_id,
                   std::forward<F>(f),
                   std::forward<As>(as)...);
    }

    /**
     * @brief register an object as a listener to this object, to be forwarded `st::message`s `sent(...)` to it 
     *
     * Multiple listeners will be forwarded messages in the order registered.
     *
     * NOTE: implementors of `st::shared_sender_context` can be trivially 
     * converted to `std::shared_ptr<st::sender_context>` or
     * `std::weak_ptr<st::sender_context>`, meaning said objects can be passed 
     * directly to this function. For example, the following is valid code given 
     * variables `st::channel chan` and `st::thread thd`:
     *
     * // automatically converts `thd` to `std::weak_ptr<st::sender_context>`
     * chan.listener(thd); 
     *
     * @param snd a weak_ptr to an object implementing `st::sender_context` to send `st::message` back to 
     * @return `true` on success, `false` if sender_context is terminated
     */
    inline bool listener(std::weak_ptr<st::sender_context> snd) {
        return this->ctx()->template cast<st::sender_context>().listener(std::move(snd));
    }

private:
    template <typename F, typename... As>
    void async_impl(std::true_type, std::size_t resp_id, F&& f, As&&... as) {
        shared_sender_context<CRTP> self = *this;

        // launch a thread and schedule the call
        std::async([=]() mutable { // capture a copy of the shared send context
             f(std::forward<As>(as)...);
             self.send(resp_id);
        }); 
    }
    
    template <typename F, typename... As>
    void async_impl(std::false_type, std::size_t resp_id, F&& f, As&&... as) {
        shared_sender_context<CRTP> self = *this;

        // launch a thread and schedule the call
        std::async([=]() mutable { // capture a copy of the shared send context
             auto result = f(std::forward<As>(as)...);
             self.send(resp_id, result);
        }); 
    }
};

}

#endif
