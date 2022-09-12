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
#include <string>
#include <vector>

namespace st { // simple thread

//******************************************************************************
// UTILITIES

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

namespace detail {
/*
 * A utility struct which will store the current value of an argument reference,
 * and restore that value to said reference when this object goes out of scope.
 */
template <typename T>
struct hold_and_restore {
    hold_and_restore() = delete; // cannot create empty value
    hold_and_restore(const hold_and_restore&) = delete; // cannot copy
    hold_and_restore(hold_and_restore&& rhs) = delete; // cannot move
    hold_and_restore(T& t) : m_ref(t), m_old(t) { }
    ~hold_and_restore() { m_ref = m_old; }
    
    T& m_ref;
    T m_old;
};
}

//******************************************************************************
// INHERITABLE INTERFACES & CORE DATA TYPES

/**
 * Type erased data container. Its purpose is similar to c++17 `std::any` but is 
 * backwards compatible to c++11.
 *
 * In practice, this is a wrapper around a std::unique_ptr<void,DELETER> that 
 * manages and sanitizes memory allocation.
 */
struct data {
    data() : m_data(nullptr, data::no_delete), m_type_code(0) { }
    data(const data& rhs) = delete; // cannot copy unique_ptr

    data(data&& rhs) : 
        m_data(std::move(rhs.m_data)), 
        m_type_code(rhs.m_type_code) 
    { }

    /**
     * @brief type deduced constructor
     */
    template <typename T>
    data(T&& t) : data(hint<T>(), std::forward<T>(t)) { }

    /**
     * @brief construct a data payload using explicit template typing instead of by deduction
     *
     * This function is the most flexible way to construct data, as it does not 
     * rely on being given a pre-constructed payload first and can invoke any 
     * arbitrary constructor for type T based on arguments `as`.
     *
     * @param as optional constructor parameters 
     * @return an allocated data object
     */
    template <typename T, typename... As>
    static data make(As&&... as) {
        return data(hint<T>(), std::forward<As>(as)...);
    }

    /**
     * @return the stored compiler derived type code
     */
    inline const std::size_t type_code() const {
        return m_type_code;
    }
   
    /**
     * @brief determine at runtime whether the type erased data type code matches the templated type code.
     *
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
     * `move_to<T>()`, which include a type check internally.
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
        data_pointer_t(allocate<T>(std::forward<As>(as)...),data::deleter<T>)
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
    const std::size_t m_type_code;
};

/**
 * @brief Interthread type erased message container 
 *
 * This object is *not* mutex locked beyond what is included in the 
 * `std::shared_ptr` implementation.
 */
struct message {
    message(){}
    message(const message& rhs) : m_context(rhs.m_context) { }
    message(message&& rhs) : m_context(std::move(rhs.m_context)) { }

    inline message& operator=(const message& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    inline message& operator=(message&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

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
            new context(static_cast<std::size_t>(id),
                type_code<T>(),
                std::forward<T>(t))));
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

    std::shared_ptr<context> m_context;
};

/**
 * @brief interface for a class with a controllable lifecycle
 * 
 * Operations are "const" to ensure that lambda captures are trivial. Because of 
 * this implementors of `sender` must mark all members used in the 
 * implementation of this function as mutable or const.
 */
struct lifecycle {
    /**
     * @return `true` if the object is running, else `false`
     */
    virtual bool running() const = 0;

    /** 
     * @brief shutdown the object (running() == `false`)
     *
     * @param process_remaining_messages if true allow recv() to succeed until message queue is empty
     */
    virtual void shutdown(bool process_remaining_messages) const = 0;

