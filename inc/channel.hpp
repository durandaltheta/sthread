//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CHANNEL__
#define __SIMPLE_THREADING_CHANNEL__

#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <utility>
#include <chrono>

#include "utility.hpp"
#include "context.hpp"
#include "message.hpp"

namespace st { // simple thread
namespace detail {
namespace channel {

struct context {
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
     * @param soft if `false` clear all previously sent messages from the internal message queue
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
     * Any message received that was sent via a call to 
     * `st::channel::schedule(...)` will be processed internally (executing the 
     * sent Callable and optional arguments) and not returned to the user. 
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
        return ctx() ? ctx()->recv(msg, false) : false;
    }
   
    /** 
     * @return count of currently unhandled messages sent to the `st::channel`'s queue
     */
    inline std::size_t queued() const {
        return ctx() ? ctx()->queued() : 0
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
        public std::input_iterator_tag {
                
        inline bool increment() const {
            bool ret = ctx() ? ctx()->recv(msg, true) == state::success : false;

            if(!ret) {
                ctx().reset();
            }

            return ret;
        }

        st::message msg;

    public:
        inline virtual ~iterator() { }

        inline iterator& operator=(const iterator& rhs) {
            ctx() = rhs.ctx();
            msg = rhs.msg;
            return *this;
        }

        inline T& operator*() const { 
            return msg; 
        }

        inline T* operator->() const { 
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
        iterator it;
        it.ctx(ctx());
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
     * @brief generic function wrapper for executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type. Is 
     * a new definition instead of a typedef so that it can be distinguished by 
     * receiving code. Messages processed by `st::message::handle(...)` will 
     * automatically execute any non-forwarded `st::message::task`s passed to it 
     * that are stored in the `st::message::data()` payload instead of passing 
     * the message to be processed by the user handler.
     *
     * If the user wishes to wrap their Callable code in this structure, but 
     * does not wish said code to be automatically executed by a receiver, then 
     * specify `forward` to `true`. Otherswise the wrapped code will be 
     * automatically executed when that `st::channel::task` is the data payload 
     * of an `st::message` received via `st::channel::recv()`.
     *
     * Because this object inherits `std::function<void()>` (and is a Callable),
     * it can be directly passed to `st::channel::schedule()` for execution 
     * (this will *remove* it's "forwardness"). This is useful once a forwarded 
     * `st::channel::task` is ready for execution on some worker.
     *
     * This object can be executed at any time by calling it's `()` operator:
     * 
     * task t([]{ std::cout << "hello world!" << std::endl; });
     * t(); // execute wrapped code and print "hello world!"
     */
    struct task : public std::function<void()> { 
        template <typename... As>
        task(bool forward, As&&... as) : 
            std::function<void()>(std::forward<As>(as)...), 
            m_fwd(forward) 
        { }

        inline const bool forward() const {
            return m_fwd;
        }

    private:
        const bool m_fwd;
    };

    /**
     * @brief wrap a Callable as a generic task for execution and forward to a receiver in an `st::channel::task`
     *
     * Callables sent via this method will *not* be automatically executed. They 
     * will instead be handed to the caller of `st::channel::recv()` for 
     * processing.
     *
     * @param id value to set as the `st::message::id()`
     * @param f Callable to forward to target sender
     * @param as optional remaining arguments for argument function
     * @return `true` on success, `false` on failure due to the channel being closed
     */
    template <typename F, typename... As>
    bool forward(std::size_t id, F&& f, As&&... as) {
        return m_ch.send(id, st::channel::task(true, to_thunk(std::forward<F>(f), std::forward<As>(as)...)));
    }
    
    /**
     * @brief wrap a Callable as a generic task and schedule it for execution on a receiver
     * 
     * The argument `f` will be executed when the message containing it is 
     * processed by a call to `st::channel::recv()`. The message will be handled 
     * internally upon receipt and not returned to the user for processing (will 
     * not cause `st::channel::recv()` to return).
     *
     * NOTE: No id value is required for this feature to function! A user only 
     * needs pass in a Callable. Disambiguation of the sent message is 
     * automatically done via type comparison by the receiver. 
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param f std::function to execute on target sender
     * @param as optional remaining arguments for argument function
     * @return `true` on success, `false` on failure due to the channel being closed
     */
    template <typename F, typename... As>
    bool schedule(F&& f, As&&... as) {
        return m_ch.send(0, st::channel::task(false, to_thunk(std::forward<F>(f), std::forward<As>(as)...)));
    }

    /**
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation back to this `st::channel`
     *
     * Internally calls `std::async` to asynchronously execute user function 
     * (Callable). This behavior is useful for evaluating long running functions 
     * in a way that will not block the current thread.
     *
     * NOTE: an `st::channel::task` is a valid Callable for this method!
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
    inline bool async(std::size_t resp_id, F&& f, As&&... as) {
        using isv = typename std::is_void<detail::function_return_type<F,As...>>;
        return async_impl(
                std::integral_constant<bool,isv::value>(),
                resp_id,
                to_thunk(std::forward<F>(f), std::forward<As>(as)...));
    }
    
    /**
     * @brief start a timer 
     *
     * This method is an abstraction of `st::channel::async()`. Once the timer 
     * elapses (and optional timeout_handler is executed) a message containing 
     * `resp_id` as its id will be sent back to this channel.
     *
     * @param resp_id of the message to be sent back to this channel after timeout
     * @param timeout a duration to elapse before timer times out
     * @param timeout_handler an optional Callable to execute after timeout but before sending the response back to the `st::channel`
     * @param as optional arguments for timeout_handler
     * @return `true` if the timer was started, `false` if this `st::channel` is closed
     */
    template< class Rep, class Period, typename F, typename... As>
    bool timer(std::size_t resp_id, const std::chrono::duration<Rep, Period>& timeout, F&& timeout_handler = []{}, As&&... as) {
        return async(
                resp_id, 
                [=]() mutable { 
                    std::this_thread::sleep_for(timeout); 
                    timeout_handler(std::forward<As>(as)...);
                });
    }

private:
    inline bool async_impl(std::true_type, std::size_t resp_id, std::function<void()> f) {
        if(ctx()) {
            auto self = ctx()

            // launch a thread and schedule the call
            std::async([=]() mutable { // capture a copy of the shared send context
                 f();
                 self.send(resp_id);
            }); 

            return true;
        } else {
            return false;
        }
    }
    
    inline bool async_impl(std::false_type, std::size_t resp_id, std::function<void()> f) {
        if(ctx()) {
            auto self = ctx()

            // launch a thread and schedule the call
            std::async([=]() mutable { // capture a copy of the shared send context
                 auto result = f();
                 self.send(resp_id, result);
            }); 

            return true;
        } else {
            return false;
        }
    }

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
};

}

#endif
