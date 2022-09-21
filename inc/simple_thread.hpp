//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis
/**
 * @file
 * @brief Simple interprocess threading and messaging
 */

#ifndef __SIMPLE_THREADING__
#define __SIMPLE_THREADING__

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <typeinfo>
#include <functional>
#include <ostream>
#include <sstream>
#include <stdio.h>
#include <string>
#include <vector>

namespace st { // simple thread

namespace detail {

// handle pre and post c++17 
#if __cplusplus >= 201703L
template <typename F, typename... Ts>
using function_return_type = typename std::invoke_result<unqualified<F>,Ts...>::type;
#else 
template <typename F, typename... Ts>
using function_return_type = typename std::result_of<unqualified<F>(Ts...)>::type;
#endif

// Convert void type to int
template <typename T>
struct convert_void_
{
    typedef T type;
};

template <>
struct convert_void_<void>
{
    typedef int type;
};

template <typename F, typename... A>
using convert_void_return = typename convert_void_<function_return_type<F,A...>>::type;

/*
 * A utility struct which will store the current value of an argument reference,
 * and restore that value to said reference when this object goes out of scope.
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

/*
 * logging utilities
 */
std::mutex& log_mutex();

template <typename... As>
void log(const char* file, std::size_t line_num, const char* func, const char* fmt, As&&... as) {
    std::lock_guard<std::mutex> lk(log_mutex());
    std::stringstream ss;
    ss << file << ":line" << line_num << ":" << func << ":";
    printf(ss.c_str());
    printf(fmt, as...);
    printf("\n"); 
}

}

#ifndef ST_DEBUG_LOG
#define ST_DEBUG_LOG(fmt, args...) st::detail::log(__FILE__, __LINE__, __FUNCTION__, fmt, args)
#endif

/**
 * @brief typedef representing the unqualified type of T
 */
template <typename T>
using base = typename std::remove_reference<typename std::remove_cv<T>::type>::type;

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
 * Type erased data container. Its purpose is similar to c++17 `std::any` but is 
 * backwards compatible to c++11.
 *
 * In practice, this is a wrapper around a std::unique_ptr<void,DELETER> that 
 * manages and sanitizes memory allocation.
 */
struct data {
    inline data() : m_data(nullptr, data::no_delete), m_type_code(0) { }
    inline data(const data& rhs) = delete; // cannot copy unique_ptr

    inline data(data&& rhs) : 
        m_data(std::move(rhs.m_data)), 
        m_type_code(std::move(rhs.m_type_code)) 
    { }

    /**
     * @brief type `T` deduced constructor
     */
    template <typename T>
    data(T&& t) : data(hint<T>(), std::forward<T>(t)) { }

    inline virtual ~data() { }

    /**
     * @brief construct a data payload using explicit template typing instead of by deduction
     *
     * This function is the most flexible way to construct data, as it does not 
     * rely on being given a pre-constructed payload `T` and can invoke any 
     * arbitrary constructor for type `T` based on arguments `as`.
     *
     * @param as optional constructor parameters 
     * @return an allocated data object
     */
    template <typename T, typename... As>
    static data make(As&&... as) {
        return data(hint<T>(), std::forward<As>(as)...);
    }

    data& operator(const data&) = delete; // cannot lvalue copy
    
    inline data& operator(data&& rhs) {
        m_data = std::move(rhs.m_data);
        m_type_code = std::move(rhs.m_type_code);
        return *this;
    }

    /**
     * @return `true` if the object represents an allocated data payload, else `false`
     */
    inline operator bool() {
        return m_data ? true : false;
    }

    /**
     * @return the stored compiler derived type code
     */
    inline const std::size_t type_code() const {
        return m_type_code;
    }
   
    /**
     * @brief determine at runtime whether the type erased data type code matches the templated type code.
     * @return true if the unqualified type of T matches the data type, else false
     */
    template <typename T>
    bool is() const {
        return m_type_code == st::type_code<T>();
    }

    /**
     * @brief cast message data payload to templated type reference 
     *
     * NOTE: this function is *NOT* type checked. A successful call to
     * `is<T>()` is required before casting to ensure type safety. It is 
     * typically better practice and generally safer to use `copy_to<T>()` or 
     * `move_to<T>()`, which include an internal type check.
     *
     * @return a reference of type T to the dereferenced void pointer payload
     */
    template <typename T>
    T& cast_to() {
        return *((base<T>*)(m_data.get()));
    }

