//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CHANNEL__
#define __SIMPLE_THREADING_CHANNEL__

#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <utility>

#include "utility.hpp"
#include "context.hpp"

namespace st { // simple thread

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between system 
 * threads. 
 *
 * All methods in this object are threadsafe.
 */
struct channel : protected st::shared_context<channel,channel::context> {
    inline virtual ~channel() { }

    inline channel& operator=(const channel& rhs) {
        ctx() = rhs.ctx();
        return *this;
    }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        channel ch;
        ch.ctx(new context);
        return ch;
    }

    inline bool closed() const {
        return ctx() ? ctx()->closed() : true;
    }

    template <typename... As>
    void close(As&&... as) {
        if(ctx()) {
            ctx()->close(std::forward<As>(as)...);
        }
    }

    /**
     * @return count of system threads blocked on `recv()` for this `st::channel`
     */
    inline std::size_t blocked_receivers() const {
        return ctx() ? ctx()->blocked_receivers() : 0;
    }

    /** 
     * @brief operation result state
     *
     * This is typically handled internally, but is provided for the operations 
     * which directly return it.
     * 
     * closed == 0 to ensure that if the user executes `st::channel::try_recv()`
     * as a loop's condition, it will correctly break out of the loop when the 
     * channel is closed (although implicitly creates a high cpu usage busy-wait 
     * loop).
     */
    enum state {
        closed = 0,/// operation failed because channel was closed
        failure, /// non-blocking operation failed
        success /// operation succeeded
    };

    /**
     * @brief receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue. This will return early if 
     * `st::channel::close()` is called.
     *
     * A successful call to `recv()` will remove a message queued by `send()` 
     * from the internal channel message queue.
     *
     * Multiple simultaneous `recv()` calls will be served in the order they 
     * were called.
     *
     * Any message received that was sent via a call to 
     * `st::channel::schedule(...)` will be processed internally (executing the 
     * sent function and optional arguments) and not returned to the user. 
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return `true` on success, `false` if channel is closed
     */
    inline bool recv(st::message& msg) {
        return ctx() ? ctx()->recv(msg, true) == state::success : false;
    }
   

    /**
     * @brief do a non-blocking receive over the channel
     *
     * Behavior of this function is the same as `st::channel::recv()` except 
     * that it can fail early.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return the result state of the operation
     */
    inline state try_recv(st::message& msg) {
        return ctx() ? ctx()->recv(msg, false) : false;
    }
   
    /** 
     * @return count of currently unhandled messages sent to the `st::channel`'s queue
     */
    inline std::size_t queued() const {
        return ctx() ? ctx()->queued() : 0
    }

    /**
     * @brief send an `st::message` with given parameters
     *
     * @param as arguments passed to `st::message::make()`
     * @return `true` on success, `false` if sender_context is closed
     * */
    template <typename... As>
    bool send(As&&... as) {
        return ctx() ? ctx()->send(st::message::make(std::forward<As>(as)...)) : false;
    }

    /**
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation to this `st::channel`
     *
     * Internally calls `std::async` to asynchronously execute user function.
     *
     * If the user function returns no value, then `st::message::data()` will be 
     * unallocated. Otherwise, `st::message::data()` will contain the value 
     * returned by Callable `f`.
     *
     * @param resp_id id of message that will be sent back to the this `st::channel` when `std::async` completes 
     * @param f function to execute on another system thread
     * @param as optional arguments for `f`
     */
    template <typename F, typename... As>
    bool async(std::size_t resp_id, F&& f, As&&... as) {
        using isv = typename std::is_void<detail::function_return_type<F,As...>>;
        return async_impl(std::integral_constant<bool,isv::value>(),
                   resp_id,
                   std::forward<F>(f),
                   std::forward<As>(as)...);
    }
    
    /**
     * @brief schedule a generic task for execution 
     * 
     * The argument `f` will be executed when the message containing it is 
     * processed by a call to `st::channel::recv()`. The message will be handled 
     * internally upon receipt and not returned to the user for processing.
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being closed
     */
    inline bool schedule(std::function<void()> f) {
        return m_ch.send(0, st::channel::task(std::move(f)));
    }

    /**
     * @brief wrap user Callable and arguments then schedule as a generic task for execution
     *
     * The argument `f` will be executed with optional arguments `as` when the 
     * message containing it is processed by a call to `st::channel::recv()`. 
     * The message will be handled internally upon receipt and not returned to 
     * the user for processing.
     *
     * @param f function to execute on target sender 
     * @param a first argument for argument function
     * @param as optional remaining arguments for argument function
     * @return `true` on success, `false` on failure due to object being closed
     */
    template <typename F, typename A, typename... As>
    bool schedule(F&& f, A&& a, As&&... as) {
        return schedule(std::function<void()>([=]() mutable { 
            f(std::forward<A>(a), std::forward<As>(as)...); 
        }));
    }

private:
    template <typename F, typename... As>
    bool async_impl(std::true_type, std::size_t resp_id, F&& f, As&&... as) {
        if(ctx()) {
            auto self = ctx()

            // launch a thread and schedule the call
            std::async([=]() mutable { // capture a copy of the shared send context
                 f(std::forward<As>(as)...);
                 self.send(resp_id);
            }); 

            return true;
        } else {
            return false;
        }
    }
    
    template <typename F, typename... As>
    bool async_impl(std::false_type, std::size_t resp_id, F&& f, As&&... as) {
        if(ctx()) {
            auto self = ctx()

            // launch a thread and schedule the call
            std::async([=]() mutable { // capture a copy of the shared send context
                 auto result = f(std::forward<As>(as)...);
                 self.send(resp_id, result);
            }); 

            return true;
        } else {
            return false;
        }
    }

    /*
     * generic function wrapper for executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type. Is 
     * a new definition instead of a typedef so that it can be distinguished by 
     * receiving code. Messages processed by `st::message::handle(...)` will 
     * automatically execute any `st::message::task`s passed to it that are 
     * stored in the `st::message::data()` payload instead of passing the
     * message to be processed by the user handler.
     */
    struct task : public std::function<void()> { 
        template <typename... As>
        task(As&&... as) : std::function<void()>(std::forward<As>(as)...) { }
    };

    // private class used to implement `recv()` behavior
    struct blocker {
        // stack variables
        struct data {
            data(st::message* m) : msg(m) { }

            inline void wait(std::unique_lock<std::mutex>& lk) {
                do {
                    cv.wait(lk);
                } while(!flag);
            }

            inline void signal() {
                if(!flag) {
                    flag = true;
                    cv.notify_one(); 
                }
            }

            inline void send(st::message& m) {
                *msg = std::move(m);
                signal();
            }

            bool flag = false;
            std::condition_variable cv;
            st::message* msg;
        };

        blocker(data* d) : m_data(d) { }
        ~blocker(){ m_data->signal(); }

        inline void send(st::message msg){ 
            m_data->send(msg); 
        }
    
        data* m_data;
    };

    struct context : public st::context {
        context() : m_closed(false) { }

        virtual ~context(){ 
            close(true); 
        }

        inline bool closed() const { 
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_closed;
        }
        
        void close(bool soft=true);
        
        inline std::size_t queued() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_msg_q.size();
        }

        inline std::size_t blocked_receivers() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_blockers.size();
        }

        void handle_queued_messages(std::unique_lock<std::mutex>& lk);
        bool send(st::message msg);
        state recv(st::message& msg, bool block);
        bool process(st::message& msg);

        bool m_closed;
        mutable std::mutex m_mtx;
        std::deque<st::message> m_msg_q;
        std::deque<std::shared_ptr<blocker>> m_blockers;
    };
};

}

#endif