    /**
     * @brief shutdown the object with default behavior
     */
    virtual void shutdown() const = 0;
};

/**
 * @brief interface for a class which will have messages sent to it 
 *
 * Implementing this interface provides the inheriting class with standard 
 * user api to send messages to it.
 * 
 * Operations are "const" to ensure that lambda captures are trivial. Because of 
 * this implementors of `sender` must mark all members used in the 
 * implementation of this function as mutable or const.
 */
struct sender {
    /**
     * @brief user implementation of message send 
     *
     * @param msg message to be sent to the object 
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    virtual bool send(message msg) const = 0;

    /**
     * @brief send a message with given parameters
     *
     * @param as arguments passed to `message::make()`
     * @return `true` on success, `false` on failure due to object being shutdown 
     * */
    template <typename... As>
    bool send(As&&... as) const {
        return send(message::make(std::forward<As>(as)...));
    }
};

//******************************************************************************
// INTERTHREAD CLASSES

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between managed 
 * system threads. Provided here as a convenience for communicating from managed 
 * system threads to other user `st::fiber`s. All methods in this object are 
 * mutex locked and threadsafe.
 *
 * Methods in this class are const in order to enable trivial lambda captures 
 * without having to always specify the lambda is mutable.
 */
struct channel {
    channel(){}
    channel(const channel& rhs) : m_context(rhs.m_context) { }
    channel(channel&& rhs) : m_context(std::move(rhs.m_context)) { }

    inline channel& operator=(const channel& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    inline channel& operator=(channel&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        return channel(std::shared_ptr<context>(new context));
    }

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() {
        return m_context ? true : false;
    }

    /// implement `bool lifecycle::running() const`
    inline bool running() const {
        return m_context->running();
    }

    /// implement `void lifecycle::shutdown(bool)`
    inline void shutdown(bool process_remaining_messages) const {
        m_context->shutdown(process_remaining_messages);
    }
    
    /// implement `void lifecycle::shutdown()`
    inline void shutdown() const {
        shutdown(true);
    }

    /** 
     * @return count of messages in the queue
     */
    inline std::size_t queued() const {
        return m_context->queued();
    }

    /**
     * @return count of `st::fiber`s blocked on `recv()`
     */
    inline std::size_t blocked_receivers() const {
        return m_context->blocked_receivers();
    }

    inline bool send(message msg) const {
        return m_context->send(std::move(msg));
    }

    /**
     * @brief optionally enqueue the argument message and receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return true on success, false if channel is shutdown
     */
    inline bool recv(message& msg) const {
        return m_context->recv(msg);
    }

private:
    struct context {
        struct blocker {
            inline void wait(std::unique_lock<std::mutex>& lk) {
                do {
                    cv.wait(lk);
                } while(!flag);
            }

            bool flag = false;
            std::condition_variable cv;
        };

        context() : m_shutdown(false) { }

        inline bool running() const { 
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_shutdown;
        }
        
        inline std::size_t queued() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_msg_q.size();
        }

        inline std::size_t blocked_receivers() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_recv_q.size();
        }

        inline void shutdown(bool process_remaining_messages) const {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_shutdown = true;

            if(!process_remaining_messages) {
                m_msg_q.clear();
            }

            for(auto& blk : m_recv_q) {
                blk->flag = true;
                blk->cv.notify_one();
            }
        }

        inline bool send(message msg) const {
            std::unique_lock<std::mutex> lk(m_mtx);

            if(m_shutdown) {
                return false;
            } else {
                m_msg_q.push_back(std::move(msg));
            }

            if(m_recv_q.size()) {
                auto blk = std::move(m_recv_q.front());
                blk->flag = true;
                m_recv_q.pop_front();
                lk.unlock();
                blk->cv.notify_one(); // notify outside of lock to limit mutex blocking
            }

            return true;
        }

        inline bool recv(message& msg) const {
            std::unique_lock<std::mutex> lk(m_mtx);

            // block until message is available or channel close
            while(m_msg_q.empty() && !m_shutdown) { 
                auto recv_blk = std::make_shared<blocker>();
                m_recv_q.push_back(recv_blk);
                recv_blk->wait(lk);
            }

            if(m_msg_q.empty()) { // no more messages to process, channel shutdown
                return false;
            } else {
                msg = std::move(m_msg_q.front());
                m_msg_q.pop_front();
                return true;
            } 
        }

        mutable bool m_shutdown;
        mutable std::mutex m_mtx;
        mutable std::deque<message> m_msg_q;
        mutable std::deque<std::shared_ptr<blocker>> m_recv_q;
    };

