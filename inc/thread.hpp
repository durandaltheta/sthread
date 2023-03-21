//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_THREAD__
#define __SIMPLE_THREADING_THREAD__

#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>

#include "message.hpp"
#include "channel.hpp"
#include "scheduler_context.hpp"

namespace st { // simple thread

/**
 * @brief a thread object potentially managing its own system thread and an `st::channel`
 *
 * This object implements `std::thread`. Unlike many other objects in this 
 * library, `st::thread` does *NOT* represent some shared context that can be 
 * passed around. Instead, it inherits `std::thread`'s behavior meaning that it 
 * cannot be lvalue copied. Because of this, it can be treated similarly to 
 * `std::thread` in implementions where necessary.
 *
 * While this is less simple than might be desired for some usecases, 
 * implementing `st::thread` in this way allows for the most fine grained
 * control over thread behavior.
 *
 * All methods in this object are threadsafe.
 */
struct thread : public std::thread {
    /// implement std::thread API
    thread(const thread& rhs) = delete;

    /// implement std::thread API
    inline thread(thread&& rhs) : 
        m_ch(std::move(rhs.m_ch)),
        m_close_on_destruct(std::move(rhs.m_close_on_destruct)),
        m_close_soft(std::move(rhs.m_close_soft)),
        m_join_on_destruct(std::move(rhs.m_join_on_destruct)),
        std::thread(std::move(rhs) {
    }
    
    /** 
     * implement std::thread API
     * 
     * Default destructor behavior simply calls `std::thread::~thread()`. 
     *
     * However, alternative behavior can be specified with a call to
     * `st::thread::cleanup()`.
     */
    virtual ~thread();

    /// implement std::thread API
    thread& operator=(const thread& rhs) = delete;
    
    /// implement std::thread API
    inline thread& operator=(thread&& rhs) {
        m_ch = std::move(rhs.m_ch);
        m_close_on_destruct = std::move(rhs.m_close_on_destruct);
        m_close_soft = std::move(rhs.m_close_soft);
        m_join_on_destruct = std::move(rhs.m_join_on_destruct);
        std::thread::operator =(std::move(rhs));
        return *this;
    }

    /** 
     * @brief construct a new system thread running user `Function`
     *
     * This will construct the shared `st::channel` object for the `st::thread` 
     * and launch the `std::thread` with `st::this_thread::get_channel()`
     * set to a copy of the shared `st::channel` object. Example usage:
     * ```
     * #include <string>
     * #include <iostream>
     * #include <sthread>
     *
     * // define your function 
     * void my_function(std::string start_str, std::string stop_str) {
     *     std::cout << start_str;
     *
     *     auto ch = st::this_thread::get_channel();
     *     st::message msg;
     *
     *     while(ch.recv(msg)) {
     *         switch(msg.id()) {
     *             case 1:
     *                 // ...
     *                 break;
     *             case 2:
     *                 // ...
     *                 break;
     *             // ...
     *             case N:
     *                 // ...
     *                 break;
     *         }
     *     }
     *
     *     std::cout << stop_str;
     * }
     *
     * // launch your `st::thread` somewhere
     * std::thread my_thread;
     *
     * void somewhere() {
     *     my_thread(my_function, "hello", "world");
     *
     *     // send messages to your system thread via the `st::channel` 
     *     auto ch = my_thread.channel();
     *     ch.send(...);
     *     ch.send(...);
     *     ch.send(...);
     * }
     *
     * // when it is time to shutdown yoursystem thread, use the 
     * // `st::thread::join(bool)` overload to `close()` your `st::channel` and 
     * // `join()` the `std::thread`:
     * void time_to_shutdown() {
     *     my_thread.join(true);
     * }
     * ```
     *
     * The shared `st::channel` object will be automatically closed when 
     * function `f` returns. However, this behavior is only useful when the user 
     * has called `std::thread::detach()`. Otherwise, the user is still 
     * responsible to call `std::thread::join()` with respect to the joinable 
     * state of this object.
     */
    template< class Function, class... Args >
    explicit thread( Function&& f, Args&&... args ) : 
        m_ch(st::channel::make()),
        thread([=]() mutable {
            st::this_thread::detail::get_channel() = m_ch;
            f(std::forward<Args>(args)...);
            // a hard close because the receiver thread is shutting down
            m_ch.close(false); 
        })
    { }

    /**
     * @brief object wrapper for any Callable capable of accepting an `st::message`
     * 
     * Default constructor is useful for use as a default `OBJECT` in system 
     * threads which only processes messages sent via `st::channel::schedule()` 
     * ignoring all other messages.
     */
    struct callable {
        /// default constructor
        callable() : m_recv([](st::message msg) { /* do nothing */ }) { }

        /// Callable constructor
        template <typename CALLABLE>
        callable(CALLABLE&& cb) : m_recv(std::forward<CALLABLE>(cb)) { }

        inline void recv(st::message& msg) {
            m_recv(msg);
        }

    private:
        std::function<void(st::message msg)> m_recv;
    };

    /**
     * @brief statically construct a new system thread running user `OBJECT` associated with returned `st::thread`
     *
     * This will construct the shared `st::channel` object for the `st::thread` 
     * and launch the `std::thread` with `st::this_thread::get_channel()`
     * set to a copy of the shared channel object.
     *
     * Type `OBJECT` should be a class implementing the method 
     * `void recv(st::message msg)`. The `OBJECT` can have any other methods it 
     * requires:
     * ```
     * struct MyClass {
     *     MyClass(std::string start_str, std::string stop_str) :
     *         m_stop_str(stop_str) {
     *         std::cout << start_str;
     *     }
     *
     *     ~MyClass() {
     *         std::cout << m_stop_str;
     *     }
     *
     *     void recv(st::message m) {
     *         switch(msg.id()) {
     *             case 1:
     *                 // ...
     *                 break;
     *             case 2:
     *                 // ...
     *                 break;
     *             // ...
     *             case N:
     *                 // ...
     *                 break;
     *         }
     *     }
     *
     *     std::string m_stop_str;
     * };
     * ```
     *
     * `OBJECT::recv()` will be automatically called in a message receive loop 
     * until the `st::thread`'s `st::channel` is closed. Example usage:
     * ```
     * st::thread my_thread;
     *
     * void somewhere() {
     *     // launch your class to receive incoming messages:
     *     my_thread = st::thread::make<MyClass>("hello", "world");
     *
     *     // send messages to your class to process via it's `recv()` method
     *     auto ch = my_thread.channel();
     *     ch.send(...);
     *     ch.send(...);
     *     ch.send(...);
     * }
     *
     * // when it is time to shutdown your class and system thread, use the 
     * // `st::thread::join(bool)` overload to `close()` your `st::channel` and 
     * // `join()` the `std::thread`:
     * void time_to_shutdown() {
     *     my_thread.join(true);
     * }
     * ```
     *
     * This `st::thread::make<OBJECT>(...)` mechanism has the following advantages over 
     * normal `st::thread`s:
     * - The system thread's message receive loop is managed by the library 
     * - Objects allow for inheritance
     * - Objects allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
     * - Objects allow for member data to be used like local variables within the `st::thread`'s receive loop
     * - Objects allow for member methods, instead of forcing the user to rely on lambdas, local objects or global namespace functions if further function calls are desired.
     * - Objects enable RAII (Resource Acquisition is Initialization) semantics
     * - Object's constructors, runtime execution (`void recv(st::message`), and destructor are broken into separate functions, which I think can make them more readable than alternatives when dealing with system threads.
     *
     * `st::thread`'s `OBJECT` will be allocated on the scheduled system thread, 
     * not the calling system thread. This allows usage of `thread_local` data 
     * where necessary.
     *
     * This function ensures that whenever an `st::thread` is constructed its 
     * `OBJECT` will be immediately running and capable of receiving 
     * `st::message`s.
     *
     * The default `OBJECT` is a `st::thread::callable`, which is capable of 
     * representing any Callable object or function normally given to the
     * standard `st::thread(Function)` constructor. IE, calling
     * `st::thread::make<>(Callable, args...)` without a specified template type
     * produces similar behavior to `st::thread(Function&& f, Args&&... args)`. 
     *
     * The default constructor for `st::thread::callable` will produce a 
     * function which *ignores* all `st::message`s received over its 
     * `st::channel`, making it very useful for producing system threads which 
     * can have arbitrary code scheduled on them via a call to 
     * `st::channel::schedule()` on the `st::channel` returned via a call to 
     * `st::thread::channel()`.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=callable, typename... As>
    static st::thread make(As&&... as) {
        return st::thread([=]() mutable {
            data d = data::make<OBJECT>(std::forward<As>(as)...);
            
            // cast once to skip some processing indirection during msg handling
            OBJECT* obj = &(d.cast_to<OBJECT>());
            object_recv_loop([obj](message& msg) mutable { obj->recv(msg); });
        });
    }

    /**
     * The user is responsible for calling `st::channel::close()` on this object 
     * when they want any system thread executing an `st::channel::recv()` loop
     * to cease execution.
     *
     * @return a copy of the shared `st::channel` object
     */
    inline st::channel channel() {
        return m_ch;
    }

    /** 
     * An alternative to standard `std::thread::join()` which will call
     * `st::channel::close()` before calling `std::thread::join()`. Calling this 
     * variation of `join()` allows the user to indicate to the running system 
     * thread via its `thread_local` `st::channel` to end receiving of messages
     * and exit.
     *
     * If the user decides to use the standard `std::thread::join()` method, 
     * then the user is responsible for calling `st::channel::close()` somewhere
     * or the `std::thread::join()` may block forever.
     *
     * WARNING: This mechanism will not address any implementations of 
     * `st::thread` using other blocking mechanisms in its looping behavior.
     *
     * If `close_soft` == `true`, then any `st::message`s sent over the channel 
     * before this point will be processible by `st::channel::recv()` but all 
     * future calls to `st::channel::send()` will fail.
     *
     * @param close_soft passed to `st::channel::close()`
     */
    virtual inline void join(bool close_soft) {
        m_ch.close(close_soft);
        std::thread::join();
    }

    /**
     * Allows specification of alternative `st::thread` destructor behavior.
     *
     * @param close_on_destruct specify that `st::channel::close()` should be called
     * @param close_soft specify the boolean value to pass to `st::channel::close()`
     * @param join_on_destruct specify that `std::thread::join()` should be called 
     */
    inline void cleanup(
            bool close_on_destruct=false, 
            bool join_on_destruct=false, 
            bool close_soft=true) {
        m_close_on_destruct = close_on_destruct;
        m_join_on_destruct = join_on_destruct;
        m_close_soft = close_soft;
    }

private:
    // looping recv function executed by a thread using an `OBJECT`
    void object_recv_loop(const std::function<void(message&)>& hdl);
    st::channel m_ch; // internal thread channel 
    bool m_close_on_destruct = false;
    bool m_join_on_destruct = false;
    bool m_close_soft = true;
};


namespace this_thread {
    namespace detail {
        thread_local st::channel& get_channel();
    }
    
    /**
     * @return a copy of the thread_local `st::channel` created by this system thread's parent `st::thread`, if any
     */
    thread_local st::channel get_channel();
}

}

#endif
