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
#include <iostream>
#include <future>

namespace st { // simple thread

namespace detail {

template <typename T>
using unqualified = typename std::decay<T>::type;

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

inline void inner_log(){}

template <typename A, typename... As>
inline void inner_log(A&& a, As&&... as) {
    std::cout << a;
    inner_log(std::forward<As>(as)...);
}

template <typename... As>
void log(const char* file, std::size_t line_num, const char* func, As&&... as) {
    std::lock_guard<std::mutex> lk(log_mutex());
    std::stringstream ss;
    ss << file << ":line" << line_num << ":" << func << ":";
    std::cout << ss.str();
    inner_log(std::forward<As>(as)...);
    std::cout << std::endl;
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

    data& operator=(const data&) = delete; // cannot lvalue copy
    
    inline data& operator=(data&& rhs) {
        m_data = std::move(rhs.m_data);
        m_type_code = std::move(rhs.m_type_code);
        return *this;
    }

    /**
     * @return `true` if the object represents an allocated data payload, else `false`
     */
    inline operator bool() const {
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
 * system threads to other user `st::thread`s. All methods in this object are 
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
     * @return count of `st::thread`s blocked on `recv()` or are listening to this `st::channel`
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
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation to this `st::channel`
     *
     * Internally calls `std::async` to asynchronously execute user function.
     * If function returns no value, then `st::message::data()` will be 
     * unallocated.
     *
     * @param resp_id id of message that will be sent back to the this `st::thread` when `std::async` completes 
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

private:
    template <typename F, typename... As>
    void async_impl(std::true_type, std::size_t resp_id, F&& f, As&&... as) {
        channel self = *this;

        // launch a thread with a default functor and schedule the call
        std::async([=]() mutable { // capture a copy of the thread
             f(std::forward<As>(as)...);
             self.send(resp_id);
        }); 
    }
    
    template <typename F, typename... As>
    void async_impl(std::false_type, std::size_t resp_id, F&& f, As&&... as) {
        channel self = *this;

        // launch a thread with a default functor and schedule the call
        std::async([=]() mutable { // capture a copy of the thread
             auto result = f(std::forward<As>(as)...);
             self.send(resp_id, result);
        }); 
    }

    struct context {
        struct blocker {
            struct data {
                data(message* m) : msg(m) { }

                inline void wait(std::unique_lock<std::mutex>& lk) {
                    do {
                        cv.wait(lk);
                    } while(!flag);
                }

                inline void signal() {
                    flag = true;
                    cv.notify_one(); 
                }

                inline void signal(message& m) {
                    *msg = std::move(m);
                    signal();
                }

                bool flag = false;
                std::condition_variable cv;
                message* msg;
            };

            blocker(data* d) : m_data(d) { }
            ~blocker(){ m_data->signal(); }
            inline void handle(message& msg){ m_data->signal(msg); }
        
            data* m_data;
        };

        context() : m_closed(false) { }

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

        bool m_closed;
        mutable std::mutex m_mtx;
        std::deque<message> m_msg_q;
        std::deque<std::unique_ptr<blocker>> m_recv_q;
    };

    channel(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    mutable std::shared_ptr<context> m_context;
};

/**
 * @brief a thread object managing its own system thread
 *
 * This library allows the user to create `st::thread` instances with user 
 * defined objects as a template argument with a call to static function:
 * `st::thread::make<OBJECT>(...)`
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
 * Note: `st::threads`s automatically throw out any unallocated messages 
 * received over their internal `st::channel` instead of passing them to the 
 * `OBJECT`'s `recv()` implementation.
 *
 * Objects instead of functions are useful for interthread message handling 
 * because they allow member data to persist between calls to 
 * `OBJECT::recv(st::message)` without any additional management and for all 
 * `OBJECT` member data to be easily accessible without dealing with global 
 * variables, void* pointers, or `std::thread` arguments.
 *
 * Another distinct advantage is objects are able to make intelligent use 
 * of C++ RAII semantics, as the objects can specify constructors and 
 * destructors as normal. In fact, the user's `OBJECT` is constructed on the 
 * target `st::thread`'s thread, instead of the calling thread, allowing for 
 * `thread_local` data to be safely used by the user `OBJECT`.
 *
 * Because `st::thread`s are designed to run in a non-blocking fashion, any 
 * message whose processing blocks an `st::thread` indefinitely can cause all 
 * sorts of bad effects, including deadlock. In a scenario where a long running 
 * or indefinite call needs to be made, it may be better to call `std::async` 
 * to execute the function on a dedicated system thread, then send a message 
 * back to the originating thread with the result. 
 *
 * `st::channel::async(...)` and `st::thread::async(...)` are provided to do 
 * exactly this operation:
 * ```
 * // given some type `result_t`
 * result_t long_running_call() {
 *     ...
 * }
 *
 * struct MythreadObject {
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
 *                 // `send(...)` the result back to this `st::thread` as a message.
 *                 st::thread::self().async(long_running_result, long_running_call);
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
struct thread {
    //--------------------------------------------------------------------------
    // construction & assignment

    inline thread(){}
    inline thread(const st::thread& rhs) : m_context(rhs.m_context) { }
    inline thread(st::thread&& rhs) : m_context(std::move(rhs.m_context)) { }

    virtual ~thread() {
        // explicitly shutdown root thread channel because a system thread 
        // holds a copy of this thread which keeps the channel alive even 
        // though the root thread is no longer reachable
        if(m_context && m_context.use_count() < 3) {
            shutdown();
        }
    }

    inline const st::thread& operator=(const st::thread& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    inline const st::thread& operator=(st::thread&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

    /**
     * Empty `OBJECT` which only processes messages sent via `schedule()` 
     * ignoring all other messages.
     */
    struct processor { 
        inline void recv(message& msg) { }
    };

    /**
     * @brief statically construct a new system thread running user `OBJECT` associated with returned `st::thread`
     *
     * Because `st::thread`s allocation constructors are private, either this 
     * function or `st::thread::threadpool<...>(...)` must be called to generate 
     * an initial, allocated root `st::thread`. This mechanism ensures that
     * whenever an `st::thread` is constructed its `OBJECT` will be immediately 
     * running and capable of receiving `st::message`s.
     *
     * `st::thread`'s `OBJECT` will be allocated on the scheduled thread, not the 
     * calling thread. This allows usage of `thread_local` data where necessary.
     *
     * The user is responsible for holding a copy of the returned `st::thread`
     * to ensure the system thread does not shutdown and user `OBJECT` is kept 
     * in memory.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=processor, typename... As>
    static st::thread make(As&&... as) {
        return st::thread(context::make<OBJECT>(std::forward<As>(as)...));
    }

    //--------------------------------------------------------------------------
    // state
   
    /** 
     * @return context pointer 
     */
    inline void* get() {
        return m_context.get();
    }
    
    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() const {
        return m_context ? true : false;
    }

    /**
     * @return `true` if argument thread represents this thread (or no thread), else `false`
     */
    inline bool operator==(const st::thread& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    /**
     * @return `true` if argument thread represents this thread (or no thread), else `false`
     */
    inline bool operator==(st::thread&& rhs) const noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    /**
     * @return the `std::thread::id` of the system thread this `st::thread` is running on
     */
    inline std::thread::id get_id() const {
        return m_context ? m_context->get_thread_id() : std::thread::id();
    }

    /**
     * This static function is intended to be called from within an `OBJECT` 
     * running in an `st::thread`.
     *
     * @return a copy of the `st::thread` currently running on the calling thread, if none is running will return an unallocated `st::thread`
     */
    static inline st::thread self() {
        return st::thread(context::tl_self().lock());
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
     * Any threadpool child `st::thread`s of this `st::thread` will also have 
     * `st::thread::shutdown(process_remaining_messages)` called on them.
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
     * `st::thread`'s `OBJECT` `recv()` method.
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
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    inline bool schedule(std::function<void()> f) {
        return schedule(task(std::move(f)));
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
        return schedule(task([=]() mutable { f(std::forward<As>(as)...); }));
    }

    /**
     * @brief wrap user function and arguments then schedule them on a different, dedicated system thread and send the result of the operation back to this `st::thread`
     *
     * @param resp_id id of message that will be sent back to the this `st::thread` when `std::async` completes 
     * @param f function to execute on another system thread
     * @param as arguments for argument function
     */
    template <typename F, typename... As>
    void async(std::size_t resp_id, F&& f, As&&... as) {
        m_context->m_ch.async(resp_id, std::forward<F>(f), std::forward<As>(as)...);
    }

private:
    /*
     * Generic function wrapper for executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type. Is 
     * a new definition instead of a typedef so that it can be distinguished by 
     * receiving code.
     */
    struct task : public std::function<void()> { 
        template <typename... As>
        task(As&&... as) : std::function<void()>(std::forward<As>(as)...) { }
    };

    struct context : public std::enable_shared_from_this<context> {
        context() : m_shutdown(false), m_ch(channel::make()) { }
        ~context() { shutdown(false); }
       
        // static thread thread constructor function
        template <typename OBJECT, typename... As>
        static std::shared_ptr<context> make(As&&... as) {
            std::shared_ptr<context> c(new context);
            c->launch_thread<OBJECT>(std::forward<As>(as)...);
            return c;
        }

        // thread local data
        static std::weak_ptr<context>& tl_self();
       
        // looping recv function executed by a root thread
        void thread_loop(const std::shared_ptr<context>& self, 
                         const std::function<void()>& do_late_init);

        // construct a thread running on a dedicated system thread
        template <typename OBJECT, typename... As>
        void launch_thread(As&&... as) {
            std::shared_ptr<context> self = shared_from_this();
            m_self = self;

            std::function<void()> do_late_init = [=]() mutable { 
                late_init<OBJECT>(std::forward<As>(as)...); 
            };

            std::thread thd([=]{ this->thread_loop(self, do_late_init); });

            m_thread_id = thd.get_id();
            thd.detach();
        }

        /*
         * Finish initializing the `st::thread` by allocating the `OBJECT` object 
         * and related handlers. Should be called on the scheduled parent 
         * `st::thread`.
         */
        template <typename OBJECT, typename... As>
        void late_init(As&&... as) {
            // properly set the thread_local self `st::thread` before `OBJECT` construction
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

        inline bool running() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_shutdown;
        }
    
        void shutdown(bool process_remaining_messages);

        inline bool send(message&& msg) {
            return m_ch.send(std::move(msg));
        }

        inline bool schedule(task&& t) {
            std::lock_guard<std::mutex> lk(m_mtx);
            return send(message::make(0,std::move(t)));
        }

        inline std::thread::id get_thread_id() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_thread_id;
        }

        mutable std::mutex m_mtx;
        bool m_shutdown;
        channel m_ch; // internal thread channel
        std::weak_ptr<context> m_self; // weak pointer to self
        std::thread::id m_thread_id; // thread id the user object is executing on
        data m_object; // user object data
        std::function<void(message&)> m_hdl; // function wrapper utilizing user object to parse messages 
    };

    thread(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    std::shared_ptr<context> m_context;
};

/**
 * @brief writes a textual representation of a thread to the output stream
 * @return a reference to the argument ostream reference
 */
template<class CharT, class Traits>
std::basic_ostream<CharT,Traits>&
operator<<(std::basic_ostream<CharT,Traits>& ost, st::thread thd) {
    std::stringstream ss;
    ss << std::hex << thd.get();
    ost << ss.str();
    return ost;
}

}

#endif