    channel(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    std::shared_ptr<context> m_context;
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
 * with user defined functors as a template argument. This is the case in 
 * these functions: 
 * `st::fiber::thread<FUNCTOR>(...)`
 * `st::fiber::launch<FUNCTOR>(...)`
 *
 * Type FUNCTOR should be a functor class. A functor is a class with call 
 * operator overload which accepts an argument message, ex:
 * ```
 * struct MyFunctor {
 *     void operator()(st::message m);
 * };
 * ```
 *
 * Message arguments can also be accepted by reference:
 * ```
 * struct MyFunctor {
 *     void operator()(st::message& m);
 * };
 * ```
 *
 * Note: `st::fibers`s automatically throw out any unallocated messages 
 * received over their internal channel.
 *
 * Functors can alternatively return an `st::message`. If the 
 * functor follows this pattern any returned non-null message will be 
 * requeued for future processing:
 * ```
 * struct MyFunctor {
 *     st::message operator()(st::message m);
 * };
 * ```
 *
 * Or again by reference:
 * ```
 * struct MyFunctor {
 *     st::message operator()(st::message& m);
 * };
 * ```
 *
 * Care must be taken with the above pattern, as it can create an implicit
 * infinite processing loop if the operator never returns a non-null 
 * message.
 *
 * Functors are useful because they allow member data to persist between 
 * calls to `operator()()` without any additional management and for all 
 * member data to be easily accessible without dealing with global variables 
 * or void* pointers.
 *
 * Another distinct advantage is functors are able to make intelligent use 
 * of C++ RAII semantics, as the functor can specify constructors and 
 * destructors as normal. In fact, the user's FUNCTOR is constructed on the 
 * target `st::fiber`'s thread, instead of the calling thread, allowing for 
 * `thread_local` data to be safely used by the user FUNCTOR.
 *
 * If an `st::fiber` running in a non-blocking fashion has no more messages 
 * to receive, it will suspend itself until new messages are sent to it via 
 * `st::fiber::send()` or `st::fiber::schedule<FUNCTOR>()`, whereupon it will 
 * resume processing messages.
 *
 * Because `st::fiber`s are designed to run in a non-blocking fashion, any 
 * message whose processing blocks an `st::fiber` indefinitely can cause all 
 * sorts of bad effects, including deadlock. In a scenario where a long running 
 * or indefinite call needs to be made, it may be better to call `std::async` 
 * to execute the function on a dedicated system thread, then send a message 
 * back to the originating fiber with the result:
 * ```
 * struct MyFiber {
 *     enum op {
 *         // ...
 *         handle_long_running_result,
 *         // ...
 *     };
 *
 *     void operator()(message msg) {
 *         switch(msg.id()) {
 *             // ...
 *                 // need to make a long running call 
 *                 // get a copy of the currently running fiber
 *                 st::fiber self = st::fiber::local::self();
 *                 // launch a thread with a default functor and schedule the call
 *                 std::async([self]{ // capture a copy of the fiber
 *                     auto my_result = execute_long_running_call();
 *                     self.send(op::handle_long_running_result, my_result);
 *                 }); 
 *                 break;
 *             case op::handle_long_running_call_result: 
 *                 // ...
 *                 // do something with my_result
 *                 break;
 *             // ...
 *         }
 *     }
 * };
 * ```
 */
struct fiber : public lifecycle, public sender {
    /**
     * Generic FUNCTOR which only processes messages sent via `schedule()` API, 
     * ignoring all other messages.
     */
    struct processor {
        inline void operator()(message& msg) { }
    };

    fiber(){}
    fiber(const fiber& rhs) : m_context(rhs.m_context) { }
    fiber(fiber&& rhs) : m_context(std::move(rhs.m_context)) { }

    virtual ~fiber() {
        // explicitly shutdown root fiber channel because a system thread 
        // holds a copy of this fiber which keeps the channel alive even 
        // though the root fiber is no longer reachable
        if(m_context && 
           m_context.get() == root().m_context.get() &&
           m_context.use_count() < 3) {
            shutdown();
        }
    }

    inline fiber& operator=(const fiber& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    inline fiber& operator=(fiber&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

    inline void* get() {
        return m_context.get();
    }

    /**
     * @return `true` if argument fiber represents this fiber (or no fiber), else `false`
     */
    inline bool operator==(fiber rhs) noexcept {
        return m_context.get() == rhs.m_context.get();
    }

    //--------------------------------------------------------------------------
    // allocation 

    /**
     * @brief construct and launch a system thread running user FUNCTOR as the blocking root `st::fiber`
     *
     * `st::fiber`'s FUNCTOR will be allocated on the scheduled thread, not the 
     * calling thread. This allows usage of `thread_local` data where necessary.
     *
     * @param as optional arguments to the constructor of type FUNCTOR
     */
    template <typename FUNCTOR=processor, typename... As>
    static fiber thread(As&&... as) {
        return fiber(context::thread<FUNCTOR>(std::forward<As>(as)...));
    }

    /**
     * @brief construct and schedule a new `st::fiber` created with user FUNCTOR on another running `st::fiber`
     *
     * The returned `st::fiber` will execute in a non-blocking, cooperative 
     * fashion on the target `st::fiber`.
     *
     * `st::fiber`'s FUNCTOR will be allocated on the scheduled thread, not the 
     * calling thread. This allows usage of `thread_local` data where necessary.
     *
     * @param as optional arguments to the constructor of type FUNCTOR
     * @return allocated `st::fiber` if successfully launched, else empty pointer
     */
    template <typename FUNCTOR=processor, typename... As>
    fiber launch(As&&... as) {
        return fiber(m_context->launch<FUNCTOR>(std::forward<As>(as)...));
    }

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() {
        return m_context ? true : false;
    }

    //--------------------------------------------------------------------------
    // lifecycle 

    /// implement `bool lifecycle::running() const`
    inline bool running() const {
        return m_context->running();
    }

    /// implement `void lifecycle::shutdown(bool)`
    inline void shutdown(bool process_remaining_messages) const {
        m_context->shutdown(process_remaining_messages);
    }
    
    /// implement `void lifecycle::shutdown()`
    inline void shutdown() const {
        shutdown(true);
    }

    //--------------------------------------------------------------------------
    // state

    /**
     * @return the `std::thread::id` of the system thread this `st::fiber` is running on
     */
    inline std::thread::id get_id() const {
        return m_context ? m_context->get_thread_id() : std::thread::id();
    }

    /**
     * @brief return the parent `st::fiber` of this instance
     * @return allocated `st::fiber` if this object has a parent, else the root `st::fiber`
     */
    inline fiber parent() const {
        return fiber(m_context->parent());
    }

    /**
     * @brief return the root `st::fiber` of this instance's executing system thread
     * @return allocated root `st::fiber` which manages this instance's system thread
     */
    inline fiber root() const {
        return fiber(m_context->root());
    }

    /**
     * namespace for thread_local `st::fiber` data
     */
    struct local {
        /**
         * @return a copy of the `st::fiber` currently running on the calling thread, if none is running will return nullptr
         */
        static inline fiber self() {
            return fiber(context::tl_self().lock());
        }

        /**
         * @return a copy of the parent of the `st::fiber` executing on the calling thread
         */
        static inline fiber parent() {
            auto self = local::self();
            return self ? self.parent() : self;
        }

        /**
         * @return a copy of the root `st::fiber` executing on the calling thread
         */
        static inline fiber root() {
            auto self = local::self();
            return self ? self.root() : self;
        }
    };

    /**
     * @brief class describing the workload of an `st::fiber`
     *
     * Useful for comparing relative `st::fiber` workloads when scheduling.
     */
    struct weight {
        /// represents count of queued messages on a `st::fiber`
        std::size_t queued;

        /// represents if a `st::fiber` is currently processing a message
        bool executing;

        /**
         * @return true if the weight is 0, else false
         */
        inline bool empty() const {
            return !(queued || executing);
        }

        /**
         * @return true if this weight is lighter than the other, else false
         */
        inline bool operator<(const weight& rhs) const {
            if(queued < rhs.queued) {
                return true;
            } else if(queued > rhs.queued) {
                return false;
            } else if(!executing && rhs.executing) {
                return true;
            } else {
                return false;
            }
        }
    };

    /**
     * @return weight representing workload of `st::fiber`
     */
    inline weight workload() const {
        return m_context->workload();
    }

    /**
     * @brief abstract the weight comparison between two fibers 
     * @return `true` if this fiber has lighter workload than the other, else `false`
     */
    inline bool operator<(const fiber& rhs) const {
        return workload() < rhs.workload();
    }

    //--------------------------------------------------------------------------
    // communication

    /// implement `bool sender::send(message) const`
    inline bool send(message msg) const {
        return m_context->send(std::move(msg));
    }

    /**
     * @brief schedule a generic task for execution 
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param t std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    inline bool schedule(std::function<void()> f) const {
        return m_context->schedule(task(std::move(f)));
    }

    /**
     * @brief wrap user function and arguments then schedule as a generic task for execution
     *
     * @param f function to execute on target sender 
     * @param as arguments for argument function
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    template <typename F, typename... As>
    bool schedule(F&& f, As&&... as) const {
        return schedule(task([=]() mutable { f(std::forward<As>(as)...); }));
    }

private:
    typedef std::function<message(message&)> handler;

    /*
     * Generic function wrapper for scheduling and executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type. 
     */
    struct task { 
        task(){}
        task(const task& rhs) : m_hdl(rhs.m_hdl) { }
        task(task&& rhs) : m_hdl(std::move(rhs.m_hdl)) { }
        task(const std::function<void()>& rhs) : m_hdl(rhs) { }
        task(std::function<void()>&& rhs) : m_hdl(std::move(rhs)) { }

        // Template type `F` should be a Callable function or functor. 
        template <typename F, typename... As>
        task(F&& f, As&&... as) : 
            m_hdl([=]() mutable { f(std::forward<As>(as)...); })
        { }

        // return an `true` if re-queueing is required, else false
        inline bool operator()() {
            m_complete = true;
            detail::hold_and_restore<bool*> har(tl_complete()); 
            tl_complete() = &m_complete;
            m_hdl();
            return m_complete;
        }

        // Modify the completion state of task object running on the calling thread.
        inline static void complete(bool val) {
            *tl_complete() = val;
        }

    private:
        static inline bool*& tl_complete() {
            thread_local bool* c=nullptr;
            return c;
        }

        bool m_complete = true;
        std::function<void()> m_hdl;
    };

    struct context : public std::enable_shared_from_this<context> {
        context(std::weak_ptr<context> root = std::weak_ptr<context>(), 
                std::weak_ptr<context> parent = std::weak_ptr<context>(),
                std::thread::id thread_id = std::thread::id()) : 
            m_ch(channel::make()),
            m_blocked(true),
            m_root(root),
            m_parent(parent),
            m_thread_id(thread_id)
        { 
            m_self = shared_from_this();
        }
        
        template <typename FUNCTOR, typename... As>
        static std::shared_ptr<context> thread(As&&... as) {
            std::shared_ptr<context> c(new context);
            c->launch_root_thread<FUNCTOR>(std::forward<As>(as)...);
            return c;
        }

        static inline std::weak_ptr<context>& tl_self() {
            thread_local std::weak_ptr<context> wp;
            return wp;
        }

        static inline std::weak_ptr<context>& tl_root() {
            thread_local std::weak_ptr<context> wp;
            return wp;
        }

        // sub handler for functors returning message
        template <typename FUNCTOR>
        static message execute_functor(std::true_type, FUNCTOR& f, message& msg) {
            return f(msg); 
        }

        // sub handler for all other functors
        template <typename FUNCTOR>
        static message execute_functor(std::false_type, FUNCTOR& f, message& msg) {
            f(msg);
            return message();
        }

        template <typename FUNCTOR, typename... As>
        std::shared_ptr<context> launch(As&&... as) {
            std::shared_ptr<context> c = new context(root(), 
                                                     m_self.lock(), 
                                                     get_thread_id());
            return schedule(task([=]() mutable {
                c->late_init<FUNCTOR>(std::forward<As>(as)...);
                c->parent()->schedule(c->nonblocking_fiber_task());
            })) ? c : std::shared_ptr<context>();
        }

        template <typename FUNCTOR, typename... As>
        void launch_root_thread(As&&... as) {
            std::shared_ptr<context> self = m_self.lock();
            m_root = self;
            m_parent = self;

            std::lock_guard<std::mutex> lk(m_mtx);

            std::thread thd([&,self]{
                {
                    // block until lock is released by spawning thread
                    std::lock_guard<std::mutex> lk(m_mtx);
                }

                // finish initialization
                detail::hold_and_restore<std::weak_ptr<context>> root_har(tl_root());
                detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
                tl_root() = self;
                tl_self() = self;
                late_init<FUNCTOR>(std::forward<As>(as)...); 

                // block thread and listen for messages in a loop
                message msg;

                std::unique_lock<std::mutex> lk(m_mtx);

                while(m_ch.recv(msg) && msg) { 
                    lk.unlock();
                    msg = m_hdl(msg);

                    if(msg) {
                        m_ch.send(msg);
                    }
                    lk.lock();
                }
            });

            m_thread_id = thd.get_id();
            thd.detach();
        }

        /*
         * Finish initializing the `st::fiber` by allocating the FUNCTOR object 
         * and related handlers. Should be called on the scheduled parent 
         * `st::fiber`.
         */
        template <typename FUNCTOR, typename... As>
        void late_init(As&&... as) {
            // properly set the `st::fiber` before `FUNCTOR` construction
            detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
            tl_self() = m_self.lock();

            // construct the `FUNCTOR`
            std::shared_ptr<FUNCTOR> f(new FUNCTOR(std::forward<As>(as)...));

            // set the `st::fiber`s handler
            m_hdl = [f](message& msg) -> message {
                using ismsg = std::is_same<message,decltype((*f)(msg))>;

                if(msg.data().is<task>()) {
                    msg.data().cast_to<task>()();
                    return message();
                } else {
                    return context::execute_functor(
                        std::integral_constant<bool,ismsg::value>(), 
                        *f, 
                        msg);
                }
            };
        }

        /*
         * Conversion to `task` is private so only `st::fiber::launch()` 
         * can access it, to avoid the possibility of double scheduling.
         */
        inline task nonblocking_fiber_task() const {
            // hold a single copy of self to keep memory allocated during processing
            std::shared_ptr<context> self = m_self.lock();
            return task([&,self]() mutable { 
                message msg;
                detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
                tl_self() = self;

                std::unique_lock<std::mutex> lk(m_mtx);

                // receive once if we will not block
                if(m_ch.queued() && m_ch.recv(msg) && msg) { 
                    lk.unlock();
                    msg = m_hdl(msg);

                    if(msg) {
                        m_ch.send(msg);
                    }
                    lk.lock();
                } 

                if(m_ch.queued() || m_ch.running()) {
                    m_blocked = !m_ch.queued(); 
                    task::complete(!m_blocked);
                }
            });
        }

        inline bool running() const {
            return m_ch.running();
        }
    
        inline void shutdown(bool process_remaining_messages) const {
            m_ch.shutdown(process_remaining_messages);
        }

        inline bool send(message msg) const {
            bool r = m_ch.send(std::move(msg));

            if(r) {
                std::lock_guard<std::mutex> lk(m_mtx);
                if(m_blocked) {
                    std::shared_ptr<context> parent = m_parent.lock();

                    if(parent) {
                        r = parent->schedule(nonblocking_fiber_task());
                        if(r) {
                            m_blocked = false;
                        } // else parent has been shutdown
                    } else { 
                        // else this fiber has not been run yet
                        r = false;
                    }
                } // else fiber is already running 
            } // else fiber is shutdown

            return r;
        }

        inline bool schedule(task t) const {
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

        inline weight workload() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return weight{
                m_ch.queued(), 
                m_ch.blocked_receivers() ? false : !m_blocked
            };
        }

        mutable std::mutex m_mtx;
        mutable channel m_ch;
        mutable bool m_blocked;
        mutable std::weak_ptr<context> m_self; // weak pointer to self
        mutable std::weak_ptr<context> m_root; // weak pointer to root fiber
        mutable std::weak_ptr<context> m_parent; // weak pointer to parent fiber
        mutable std::thread::id m_thread_id;
        mutable handler m_hdl; // function wrapper utilizing functor to parse messages 
    };

    fiber(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    std::shared_ptr<context> m_context;
};

/**
 * @brief writes a textual representation of a fiber identifier id to the output stream ost.
 */
template<class CharT, class Traits>
std::basic_ostream<CharT,Traits>&
operator<<(std::basic_ostream<CharT,Traits>& ost, fiber f) {
    std::stringstream ss;
    ss << std::hex << f.get();
    ost << ss.str();
    return ost;
}

/**
 * @brief jointly manage the lifecycle of multiple `st::fiber`s
 *
 * When the last shared `st::weave` context managed is destroyed, all 
 * `st::fiber`s managed by the `st::weave`'s context will be shutdown with
 * `st::fiber::shutdown()`.
 *
 * This is a useful object for storing arbitrary fibers that should be 
 * cleanly shutdown together.
 */
struct weave : public lifecycle {
    weave(){}
    weave(const weave& rhs) : m_context(rhs.m_context) { }
    weave(weave&& rhs) : m_context(std::move(rhs.m_context) { }
    
    virtual ~weave() { }

    inline weave& operator=(const weave& rhs) {
        m_context = rhs.m_context;
        return *this;
    }

    inline weave& operator=(weave&& rhs) {
        m_context = std::move(rhs.m_context);
        return *this;
    }

    /**
     * @brief make an `st::weave` with allocated shared context to manage multiple argument `st::fiber`s
     *
     * @param fs optional list of fibers 
     * @return an allocated `st::weave` managing argument fibers
     */
    template <typename... Fs>
    static weave make(Fs&&... fs) {
        std::vector<fiber> fibers{ std::forward<Fs>(fs)... };
        return weave(std::shared_ptr<context>(new context(std::move(fibers)));
    }

    /**
     * @return an approximation of maximum concurrent threads on the executing hardware
     */
    static std::size_t concurrency() const {
        std::size_t count = std::thread::hardware_concurrency();
        return count ? count : 1; // enforce at least 1
    }

    /**
     * @brief construct a weave of a prescribed number of newly created `fiber` system threads 
     *
     * This is useful for generating an efficient multi-thread executor for 
     * `fiber::schedule()`ing tasks or `fiber::launch()`ing new `fiber`s by 
     * executing those functions on the fiber returned with
     * `st::weave::select()`.
     *
     * A default attempt at generating an executor with maximal processing 
     * throughput can be acquired by calling this function with all default 
     * arguments:
     * `st::weave::threadpool<>()`
     *
     * @param count the number of worker `fiber` threads in the pool 
     * @param as optional arguments for type `FUNCTOR`
     * @return a weave with count number of worker `fiber` system threads
     */
    template <typename FUNCTOR=fiber::processor, typename... As>
    static weave threadpool(std::size_t count=weave::concurrency(), As&&... as) {
        std::vector<fiber> fibers(count);

        for(auto& f : fibers) {
            f = fiber::thread<FUNCTOR>(std::forward<As>(as)...);
        }

        return weave(std::shared_ptr<context>(new context(std::move(fibers)));
    }

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() {
        return m_context ? true : false;
    }

    /// implement `bool lifecycle::running() const`
    inline bool running() const {
        return m_context->running();
    }

    /// implement `void lifecycle::shutdown(bool)`
    inline void shutdown(bool process_remaining_messages) const {
        m_context->shutdown(process_remaining_messages);
    }
    
    /// implement `void lifecycle::shutdown()`
    inline void shutdown() const {
        shutdown(true);
    }

    /**
     * @return a copy of the fibers managed by this `st::weave`
     */
    inline std::vector<fiber> fibers() const {
        std::lock_guard<std::mutex> lk(m_context->m_mtx);
        return m_context->m_fibers;
    }

    /**
     * @return the count of the `st::fiber`s managed by this object
     */
    inline std::size_t count() const {
        return m_context->count()
    }

    /**
     * @return the count of the `st::fiber`s managed by this object
     */
    inline std::size_t size() const {
        return count();
    }

    /**
     * @param index address of fiber 
     * @return fiber stored at index
     */
    inline fiber operator[](std::size_t index) const {
        return m_context->get(index);
    }

    /**
     * @brief append one or more fibers to the weave 
     * @param f the first fiber to append 
     * @param fs optional additional fibers to append 
     */
    template <typename... Fs>
    void append(fiber f, Fs&&... fibers) {
        constexpr std::size_t fiber_count = 1 + sizeof...(fibers);
        m_context->append<fiber_count>(f, std::forward<Fs>(fibers)...);
    }

    /**
     * @brief acquire a relatively light workload fiber selected via a best-effort algorithm 
     *
     * `st::weave` will use a constant time algorithm to select an 
     * `st::fiber` with a relatively light workload. This is useful when a 
     * `st::weave` represents a group of worker fibers for processing 
     * arbitrary messages and scheduling arbitrary code for execution, such 
     * as `weave`s produced by a call to `st::weave::pool()`.
     *
     * If any managed `st::fiber` is shutdown early (had 
     * `st::fiber::shutdown()` called on it before the managing 
     * `st::weave` has gone out of scope) then this function may 
     * cause operations on the selected fiber to unexpectedly fail.
     *
     * @return the selected fiber
     */
    inline fiber select() const {
        return m_context->select();
    }

private:
    struct context {
        template <typename... As>
        context(As as...) : 
            m_fibers(as...),
            m_cur_idx(0)
        { }

        // when the last weave::context goes out of scope shutdown child fibers
        ~context() {
            shutdown();
        }

        inline bool running() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_fibers.size();
        }
    
        inline void shutdown(bool process_remaining_messages) const {
            std::vector<fiber> fibers;

            {
                std::lock_guard<std::mutex> lk(m_mtx);
                fibers = m_fibers;
                m_fibers.clear();
                m_cur_idx = 0;
            }

            // shutdown fibers outside of lock scope to prevent deadlock
            for(auto f : fibers) {
                f.shutdown(process_remaining_messages);
            }
        }

        inline std::size_t count() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_fibers.size();
        }

        inline fiber get(std::size_t index) const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_fibers[index];
        }

        template <typename... Fs>
        void inner_append(fiber& f) const {
            m_fibers.push_back(f);
        }

        template <typename... Fs>
        void inner_append(fiber& f, fiber& f2, Fs&&... fibers) const {
            m_fibers.push_back(f);
            append(f2, std::forward<Fs>(fibers)...);
        }

        template <std::size_t fiber_count, typename... Fs>
        void append(fiber& f, Fs&&... fibers) const {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_fibers.reserve(fiber_count + m_fibers.size());
            inner_append(f, std::forward<Fs>(fibers)...);
        }

        inline fiber select() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            std::size_t sz = m_fibers.size();

            if(sz > 1) {
                auto& prev_fiber = m_fibers[m_cur_idx];
                ++m_cur_idx;

                // if at the end of the vector return to the first entry
                if(m_cur_idx >= sz) {
                    m_cur_idx = 0;
                } 

                auto& cur_fiber = m_fibers[m_cur_idx];
                return prev_fiber < cur_fiber ? prev_fiber : cur_fiber;
            } else if(sz == 1) {
                return m_fibers[0];
            } else {
                return fiber();
            }
        }

        mutable std::mutex m_mtx;
        mutable std::vector<fiber> m_fibers;
        mutable std::size_t m_cur_idx;
    };
    
    weave(std::shared_ptr<context> ctx) : m_context(std::move(ctx)) { }
    std::shared_ptr<context> m_context;
};

}

#endif
