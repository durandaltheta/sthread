//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CHANNEL__
#define __SIMPLE_THREADING_CHANNEL__

#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <utility>
#include <thread>
#include <future>
#include <chrono>

#include "utility.hpp"
#include "context.hpp"
#include "message.hpp"

namespace st { // simple thread  

/** 
 * @brief operation result state
 *
 * This is typically handled internally, but is provided for the operations 
 * which directly return it.
 * 
 * closed == 0 to ensure that if the user executes `st::channel::try_recv()`
 * as a loop's condition, it will correctly break out of the loop when the 
 * channel is closed (although this implicitly creates a high cpu usage 
 * busy-wait loop).
 */
enum state {
    closed = 0, /// operation failed because channel was closed
    failure, /// non-blocking operation failed
    success /// operation succeeded
};

namespace detail {
namespace channel {

// private class used to implement `recv()` blocking behavior
struct blocker {
    // stack condition data (not allocated!)
    struct data {
        data(st::message* m) : msg(m) { }

        inline void wait(std::unique_lock<std::mutex>& lk) {
            do {
                cv.wait(lk);
            } while(!flag);
        }

        inline void signal() {
            // only call signal once
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

    // a pointer to stack condition data 
    data* m_data;
};

struct context {
    context() : m_closed(false) { }

    virtual ~context(){ 
        close(true); 
    }

    inline bool closed() const { 
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_closed;
    }
    
    inline void close(bool soft=true) {
        std::unique_lock<std::mutex> lk(m_mtx);
        if(!m_closed) {
            m_closed = true;

            if(!soft) {
                m_msg_q.clear();
            }

            if(m_msg_q.empty() && m_blockers.size()) {
                m_blockers.clear(); // allow receivers to terminate
            }
        }
    }
    
    inline std::size_t queued() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_msg_q.size();
    }

    inline std::size_t blocked_receivers() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_blockers.size();
    }

    inline void handle_queued_messages(std::unique_lock<std::mutex>& lk) {
        st::message msg;

        while(m_msg_q.size() && m_blockers.size()) {
            std::shared_ptr<st::channel::detail::blocker> s = m_blockers.front();
            m_blockers.pop_front();
            msg = m_msg_q.front();
            m_msg_q.pop_front();

            lk.unlock();
            s->send(msg);
            lk.lock();
        }

        if(m_closed && m_blockers.size()) {
            m_blockers.clear(); // allow receivers to terminate
        }
    }

    inline bool send(st::message msg) {
        std::unique_lock<std::mutex> lk(m_mtx);

        if(m_closed) {
            return false;
        } else {
            m_msg_q.push_back(std::move(msg));
            handle_queued_messages(lk);
            return true;
        } 
    }

    inline state recv(st::message& msg, bool block) {
        std::unique_lock<std::mutex> lk(m_mtx);

        do {
            if(!m_msg_q.empty()) {
                // can safely loop here until message queue is empty or we can return 
                // a message, even when block == false
                while(!m_msg_q.empty()) {
                    // retrieve message immediately
                    msg = std::move(m_msg_q.front());
                    m_msg_q.pop_front();

                    if(msg) {
                        return st::channel::success;
                    }
                }
            } else if(m_closed) {
                return st::channel::state::closed;
            } else if(block) {
                // block until message is available or channel termination
                while(!msg && !m_closed) { 
                    st::channel::detail::blocker::data d(&msg);
                    m_blockers.push_back(
                            std::shared_ptr<st::channel::detail::blocker>(
                                new st::channel::detail::blocker(&d)));
                    d.wait(lk);
                }

                if(msg) {
                    return st::channel::state::success;
                }
            } 
        } while(block); // on blocking receive, loop till we receive a non-task message or channel is closed

        // since we didn't early return out the receive loop, then this is a failed try_recv()
        return st::channel::state::failure;
    }