    /**
     * @brief copy the data payload to argument t
     *
     * @param t reference to templated variable t to deep copy the data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool copy_to(T& t) {
        if(is<T>()) {
            t = cast_to<T>();
            return true;
        } else {
            return false;
        }
    }

    /**
     * @brief rvalue swap the data payload to argument t
     *
     * @param t reference to templated variable t to rvalue swap the data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool move_to(T& t) {
        if(is<T>()) {
            std::swap(t, cast_to<T>());
            return true;
        } else {
            return false;
        }
    }

private:
    typedef void(*deleter_t)(void*);
    typedef std::unique_ptr<void,deleter_t> data_pointer_t;

    template <typename T> struct hint { };

    template <typename T, typename... As>
    data(hint<T> h, As&&... as) :
        m_data(allocate<T>(std::forward<As>(as)...),data::deleter<T>),
        m_type_code(st::type_code<T>())
    { }

    template <typename T, typename... As>
    static void* allocate(As&&... as) {
        return (void*)(new base<T>(std::forward<As>(as)...));
    }

    template <typename T>
    static void deleter(void* p) {
        delete (base<T>*)p;
    }

    static inline void no_delete(void* p) { }

    data_pointer_t m_data;
    std::size_t m_type_code;
};

/**
 * @brief Interthread type erased message container 
 *
 * This object is *not* mutex locked beyond what is included in the 
 * `std::shared_ptr` implementation.
 */
struct message {
    inline message(){}
    inline message(const message& rhs) : m_context(rhs.m_context) { }
    inline message(message&& rhs) : m_context(std::move(rhs.m_context)) { }
    inline virtual ~message() { }

