//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_SENDER__
#define __SIMPLE_THREADING_SENDER__

#include "utility.hpp"
#include "context.hpp"

namespace st { // simple thread

/**
 * @brief interface for objects that can have their execution terminated
 */
struct lifecycle {
    virtual ~lifecycle(){ }

    /**
     * @return `false` if object is unallocated or has been terminated, else `true`
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
};

/**
 * @brief parent of `st::context`s which can be sent messages
 */
struct sender_context : public context, public lifecycle {
    sender_context() = delete;
    sender_context(const context&) = delete;
    sender_context(context&&) = delete;

    template <typename PARENT, typename SELF>
    sender_context(context::type_info<PARENT,SELF> ti) : st::context(ti) { }

    virtual ~sender_context(){ 
        terminate();
    }

    /** 
     * @return count of messages in the `st::shared_sender_context's queue
     */
    virtual inline std::size_t queued() const = 0;

    /**
     * @brief send an `st::message` to the implementor 
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
template <CRTP>
struct shared_sender_context : public shared_context<CRTP>, public lifecycle {
    virtual ~shared_sender_context(){ }

    virtual inline bool alive() const {
        return context() && context()->cast<st::sender_context>().alive();
    }

    virtual inline void terminate(bool soft) {
        return context()->cast<st::sender_context>().terminate(soft);
    }
   
    /** 
     * @return count of messages sent to the `st::shared_sender_context's queue
     */
    inline std::size_t queued() const {
        return context()->cast<st::sender_context>().queued();
    }

    /**
     * @brief send an `st::message` with given parameters
     *
     * @param as arguments passed to `st::message::make()`
     * @return `true` on success, `false` if sender_context is terminated
     * */
    template <typename... As>
    bool send(As&&... as) {
        return context()->cast<st::sender_context>().send(
            st::message::make(std::forward<As>(as)...));
    }

    /**
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation to this `st::shared_sender_context<CRTP>`
     *
     * Internally calls `std::async` to asynchronously execute user function.
     * If function returns no value, then `st::message::data()` will be 
     * unallocated.
     *
     * @param resp_id id of message that will be sent back to the this `st::shared_sender_context<CRTP>` when `std::async` completes 
     * @param f function to execute on another system thread
     * @param as arguments for argument function
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
     * @brief register a weak pointer of a `st::sender_context` as a listener to this object 
     *
     * NOTE: `std::weak_ptr<T>` can be created directly from a `st::shared_ptr<T>`. 
     * IE, the user can pass an `std::shared_ptr<st::context>` to this function.
     *
     * @param snd a shared_ptr to an object implementing `st::sender_context` to send `st::message` back to 
     * @return `true` on success, `false` if sender_context is terminated
     */
    inline bool listener(std::weak_ptr<st::sender_context> snd) {
        return context()->cast<st::sender_context>().listener(std::move(snd));
    }
  
    /**
     * @brief register an `st::shared_sender_context` as a listener to this object 
     *
     * WARNING: An object should never register itself as a listener to itself,
     * (even implicitly) as this can create an infinite loop.
     *
     * @param snd an object implementing `st::shared_sender_context` to send `st::message` back to 
     * @return `true` on success, `false` if sender_context is terminated
     */
    template <typename RHS_CRTP>
    inline bool listener(shared_sender_context<RHS_CRTP>& snd) {
        return listener(snd.context()->cast<st::sender_context>());
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
}

}

#endif