    bool m_closed;
    mutable std::mutex m_mtx;
    std::deque<st::message> m_msg_q;
    std::deque<std::shared_ptr<blocker>> m_blockers;
};

}
}

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between system 
 * threads. 
 *
 * All methods in this object are threadsafe.
 */
struct channel : protected st::shared_context<channel,detail::channel::context> {
    inline virtual ~channel() { }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        channel ch;
        ch.ctx(std::make_shared<detail::channel::context>());
        return ch;
    }

    /**
     * @return `true` if the `st::channel::close()` has been called or if `st::channel::make()` was never called, else `false`
     */
    inline bool closed() const {
        return ctx() ? ctx()->closed() : true;
    }

    /**
     * @brief `st::channel` is set to the closed state
     * @param soft if `false` clear all previously sent messages from the internal message queue, otherwise leave previously sent messages to be received with `recv()`
     */
    inline void close(bool soft=true) {
        if(ctx()) {
            ctx()->close(soft);
        }
    }

    /**
     * @return count of system threads blocked on `recv()` for this `st::channel`
     */
    inline std::size_t blocked_receivers() const {
        return ctx() ? ctx()->blocked_receivers() : 0;
    }

    /**
     * @brief receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * message available in the message queue, after which the argument message 
     * reference will be overwritten by the front of the queue. The front of the 
     * queue will then be popped. 
     *
     * This will return early if `st::channel::close(false)` is called. If
     * `st::channel::close(true)` or `st::channel::close()` is called instead, 
     * then calls to this function will succeed until the internal message queue 
     * is empty.
     *
     * Multiple simultaneous `recv()` calls will be served in the order they 
     * were called.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return `true` on success, `false` if channel is closed
     */
    inline bool recv(st::message& msg) {
        return ctx() ? ctx()->recv(msg, true) == state::success : false;
    }

    /**
     * @brief do a non-blocking message receive over the channel
     *
     * Behavior of this function is the same as `st::channel::recv()` except 
     * that it can fail early and returns an enumeration instead of a boolean.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return the result state of the operation
     */
    inline state try_recv(st::message& msg) {
        return ctx() ? ctx()->recv(msg, false) : state::closed;
    }
   
    /** 
     * @return count of currently unhandled messages sent to the `st::channel`'s queue
     */
    inline std::size_t queued() const {
        return ctx() ? ctx()->queued() : 0;
    }

    /**
     * @brief send an `st::message` with given parameters into the internal message queue
     *
     * This method is non-blocking.
     *
     * This method will immediately return if the channel is closed.
     *
     * @param as arguments passed to `st::message::make()`
     * @return `true` on success, `false` if the `st::channel` is closed
     * */
    template <typename... As>
    bool send(As&&... as) {
        return ctx() ? ctx()->send(st::message::make(std::forward<As>(as)...)) : false;
    }
    
    //--------------------------------------------------------------------------
    // Iteration 
   
    /**
     * @brief implementation of an input iterator for `st::channel`
     *
     * This implementation allows trivial usage of channels in `for` loops:
     * for(auto& msg : ch) { 
     *     // handle message
     * }
     */
    class iterator : 
        public st::shared_context<iterator, detail::channel::context>, 
        public std::input_iterator_tag 
    {
        inline void increment() {
            bool ret = ctx() ? ctx()->recv(msg, true) == state::success : false;

            if(!ret) {
                ctx().reset();
            }
        }

        st::message msg;

    public:
        iterator() { }

        iterator(std::shared_ptr<detail::channel::context> rhs) { 
            ctx(rhs);
        }
        
        inline virtual ~iterator() { }

        inline iterator& operator=(const iterator& rhs) {
            ctx() = rhs.ctx();
            msg = rhs.msg;
            return *this;
        }

        inline st::message& operator*() { 
            return msg; 
        }

        inline st::message* operator->() { 
            return &msg;
        }

        inline iterator& operator++() {
            increment();
            return *this;
        }

        inline iterator operator++(int) {
            increment();
            return *this;
        }
    };

    /**
     * @return an iterator to the beginning of the channel
     */
    inline iterator begin() const {
        iterator it(ctx());
        ++it; // get an initial value
        return it;
    }