    /** 
     * @brief convenience function for templating 
     * @param msg message object to immediately return 
     * @return message object passed as argument
     */
    static inline message make(message msg) {
        return std::move(msg);
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @param t arbitrary typed data to be stored as the message data 
     * @return an allocated message
     */
    template <typename ID, typename T>
    static message make(ID id, T&& t) {
        return message(std::shared_ptr<context>(
            new context(static_cast<std::size_t>(id), std::forward<T>(t))));
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @return an allocated message
     */
    template <typename ID>
    static message make(ID id) {
        return message(std::shared_ptr<context>(
            new context(static_cast<std::size_t>(id), 0)));
    }

    /// lvalue assignment
    inline message& operator=(const message& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    /// rvalue assignment
    inline message& operator=(message&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

    /**
     * @return `true` if argument message represents this message (or no message), else `false`
     */
    inline bool operator==(const message& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }


    /**
     * @return `true` if argument message represents this message (or no message), else `false`
     */
    inline bool operator==(message&& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() {
        return m_context ? true : false;
    }

    /**
     * @brief an unsigned integer representing message's intended operation
     *
     * An `id` can trivially represent an enumeration, which can represent a 
     * specific request, response, or notification operation.
     */
    const std::size_t id() const {
        return m_context->m_id;
    }

    /**
     * @brief optional type erased payload data
     */
    inline st::data& data() {
        return m_context->m_data;
    }

    /**
     * Generic function wrapper for executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type. Is 
     * a new definition instead of a typedef so that it can be distinguished by 
     * receiving code.
     */
    struct task : public std::function<void()> { 
        template <typename F, typename... As>
        task(As&&... as) : std::function<void()>(std::forward<As>(as)...) { }
    };

private:
    struct context {
        context(const std::size_t c) : m_id(c) { }

        template <typename T>
        context(const std::size_t c, T&& t) :
            m_id(c),
            m_data(std::forward<T>(t))
        { }

        const std::size_t m_id;
        st::data m_data;
    };

    message(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    std::shared_ptr<context> m_context;
};

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between managed 
 * system threads. Provided here as a convenience for communicating from managed 
 * system threads to other user `st::fiber`s. All methods in this object are 
 * threadsafe.
 */
struct channel {
    inline channel(){}
    inline channel(const channel& rhs) : m_context(rhs.m_context) { }
    inline channel(channel&& rhs) : m_context(std::move(rhs.m_context)) { }
    inline virtual ~channel() { }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        return channel(std::shared_ptr<context>(new context));
    }

    inline const channel& operator=(const channel& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    inline const channel& operator=(channel&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

    /**
     * @return `true` if argument channel represents this channel (or no channel), else `false`
     */
    inline bool operator==(const channel& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    /**
     * @return `true` if argument channel represents this channel (or no channel), else `false`
     */
    inline bool operator==(channel&& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() const {
        return m_context ? true : false;
    }

    /**
     * @return `true` if the object is closed, else `false`
     */
    inline bool closed() const {
        return m_context->closed();
    }

    /** 
     * @brief shutdown the object (closed() == `false`)
     *
     * @param process_remaining_messages if true allow recv() to succeed until message queue is empty
     */
    inline void close(bool process_remaining_messages) {
        m_context->close(process_remaining_messages);
    }
    
    /// shutdown the object with default behavior
    inline void close() {
        close(true);
    }

    /** 
     * @return count of messages in the queue
     */
    inline std::size_t queued() const {
        return m_context->queued();
    }

    /**
     * @return count of `st::fiber`s blocked on `recv()` or are listening to this `st::channel`
     */
    inline std::size_t blocked_receivers() const {
        return m_context->blocked_receivers();
    }

    /**
     * @brief send a message with given parameters
     *
     * @param as arguments passed to `message::make()`
     * @return `true` on success, `false` if channel is closed
     * */
    template <typename... As>
    bool send(As&&... as) {
        return m_context->send(message::make(std::forward<As>(as)...));
    }

    /**
     * @brief optionally enqueue the argument message and receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue.
     *
     * A successful call to `recv()` will remove a message queued by `send()` 
     * from the internal channel message queue.
     *
     * Multiple simultaneous `recv()` calls will be served in the order they 
     * were called.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return `true` on success, `false` if channel is closed
     */
    inline bool recv(message& msg) {
        return m_context->recv(msg);
    }

    /**
     * @brief interface for objects registered as a listeners to this channel  
     *
     * `receiver`s can receive `st::message`s from this `st::channel`
     */
    struct receiver {
        virtual ~receiver(){}

        /**
         * @brief interface for receiver to receive a message
         *
         * If the reference `msg` is still allocated after `handle()` returns,
         * the `msg` will be treated as unhandled, the failed 
         * `st::channel::receiver` removed from the receiver queue, and the 
         * `msg` pushed to the front of the `st::channel::queue` to be handled 
         * by the next available `st::channel::receiver`.
         *
         * @param msg message to be received by the receiver implementation
         * @return if `true` requeue receiver to handle further messages, else throw out receiver
         */
        virtual bool handle(message& msg) = 0;
       
        /**
         * @brief allocate a new receiver from inheritor type `RECEIVER`
         * @param as optional constructor arguments for type `RECEIVER` 
         * @return an allocated receiver
         */
        template <typename RECEIVER, typename.. As>
        static std::unique_ptr<receiver> make(As&&... as) {
            return std::unique_ptr<receiver>(
                std::dynamic_cast<receiver*>(new RECEIVER(
                    std::forward<As>(as)...)));
        }
    };

    /**
     * @brief register a implementer of receiver interface as a listener to this channel 
     *
     * All `st::channel::receiver`s registered as listener are kept by the 
     * `st::channel` and will go out of scope when the last copy of the 
     * `st::channel` goes out of scope. 
     *
     * Registered `st::channel::receiver` objects will compete to receive 
     * `st::message`s sent to this `st::channel`.
     */
    inline bool listener(std::unique_ptr<receiver> rcv) {
        return m_context->listener(std::move(rcv));
    }

    /**
     * @brief register another `st::channel` as a receiver of `st::message`s sent to this `st::channel`
     *
     * The argument `st::channel` registered as a listener to this `st::channel` 
     * will be placed in the receiver queue for messages sent through this 
     * `st::channel`. 
     *
     * Listening channels are queued in the same fashion as calls to `recv()`,
     * executing in the order called. However, listening channels differ from 
     * calls to `recv()` in that they are immediately requeued internally to
     * receive further messages.
     *
     * If a call to a listening `st::channel`'s `send()` fails, the 
     * `st::channel` will be removed from the receiver queue and another 
     * `st::channel::receiver` for the message will be retrieved if available.
     *
     * WARNING: `st::channel`s should never be set as listeners to each 
     * other, as this will create an infinite loop once an `st::message` is sent 
     * to one.
     *
     * @param ch other channel to set as a listener
     * @return `true` on success, `false` if channel is closed
     */
    inline bool listener(st::channel ch) {
        return listener(receiver::make<redirect>(ch));
    }

    /**
     * @brief typedef of callback function used by `st::channel`
     */
    typedef std::function<void(message)> callback;

    /**
     * @brief register a channel as a listener with a callback that will be forwarded to the listener as an `st::message::task` on `st::message` receipt
     *
     * An `st::message::task` will be forwarded to the listening channel 
     * whenever an `st::message` would be forwarded to the listening channel. 
     * When the `st::message::task` is executed the given 
     * `st::channel::callback` will be called with the received `st::message`.
     *
     * If a call to a listening `st::channel`'s `send()` fails, the 
     * `st::channel` will be removed from the receiver queue and another 
     * `st::channel::receiver` for the message will be retrieved if available.
     *
     * @param ch other channel to set as a listener
     * @param cb callback function to be forwarded for execution to listening channel 
     * @return `true` on success, `false` if channel is closed
     */
    inline bool listener(channel ch, callback cb) {
        return listener(receiver::make<redirect_callback>(std::move(ch), std::move(cb)));
    }

    /**
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation to this `st::channel`
     *
     * Internally calls `std::async` to asynchronously execute user function.
     * If function returns no value, then `st::message::data()` will be 
     * unallocated.
     *
     * @param resp_id id of message that will be sent back to the this `st::fiber` when `std::async` completes 
     * @param f function to execute on another system thread
     * @param as arguments for argument function
     */
    template <typename F, typename... As>
    bool async(std::size_t resp_id, F&& f, As&&... as) {
        using isv = typename std::is_void<detail::function_return_type<F,A...>>;
        return async_impl(std::integral_constant<bool,isv::value>(),
                          ch,
                          resp_id,
                          std::forward<F>(f),
                          std::forward<As>(as)...);
    }

private:
    template <typename F, typename... As>
    bool async_impl(std::true_type, std::size_t resp_id, F&& f, As&&... as) {
        channel self = *this;

        // launch a thread with a default functor and schedule the call
        std::async([=]{ // capture a copy of the fiber
             f(std::forward<As>(as)...);
             self.send(resp_id);
        }); 
    }
    
    template <typename F, typename... As>
    bool async_impl(std::false_type, std::size_t resp_id, F&& f, As&&... as) {
        channel self = *this;

        // launch a thread with a default functor and schedule the call
        std::async([=]{ // capture a copy of the fiber
             auto result = f(std::forward<As>(as)...);
             self.send(resp_id, result);
        }); 
    }

    struct redirect : public receiver {
        bool handle(message&);
        channel ch;
    };

    struct redirect_callback : public receiver {
        virtual ~callback(){}
        bool handle(message&);
        channel ch;
        std::function<void(message)> cb;
    };

    struct blocker : public receiver {
        struct data {
            data() = delete;
            data(message* m) : msg(m) { }

            inline void wait(std::unique_lock<std::mutex>& lk) {
                do {
                    cv.wait(lk);
                } while(!flag);
            }

            inline void signal(message& m) {
                *msg = std::move(m);
                flag = true;
                cv.notify_one(); 
            }

            bool flag = false;
            std::condition_variable cv;
            message* msg;
        };

        blocker(data* d) : m_data { }
        virtual ~blocker();

        bool handle(message&);
    
    private:
        data* m_data;
    };

    struct context {
        context() : m_closed(false), m_cur_key(1) { }

        inline bool closed() const { 
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_closed;
        }
        
        inline std::size_t queued() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_msg_q.size();
        }

        inline std::size_t blocked_receivers() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_recv_q.size();
        }

        void close(bool process_remaining_messages);
        void handle_queued_messages(std::unique_lock<std::mutex>& lk);
        bool send(message msg);
        bool recv(message& msg);
        bool listener(std::unique_ptr<receiver> rcv);

        bool m_closed;
        std::mutex m_mtx;
        std::deque<message> m_msg_q;
        std::deque<std::unique_ptr<receiver>> m_recv_q;
    };

    channel(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    mutable std::shared_ptr<context> m_context;
};

/**
 * @brief a coroutine intended to run on either a system thread or another executing `st::fiber`
 *
 * According to wikipedia: Coroutines are computer program components that 
 * generalize subroutines for non-preemptive multitasking, by allowing 
 * execution to be suspended and resumed.
 *
 * The general advantages of using coroutines compared to thread`s:
 * - changing which coroutine is running by suspending its execution is 
 *   exponentially faster than changing which system thread is running. IE, the 
 *   more concurrent operations need to occur, the more efficient coroutines 
 *   become in comparison to threads.
 * - faster context switching results in faster communication between code
 * - coroutines take less memory than threads 
 * - the number of coroutines is not limited by the operating system
 * - coroutines do not require system level calls to create
 *
 * The general disadvantages of using coroutines:
 * - coroutines are expected to use only non-blocking operations to avoid
 *   blocking their parent thread.
 *
 * While more powerful coroutines are possible in computing, particularly with 
 * assembler level support which allocates stacks for coroutines, the best that 
 * can be accomplished at present in C++ is stackless coroutines. This means 
 * that code cannot be *arbitrarily* suspended and resumed at will (although 
 * this can be simulated with some complicated `switch` based hacks which add  
 * significant complexity, and also come with their own limitations. Further 
 * support for this kind of coroutine is provided in C++20 and onwards). 
 *
 * Instead this library allows the user to create `st::fiber` instances 
 * with user defined objects as a template argument. This is the case in 
 * these functions: 
 * `st::fiber::thread<OBJECT>(...)`
 * `st::fiber::threadpool<OBJECT>(...)`
 * `st::fiber::coroutine<OBJECT>(...)`
 *
 * Type `OBJECT` should be a class implementing the method 
 * `void recv(st::message msg)`:
 * ```
 * struct MyClass {
 *     void recv(st::message m);
 * };
 * ```
 *
 * Message arguments can also be accepted by reference:
 * ```
 * struct MyClass {
 *     void recv(st::message& m);
 * };
 * ```
 *
 * Note: `st::fibers`s automatically throw out any unallocated messages 
 * received over their internal `st::channel` instead of passing them to the 
 * `OBJECT`'s `recv()` implementation.
 *
 * Note: Any `st::message` received by an `st::fiber` that contains an 
 * `st::message::task()` will have its payload immediately executed and 
 * skip passing the messsage to the `OBJECT`'s `recv()` implementation.
 *
 * Objects instead of functions are useful for interthread message handling 
 * because they allow member data to persist between calls to 
 * `recv(st::message)` without any additional management and for all member data 
 * to be easily accessible without dealing with global variables or void* 
 * pointers.
 *
 * Another distinct advantage is objects are able to make intelligent use 
 * of C++ RAII semantics, as the objects can specify constructors and 
 * destructors as normal. In fact, the user's `OBJECT` is constructed on the 
 * target `st::fiber`'s thread, instead of the calling thread, allowing for 
 * `thread_local` data to be safely used by the user `OBJECT`.
 *
 * Because `st::fiber`s are designed to run in a non-blocking fashion, any 
 * message whose processing blocks an `st::fiber` indefinitely can cause all 
 * sorts of bad effects, including deadlock. In a scenario where a long running 
 * or indefinite call needs to be made, it may be better to call `std::async` 
 * to execute the function on a dedicated system thread, then send a message 
 * back to the originating fiber with the result. 
 *
 * `st::channel::async(...)` and `st::fiber::async(...)` are provided to do 
 * exactly this operation:
 * ```
 * // given some type `result_t`
 * result_t long_running_call() {
 *     ...
 * }
 *
 * struct MyFiberObject {
 *     enum op {
 *         // ...
 *         long_running_result,
 *         // ...
 *     };
 *
 *     void recv(message msg) {
 *         switch(msg.id()) {
 *             // ...
 *                 // execute the given function on another system thread and
 *                 // `send(...)` the result back to this `st::fiber` as a message.
 *                 st::fiber::self().async(long_running_result, long_running_call);
 *                 break;
 *             case op::long_running_result: 
 *                 // do something with message containing result_t
 *                 break;
 *             // ...
 *         }
 *     }
 * };
 * ```
 */
struct fiber {
    //--------------------------------------------------------------------------
    // construction & assignment

    inline fiber(){}
    inline fiber(const fiber& rhs) : m_context(rhs.m_context) { }
    inline fiber(fiber&& rhs) : m_context(std::move(rhs.m_context)) { }
    virtual ~fiber();

    inline const fiber& operator=(const fiber& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    inline const fiber& operator=(fiber&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

    /**
     * @return an approximation of maximum concurrent threads on the executing hardware
     */
    static std::size_t concurrency() {
        std::size_t count = std::thread::hardware_concurrency();
        return count ? count : 1; // enforce at least 1
    }

    /**
     * Empty `OBJECT` which only processes messages sent via `schedule()` (and
     * therefore implicitly `listen(ch, cbk)`), ignoring all other messages.
     */
    struct processor { 
        inline void recv(message& msg) { }
    };

    /**
     * @brief statically construct a new system thread running user `OBJECT` associated with returned `st::fiber`
     *
     * Because `st::fiber`s allocation constructors are private, either this 
     * function or `st::fiber::threadpool<...>(...)` must be called to generate 
     * an initial, allocated root `st::fiber`. This mechanism ensures that
     * whenever an `st::fiber` is constructed its `OBJECT` will be immediately 
     * running and capable of receiving `st::message`s.
     *
     * `st::fiber`'s `OBJECT` will be allocated on the scheduled thread, not the 
     * calling thread. This allows usage of `thread_local` data where necessary.
     *
     * The user is responsible for holding a copy of the returned `st::fiber`
     * to ensure the system thread does not shutdown and user `OBJECT` is kept 
     * in memory.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=processor, typename... As>
    static fiber thread(As&&... as) {
        return fiber(context::thread<OBJECT>(std::forward<As>(as)...));
    }

    /**
     * @brief construct a root `st::fiber` managing a prescribed number of child `st::fiber` system threads listening to the root's internal `st::channel`
     *
     * This is useful for generating an efficient multi-thread executor for 
     * `st::fiber::schedule()`ing tasks or `st::fiber::coroutine()`ing new 
     * `st::fiber`s by executing those functions on a listening child fiber 
     * system thread.
     *
     * A default attempt at generating an executor with maximal processing 
     * throughput can be acquired by calling this function with all default 
     * arguments:
     * `st::fiber::threadpool<>()`
     *
     * The user is responsible for holding a copy of the root `st::fiber` 
     * returned by this function to keep it in memory, The root fiber holds 
     * copies of its children to keep them in memory, but the root `st::fiber` 
     * itself has no such protection.
     *
     * Similarly when the root `st::fiber` is `shutdown()` or the last copy goes 
     * out of scope, all child `st::fiber`s will be `shutdown()` alongside it.
     *
     * @param count the number of worker `fiber` threads in the pool 
     * @param as optional arguments for type `OBJECT`
     * @return a `st::fiber` with count number of child `fiber` system threads
     */
    template <typename OBJECT=processor, typename... As>
    static fiber threadpool(std::size_t count=fiber::concurrency(), As&&... as) {
        return fiber(context::threadpool<OBJECT>(count, std::forward<As>(as)...));
    }

    /**
     * @brief construct and launch a non-blocking, cooperative `st::fiber` created with user `OBJECT` as a child of the calling st::fiber`
     *
     * `st::fiber`'s `OBJECT` will be allocated on the scheduled thread, not the 
     * calling thread. This allows usage of `thread_local` data where necessary.
     *
     * The user is responsible for holding a copy of the returned `st::fiber`
     * to ensure user `OBJECT` is kept in memory.
     *
     * `st::fiber` coroutines gain a significant communication speed boost when 
     * communicating with other `st::fiber`s executing on the same system thread 
     * compared to communicating with `st::fiber`s running on different system 
     * threads.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     * @return allocated `st::fiber` if successfully launched, else empty pointer
     */
    template <typename OBJECT=processor, typename... As>
    fiber coroutine(As&&... as) {
        return fiber(m_context->launch_coroutine<OBJECT>(std::forward<As>(as)...));
    }

    //--------------------------------------------------------------------------
    // state

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() const {
        return m_context ? true : false;
    }

    /**
     * @return `true` if argument fiber represents this fiber (or no fiber), else `false`
     */
    inline bool operator==(const fiber& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    /**
     * @return `true` if argument fiber represents this fiber (or no fiber), else `false`
     */
    inline bool operator==(fiber&& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    /**
     * @return the `std::thread::id` of the system thread this `st::fiber` is running on
     */
    inline std::thread::id get_id() const {
        return m_context ? m_context->get_thread_id() : std::thread::id();
    }

    /**
     * This static function is intended to be called from within an `OBJECT` 
     * running in an `st::fiber`.
     *
     * @return a copy of the `st::fiber` currently running on the calling thread, if none is running will return an unallocated `st::fiber`
     */
    static inline fiber self() {
        return fiber(context::tl_self().lock());
    }

    /**
     * @brief return the parent `st::fiber` of this instance
     * @return allocated `st::fiber` if this object has a parent, else the root `st::fiber`
     */
    inline fiber parent() const {
        return fiber(m_context->parent());
    }

    /**
     * @return allocated root `st::fiber` at the top of the family tree
     */
    inline fiber root() const {
        return m_context ? fiber(m_context->root()) : fiber();
    }

    //--------------------------------------------------------------------------
    // lifecycle 

    /**
     * @return `true` if the object is running, else `false`
     */
    inline bool running() const {
        return m_context->running();
    }

    /** 
     * @brief shutdown the object (running() == `false`)
     *
     * Any threadpool child `st::fiber`s of this `st::fiber` will also have 
     * `st::fiber::shutdown(process_remaining_messages)` called on them.
     *
     * @param process_remaining_messages if true allow recv() to succeed until message queue is empty
     */
    inline void shutdown(bool process_remaining_messages) {
        m_context->shutdown(process_remaining_messages);
    }
    
    /// shutdown the object with default behavior
    inline void shutdown() {
        shutdown(true);
    }

    //--------------------------------------------------------------------------
    // communication

    /**
     * @brief send a message with given parameters
     *
     * The `st::message` sent by this function will be passed to this
     * `st::fiber`'s `OBJECT` `recv()` method.
     *
     * @param as arguments passed to `message::make()`
     * @return `true` on success, `false` on failure due to object being shutdown 
     * */
    template <typename... As>
    bool send(As&&... as) {
        return m_context->send(message::make(std::forward<As>(as)...));
    }
   
    /**
     * @brief schedule a generic task for execution 
     * @param t `message::task` to execute on target sender
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    inline bool schedule(message::task t) {
     * @param t std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being shutdown 
        return m_context->schedule(std::move(t));
    }

    /**
     * @brief schedule a generic task for execution 
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    inline bool schedule(std::function<void()> f) {
        return schedule(message::task(std::move(f)));
    }

    /**
     * @brief wrap user function and arguments then schedule as a generic task for execution
     *
     * @param f function to execute on target sender 
     * @param as arguments for argument function
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    template <typename F, typename... As>
    bool schedule(F&& f, As&&... as) {
        return schedule(message::task([=]() mutable { f(std::forward<As>(as)...); }));
    }

    /**
     * @brief wrap user function and arguments then schedule them on a different, dedicated system thread and send the result of the operation back to this `st::fiber`
     *
     * @param resp_id id of message that will be sent back to the this `st::fiber` when `std::async` completes 
     * @param f function to execute on another system thread
     * @param as arguments for argument function
     */
    template <typename F, typename... As>
    bool async(std::size_t resp_id, F&& f, As&&... as) {
        return m_context->m_ch.async(resp_id, std::forward<F>(f), std::forward<As>(as)...);
    }
   
    /**
     * @brief register this `st::fiber` to listen for `st::message`s through an additional, external `st::channel`
     *
     * This functionality is useful for abstracting communications between 
     * `st::fiber`s or system threads using an `st::channel` as a shared 
     * interface.
     *
     * `st::message`s received from the external `st::channel` will be forwarded 
     * to this `st::fiber` `OBJECT`'s `recv()` method as if the message had been 
     * sent with `st::fiber::send()`.
     *
     * @param ch channel to forward messages from to this `st::fiber`
     * @return `true` if registration as a listener succeeded, else `false`
     */
    inline bool listen(st::channel ch) {
        return ch.listener(m_context->m_ch);
    }

    /**
     * @brief register this `st::fiber` to listen for `st::message`s through an additional, external `st::channel` and evaluate the received `st::message`s via a callback
     *
     * This functionality is useful for abstracting communications between 
     * `st::fiber`s or system threads using an `st::channel` as a shared 
     * interface.
     *
     * `st::message`s received from the external `st::channel` will be handled 
     * by the argument `st::channel::callback` function on the system thread the 
     * `OBJECT` is running on, making this callback inherently threadsafe. This 
     * callback will only be executed by it's originating `st::fiber`, ensuring 
     * that `OBJECT` will be in memory when the callback executes. Because of 
     * this, the callback function can wrap any other operation, including calls 
     * to `OBJECT` methods by reference.
     *
     * This callback mechanism allows the user to handle `st::message`s separate 
     * from the main `OBJECT` `recv(...)` handler. Reasons for such behavior 
     * include:
     * - Handling messages from channels with only one message type, 
     *   particularly from `st::channel`s with a limited lifetime where usage 
     *   of an enumeration id is unnecessary.
     * - Dealing with messages which use `id()` enumerations that conflict with 
     *   the enumeration definition used in the `OBJECT`'s `recv(...)` message 
     *   handler.
     *
     * @param ch channel to forward messages from to this `st::fiber`
     * @param cb a callback function to be executed with the received message
     * @return `true` if registration as a listener succeeded, else `false`
     */
    inline bool listen(st::channel ch, st::channel::callback cb) {
        return ch.listener(m_context->m_ch, std::move(cb));
    }

private:
    struct context : public std::enable_shared_from_this<context> {
        context(std::weak_ptr<context> root = std::weak_ptr<context>(), 
                std::weak_ptr<context> parent = std::weak_ptr<context>(),
                std::thread::id thread_id = std::thread::id()) : 
            m_shutdown(false),
            m_ch(channel::make()),
            m_root(root),
            m_parent(parent),
            m_thread_id(thread_id)
        { }

        context() : 
            m_shutdown(false),
            m_ch(channel::make()),
        { }

        virtual ~context();
       
        // static fiber thread constructor function
        template <typename OBJECT, typename... As>
        static std::shared_ptr<context> thread(As&&... as) {
            std::shared_ptr<context> c(new context);
            c->launch_thread<OBJECT>(std::forward<As>(as)...);
            return c;
        }
        
        // static fiber threadpool constructor function
        template <typename OBJECT, typename... As>
        static std::shared_ptr<context> threadpool(As&&... as) {
            std::shared_ptr<context> c(new context);
            c->launch_threadpool<OBJECT>(std::forward<As>(as)...);
            return c;
        }

        // thread local data
        static std::weak_ptr<context>& tl_self();
        static std::weak_ptr<context>& tl_root();
       
        // looping recv function executed by a root fiber
        void thread_loop(const std::shared_ptr<context>& self, 
                         const std::function<void()>& do_late_init);

        // construct a fiber running on a dedicated system thread
        template <typename OBJECT, typename... As>
        void launch_thread(As&&... as) {
            std::shared_ptr<context> self = shared_from_this();
            m_self = self;
            m_root = self;
            m_parent = self;

            std::function<void()> do_late_init = [=]() mutable { 
                late_init<OBJECT>(std::forward<As>(as)...); 
            };

            std::thread thd([=]{ this->thread_loop(self, do_late_init); });

            m_thread_id = thd.get_id();
            thd.detach();
        }

        // construct a threadpool of fibers running on dedicated system threads
        template <typename OBJECT, typename... As>
        void launch_threadpool(As&&... as) {
            std::shared_ptr<context> self = shared_from_this();
            m_self = self;
            m_root = self;
            m_parent = self;

            // storage location for threadpool child fibers
            m_threadpool_children = std::unique_ptr<std::vector<fiber>>(
                new std::vector<fiber>(count)); 

            // Create `count` number of `st::fiber`s into the root `st::fiber`. 
            // All `st::fiber`s will be created on their own system thread and 
            // as listeners to the root `st::fiber`s `st::channel`.
            for(std::size_t i=0; i<count; ++i) {
                fiber f = fiber::thread<OBJECT>(std::forward<As>(as)...);
                f.listen(m_ch);
                m_threadpool_children[i] = std::move(f);
            }
        }

        // object which initially receives messages over the fiber's channel
        struct fiber_receiver : public channel::receiver {
            virtual ~fiber_receiver(){}
            bool handle(message msg);
            std::weak_ptr<context> weak_ctx;
        };

        // handle a message received by fiber_receiver
        bool handle_received_message(message& msg);

        // requeue fiber on its parent for message processing
        bool wakeup(std::unique_lock<std::mutex>&);

        // construct a fiber as a coroutine
        template <typename OBJECT, typename... As>
        std::shared_ptr<context> launch_coroutine(As&&... as) {
            std::shared_ptr<context> c = new context(root(), 
                                                     m_self.lock(), 
                                                     get_thread_id());
            // self must be set before this function returns
            c->m_self = c->shared_from_this();

            // create a task to run on the parent to finish initialization
            message::task t([=]() mutable { 
                c->late_init_coroutine<OBJECT>(std::forward<As>(as)...);
            });

            return schedule(std::move(t))) ? c : std::shared_ptr<context>();
        }

        // finish coroutine initialization on its parent's system thread
        template <typename OBJECT, typename... As>
        void late_init_coroutine(As&&... as) {
            // construct `OBJECT`
            late_init<OBJECT>(std::forward<As>(as)...); 

            // setup this fiber as a receiver to its internal channel
            m_ch.listener(
                channel::receiver::make<fiber_receiver>(
                    m_self.lock()));

            // schedule the fiber for execution on its parent
            parent()->schedule(nonblocking_fiber_task());
        }

        /*
         * Finish initializing the `st::fiber` by allocating the `OBJECT` object 
         * and related handlers. Should be called on the scheduled parent 
         * `st::fiber`.
         */
        template <typename OBJECT, typename... As>
        void late_init(As&&... as) {
            // properly set the thread_local self `st::fiber` before `OBJECT` construction
            detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
            tl_self() = m_self.lock();

            // construct the `OBJECT` 
            m_object = data::make<OBJECT>(std::forward<As>(as)...);

            // generate a message handler wrapper for `OBJECT`
            m_hdl = [&](message& msg) {
                if(msg.data().is<task>()) {
                    msg.data().cast_to<task>()(); // evaluate task immediately
                } else {
                    m_object.cast_to<OBJECT>().recv(msg);
                }
            };
        }
        
        // return a task capable of processing a received message which will 
        // keep this fiber in memory for its lifetime
        message::task nonblocking_fiber_task() const;

        // process a received message in a nonblocking manner
        void nonblocking_process_message();

        inline bool running() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_shutdown;
        }
    
        void shutdown(bool process_remaining_messages);

        inline bool send(message&& msg) {
            return m_ch.send(std::move(msg));
        }

        inline bool schedule(message::task&& t) {
            std::lock_guard<std::mutex> lk(m_mtx);
            return send(message::make(0,std::move(t)));
        }

        inline std::shared_ptr<context> parent() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            std::shared_ptr<context> parent = m_parent.lock();
            return parent ? parent : m_root.lock();
        }

        inline std::shared_ptr<context> root() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_root.lock();
        }

        inline std::thread::id get_thread_id() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_thread_id;
        }

        std::mutex m_mtx;
        bool m_shutdown;
        channel m_ch; // internal fiber channel
        std::deque<message> m_received_msgs; // queue of messages received by receiver
        std::weak_ptr<context> m_self; // weak pointer to self
        std::weak_ptr<context> m_root; // weak pointer to root fiber
        std::weak_ptr<context> m_parent; // weak pointer to parent fiber
        std::thread::id m_thread_id; // thread id the user object is executing on
        data m_object; // user object data
        std::function<void(message&)> m_hdl; // function wrapper utilizing user object to parse messages 
        std::unique_ptr<std::vector<fiber>> m_threadpool_children; // storage location for threadpool child fibers
    };

    fiber(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    std::shared_ptr<context> m_context;
};

/**
 * @brief writes a textual representation of a fiber to the output stream
 * @return a reference to the argument ostream reference
 */
template<class CharT, class Traits>
std::basic_ostream<CharT,Traits>&
operator<<(std::basic_ostream<CharT,Traits>& ost, fiber f) {
    std::stringstream ss;
    ss << std::hex << f.get();
    ost << ss.str();
    return ost;
}

}

#endif