    /**
     * If another `st::channel::iterator` == the iterator returned by this 
     * method, then the iterator has reached the end of the `st::channel`.
     *
     * @return an iterator to the end of the channel
     */
    inline iterator end() const { 
        return iterator(); 
    } 

    //--------------------------------------------------------------------------
    // Asynchronous Execution

    /**
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation back to this `st::channel`
     *
     * Internally calls `std::async` to asynchronously execute user function 
     * (Callable). This behavior is useful for evaluating long running functions 
     * in a way that will not block the current thread. Additionally, the 
     * internal usage of `std::async()` allows for optimizations made by the 
     * standard library when launching temporary worker threads.
     *
     * The resulting `st::message`'s `st::message::id()` will equal the value 
     * of argument `resp_id`.
     *
     * If the user function returns no value, then `st::message::data()` will be 
     * unallocated. Otherwise, `st::message::data()` will contain the value 
     * returned by Callable `f`.
     *
     * @param resp_id of the message that will be sent back to the this `st::channel` when `std::async` completes 
     * @param f a Callable to execute on another system thread
     * @param as optional arguments for `f`
     * @return `true` if the asynchronous call was scheduled, `false` if this `st::channel` is closed
     */
    template <typename F, typename... As>
    bool async(std::size_t resp_id, F&& f, As&&... as) {
        using isv = typename std::is_void<detail::function_return_type<F,As...>>;
        return async_impl(
                std::integral_constant<bool,isv::value>(),
                resp_id,
                std::forward<F>(f), 
                std::forward<As>(as)...);
    }
    
    /**
     * @brief start a timer 
     *
     * This method is an abstraction of `st::channel::async()`. Once the timer 
     * elapses a message containing `resp_id` as its id will be sent back to 
     * this channel. 
     *
     * @param resp_id of the message to be sent back to this channel after timeout
     * @param timeout a duration to elapse before timer times out
     * @param payload of the message to be sent back to this channel after timeout
     * @return `true` if the timer was started, `false` if this `st::channel` is closed
     */
    template< class Rep, class Period, typename P>
    bool timer(std::size_t resp_id, const std::chrono::duration<Rep, Period>& timeout, P&& payload) {
        return async(
                resp_id, 
                [=]() mutable -> decltype(std::forward<P>(payload)) { 
                    std::this_thread::sleep_for(timeout); 
                    return std::forward<P>(payload);
                });
    }
    
    /**
     * @brief start a timer 
     *
     * This method is an abstraction of `st::channel::async()`. Once the timer 
     * elapses a message containing `resp_id` as its id will be sent back to 
     * this channel. The message data payload will be empty.
     *
     * @param resp_id of the message to be sent back to this channel after timeout
     * @param timeout a duration to elapse before timer times out
     * @return `true` if the timer was started, `false` if this `st::channel` is closed
     */
    template< class Rep, class Period>
    bool timer(std::size_t resp_id, const std::chrono::duration<Rep, Period>& timeout) {
        return async(
                resp_id, 
                [=]() mutable -> void { 
                    std::this_thread::sleep_for(timeout); 
                });
    }

private:
    template <typename F, typename... As>
    bool async_impl(std::true_type, std::size_t resp_id, F&& f, As&&... as) {
        if(ctx()) {
            auto self = ctx();

            // launch a thread and schedule the call
            std::async([=]() mutable { 
                 f(std::forward<As>(as)...);
                 // capture a copy of the shared send context
                 self->send(st::message::make(resp_id));
            }); 

            return true;
        } else {
            return false;
        }
    }
    
    template <typename F, typename... As>
    bool async_impl(std::false_type, std::size_t resp_id, F&& f, As&&... as) {
        if(ctx()) {
            auto self = ctx();

            // launch a thread and schedule the call
            std::async([=]() mutable { 
                 auto result = f(std::forward<As>(as)...);
                 // capture a copy of the shared send context
                 self->send(st::message::make(resp_id, result));
            }); 

            return true;
        } else {
            return false;
        }
    }
};

}

#endif
