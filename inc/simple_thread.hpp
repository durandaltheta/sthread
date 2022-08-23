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

namespace st { // simple thread

//******************************************************************************
// UTILITIES

/**
 * @brief convenience and readability aid type alias to `std::shared_ptr`
 */
template <typename T>
using sptr = std::shared_ptr<T>;

/**
 * @brief convenience and readability aid type alias to `std::weak_ptr`
 */
template <typename T>
using wptr = std::weak_ptr<T>;

/**
 * @brief convenience and readability aid type alias to `std::unique_ptr`
 */
template <typename T>
using uptr = std::unique_ptr<T>;

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
    return typeid(base<T>).hash_type_code();
}

/**
 * A utility struct which will store the current value of an argument reference,
 * and restore that value to said reference when this object goes out of scope.
 */
template <typename T>
struct hold_and_restore {
    hold_and_restore() = 0; // cannot create empty value
    hold_and_restore(const hold_and_restore&) = 0; // cannot copy
    hold_and_restore(hold_and_restore&& rhs) : m_ref(rhs.m_ref), m_old(rhs.m_old) { }
    hold_and_restore(T& t) : m_ref(t), m_old(t) { }
    ~hold_and_restore() { m_ref = m_old; }
    
    T& m_ref;
    T m_old;
};

//******************************************************************************
// INHERITABLE INTERFACES & DATA TYPES

/**
 * @brief convenience inheritable type for determining type erased value at runtime 
 *
 * Inheriting this class enables usage of `is<T>()` to compare the inheriting 
 * class's type erased type at runtime to any type `T`.
 */
struct type_aware {
    type_aware() : m_type_code(0) { }
    type_aware(const std::size_t type_code) : m_type_code(type_code) { }

    virtual ~type_aware(){}

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
        return m_type_code == type_code<T>();
    }

protected: 
    const std::size_t m_type_code;
};

/**
 * Convenience CRTP interface enabling type conversions for objects which only 
 * exist in shared pointer form. IE, their constructors are private so they must 
 * be constructed with static `make()` functions which return the objects as 
 * shared pointers.
 */
template <typename CRTP>
struct self_aware {
    /**
     * Useful when a shared pointer to an object which implements `self_aware` 
     * is required but only a reference or raw pointer to that object is 
     * available.
     *
     * @return shared pointer to self
     */
    inline sptr<CRTP> self() {
        returm m_self.lock();
    }

    /**
     * @brief dynamically cast an object to desired type
     *
     * This is useful when working with standardized APIs. For instance, objects 
     * in this library that are `self_aware` also typically implement the 
     * `sender` interface:
     * `st::channel::cast_to<st::sender>()`
     * `st::fiber::cast_to<st::sender>()`
     * `st::executor::cast_to<st::sender>()`
     *
     * Allowing type agnostic APIs can be constructed to accept a 
     * `st::sptr<st::sender>`.
     * 
     * @return dynamically cast shared pointer from self to target type
     */
    template <typename T>
    inline sptr<T> cast_to() {
        return std::dynamic_pointer_cast<T>(m_self.lock());
    }

protected:
    // implementing object is responsible for assigning this value when their 
    // `make()` is called
    wptr<CRTP> m_self; 
}

/**
 * @brief interface representing an object with a managed lifecycle in this library 
 *
 * Implementing this interface provides the inheriting class with standard 
 * user api to manage the lifecycle of the object.
 */
struct lifecycle_aware {
    // when the inheriting class is destroyed, it should be shutdown
    virtual ~lifecycle_aware() { 
        shutdown(); 
    }

    /**
     * @return `true` if the object is running, else `false`
     */
    virtual bool running() = 0;

    /** 
     * @brief shutdown the object
     *
     * @param process_remaining_messages if true allow recv() to succeed until message queue is empty
     */
    virtual void shutdown(bool process_remaining_messages) = 0;

    /**
     * @brief shutdown the object with default behavior
     */
    virtual void shutdown() { 
        shutdown(false); 
    }
};

/**
 * Type erased data container. Its purpose is similar to c++17 `std::any` but is 
 * backwards compatible to c++11.
 *
 * In practice, this is a wrapper around a std::unique_ptr<void,DELETER> that 
 * manages and sanitizes memory allocation.
 */
struct data : public type_aware {
    data() : data_pointer_t(nullptr, data::no_delete) { }
    data(const data& rhs) = delete; // cannot copy unique_ptr

    data(data&& rhs) : 
        m_data(std::move(rhs.m_data)), 
        type_aware(rhs.type_code()) 
    { }
    
    explicit data(const char* s) : 
        data_pointer_t(allocate<T>(std::string(s)),
                       data::deleter<std::string>),
        type_aware(type_code<std::string>())
    { }
    
    explicit data(char* s) : 
        data_pointer_t(allocate<T>(std::string(s)),
                       data::deleter<std::string>),
        type_aware(type_code<std::string>())
    { }

    template <typename T>
    data(T&& t) : 
        data_pointer_t(allocate<T>(std::forward<T>(t)),data::deleter<T>),
        type_aware(type_code<T>())
    { }

    /**
     * @brief cast message data payload to templated type reference 
     *
     * NOTE: this function is *NOT* type checked. A successful call to
     * `is<T>()` (inherited from `type_aware`) is required before casting to 
     * ensure type safety. It is typically better practice and generally safer 
     * to use `copy_to<T>()` or `move_to<T>()`, which include a type check 
     * internally.
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
    typedef uptr<void,deleter_t> data_pointer_t;

    template <typename T>
    static void* allocate(T&& t) {
        return (void*)(new base<T>(std::forward<T>(t)));
    }

    template <typename T>
    static void deleter(void* p) {
        delete (base<T>*)p;
    }

    static inline void no_delete(void* p) { }
    data_pointer_t m_data;
};

/**
 * @brief Interthread type erased message container 
 *
 * This object is *not* mutex locked beyond what is included in the 
 * `sptr` implementation.
 */
struct message : protected self_aware<message> {
    /** 
     * @brief convenience function for templating 
     * @param msg message object to immediately return 
     * @return message object passed as argument
     */
    static inline sptr<message> make(sptr<message> msg) {
        return std::move(msg);
    }

    /**
     * @brief construct a message
     *
     * This is a sanity overload for handling people sending char arrays by 
     * deep copying the array into an std::string.
     *
     * @param id an unsigned integer representing which type of message
     * @param s c-string to be sent as an std::string
     * @return an allocated message
     */
    template <typename ID>
    static sptr<message> make(ID id, const char* s) {
        sptr<message> m(new message(
            static_cast<std::size_t>(id),
            type_code<std::string>(),
            s));
        m->m_self = m;
        return m;
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @param t arbitrary typed data to be stored as the message data 
     * @return an allocated message
     */
    template <typename ID, typename T>
    static sptr<message> make(ID id, T&& t) {
        sptr<message> m(new message(
            static_cast<std::size_t>(id),
            type_code<T>(),
            std::forward<T>(t)));
        m->m_self = m;
        return m;
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @return an allocated message
     */
    template <typename ID>
    static sptr<message> make(ID id) {
        sptr<message> m(new message(static_cast<std::size_t>(id), 0));
        m->m_self = m;
        return m;
    }

    /**
     * @brief an unsigned integer representing message's intended operation
     *
     * An `id` can trivially represent an enumeration, which can represent a 
     * specific request, response, or notification operation.
     */
    const std::size_t id;

    /**
     * @brief optional type erased payload data
     */
    data data;

private:
    message() = delete;
    message(const message& rhs) = delete;
    message(message&& rhs) = delete;
    
    message(const std::size_t c) : m_id(c) { }

    template <typename T>
    message(const std::size_t c, T&& t) :
        m_id(c),
        data(std::forward<T>(t))
    { }
};

/**
 * @brief interface for a class which will have messages sent to it 
 *
 * Implementing this interface provides the inheriting class with standard 
 * user api to send messages to it.
 */
struct sender {
    /**
     * @brief user implementation of message send 
     * 
     * Operation is "const" to ensure that lambda captures are easy. Otherwise 
     * the user would have to make all lambdas which capture a `sender` to have 
     * a `mutable` capture list. Because of this implementors of `sender` 
     * must mark all members used in the implementation of this function as 
     * mutable or const.
     *
     * @param msg message to be sent to the object 
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    virtual bool send(sptr<message> msg) const = 0;

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

/**
 * Extension of `sender` interface which enables API to schedule arbitrary tasks 
 * for execution.
 */
struct scheduler : public sender {
    /**
     * @brief generic function wrapper for scheduling and executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type.
     */
    struct task { 
        /**
         * Any object which implements this interface will be convertable to a 
         * `task`.
         */
        struct convertable {
            virtual ~convertable(){}
            virtual operator task() = 0;
        };

        task(){}
        task(const task& rhs) : m_hdl(rhs.m_hdl) { }
        task(task&& rhs) : m_hdl(std::move(rhs.m_hdl)) { }

        /**
         * Template type `F` should be a Callable function or functor. 
         */
        template <typename F, typename... As>
        task(F&& f, As&&... as) : 
            m_hdl([=]() mutable { f(std::forward<As>(as)...); })
        { }

        /**
         * @brief boolean conversion function
         *
         * @return `true` if task contains valid function, else `false`
         */
        inline operator bool()() {
            return m_hdl ? true : false;
        }

        /**
         * @return an `true` if re-queueing is required, else false
         */
        inline bool operator()() {
            hold_and_restore<bool*> har(tl_complete()); 
            tl_complete() = &m_complete;
            m_hdl();
            return m_complete;
        }

        /**
         * Modify the completion state of currently running task object.
         *
         * This internal flag this modifies defaults to `true`. If the user sets 
         * this value to `false`, it will indicate to the processing code that 
         * the task needs to be re-executed instead of being discarded.
         *
         * This operation will fail silently if called outside of a running task 
         * object.
         *
         * @param val the new complete state
         */
        inline static void complete(bool val) {
            bool* cp = tl_complete();
            if(cp) {
                *cp = val;
            }
        }

    private:
        static inline bool*& tl_complete() {
            thread_local bool* c=nullptr;
            return c;
        }

        bool m_complete = true;
        std::function<void()> m_hdl;
    };

    /**
     * @brief schedule a generic task for execution
     *
     * @param t function to execute on target sender
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    inline bool schedule(task t) const {
        return send(message::make(0,std::move(t)));
    }

    /**
     * @brief schedule a shared pointer convertable to a task for execution
     *
     * @param sp shared pointer to convert to task and execute on target sender
     * @return `true` on success, `false` on failure due to object being shutdown 
     */
    template <typename T>
    bool schedule(sptr<T> sp) const {
        return schedule(task(*std::dynamic_pointer_cast<task::convertable>(sp)));
    };

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
 */
struct channel : public sender, 
                 protected self_aware<channel>,
                 public lifecycle_aware { 
    /**
     * @brief Construct a channel as a shared_ptr 
     * @return a channel shared_ptr
     */
    static inline sptr<channel> make() {
        auto ch = sptr<channel>(new channel());
        ch->m_self = ch;
        return ch;
    }

    /** 
     * @return count of messages in the queue
     */
    inline std::size_t queued() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_msg_q.size();
    }

    /**
     * @return true if queue is empty, else false 
     */
    inline bool empty() const { 
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_msg_q.size() == 0;
    }

    /**
     * @return count of `st::fiber`s blocked on `recv()`
     */
    inline std::size_t blocked_receivers() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_recv_q.size();
    }

    inline bool running() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_shutdown;
    }

    inline void shutdown(bool process_remaining_messages=true) {
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

    inline bool send(sptr<message> msg) const {
        sptr<blocker> blk;

        {
            std::unique_lock<std::mutex> lk(m_mtx);

            if(m_shutdown) {
                return false;
            } else {
                m_msg_q.push_back(std::move(msg));
            }

            if(m_recv_q.size()) {
                blk = std::move(m_recv_q.front());
                m_recv_q.pop_front();
                blk->flag = true;
            }
        }

        if(blk) { // notify outside of lock scope to limit mutex blocking
            blk->cv.notify_one();
        }

        return true;
    }

    /**
     * @brief optionally enqueue the argument message and receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return true on success, false if channel is closed
     */
    inline bool recv(sptr<message>& msg) {
        std::unique_lock<std::mutex> lk(m_mtx);

        // block until message is available or channel close
        while(m_msg_q.empty() && !m_shutdown) { 
            auto recv_blk = std::make_shared<blocker>();
            m_recv_q.push_back(recv_blk);
            recv_blk->wait(lk);
        }

        if(m_msg_q.empty()) { // no more messages to process, channel closed
            return false;
        } else {
            msg = std::move(m_msg_q.front());
            m_msg_q.pop_front();
            return true;
        } 
    }

private:
    struct blocker {
        inline void wait(std::unique_lock<std::mutex>& lk) {
            do {
                cv.wait(lk);
            } while(!flag);
        }

        bool flag = false;
        std::condition_variable cv;
    };

    channel() : m_shutdown(false) { }

    channel() = delete;
    channel(const channel& rhs) = delete;
    channel(channel&& rhs) = delete;

    mutable bool m_shutdown;
    mutable std::mutex m_mtx;
    mutable std::deque<sptr<message>> m_msg_q;
    mutable std::deque<sptr<blocker>> m_recv_q;
};

/**
 * @brief a coroutine fiber which is intended to run on either a system thread or another executing `st::fiber`
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
 * this can be simulated with some complicated `switch` based hacks which add a 
 * lot of complexity, that also come with their own limitations). 
 *
 * Instead this library allows the user to create `st::fiber` instances 
 * with user defined functors as a template argument. This is the case in 
 * these functions: 
 * `st::fiber::thread<FUNCTOR>(...)`
 * `st::fiber::schedule<FUNCTOR>(...)`
 *
 * Type FUNCTOR should be a functor class. A functor is a class with call 
 * operator overload which accepts an argument sptr<message>, 
 * ex:
 * ```
 * struct MyFunctor {
 *     void operator()(st::sptr<st::message> m);
 * };
 * ```
 *
 * Message arguments can also be accepted by reference:
 * ```
 * struct MyFunctor {
 *     void operator()(st::sptr<st::message>& m);
 * };
 * ```
 *
 * Note: `st::fibers`s automatically throw out any null messages received 
 * from their channel.
 *
 * Functors can alternatively return an `st::sptr<st::message>`. If the 
 * functor follows this pattern any returned non-null message will be 
 * requeued for future processing:
 * ```
 * struct MyFunctor {
 *     sptr<message> operator()(st::sptr<st::message> m);
 * };
 * ```
 *
 * Or again by reference:
 * ```
 * struct MyFunctor {
 *     sptr<message> operator()(st::sptr<st::message>& m);
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
 * destructors as normal.
 *
 * If a `st::fiber` running in a non-blocking fashion has no more messages 
 * to receive, it will suspend itself until new messages are sent to it via 
 * `st::fiber::send()` or `st::fiber::schedule<FUNCTOR>()`, whereupon it will 
 * resume processing messages.
 *
 * Because `st::fiber`s are designed to run in a non-blocking fashion, any 
 * message whose processing blocks an `st::fiber` indefinitely can cause all 
 * sorts of bad effects, including deadlock. In a scenario where a long running 
 * call needs to be made, it may be better to call `std::async` to launch a 
 * dedicated system thread and execute the function, then send a message back 
 * to the originating fiber with the result:
 *
 * ```
 * struct MyFiber {
 *     enum op {
 *         // ...
 *         handle_long_running_result,
 *         // ...
 *     };
 *
 *     void operator()(sptr<message> msg) {
 *         switch(msg->id) {
 *             // ...
 *                 // need to make a long running call 
 *                 // get a copy of the currently running fiber
 *                 st::sptr<st::fiber> self = st::fiber::local_self();
 *                 // launch a thread with a default functor and schedule the call
 *                 std::async([self]{ 
 *                     auto my_result = execute_long_running_call();
 *                     self->send(op::handle_long_running_result, my_result);
 *                 }); // thread will process the scheduled function before shutting down
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
struct fiber : public type_aware,
               protected self_aware<fiber>,
               public lifecycle_aware,
               public scheduler,
               public scheduler::task::convertable {
    /**
     * Generic FUNCTOR which only processes messages sent via `schedule()` API, 
     * ignoring all other messages.
     */
    struct processor {
        inline void operator()(sptr<message>& msg) { }
    };

    /**
     * @brief construct and launch a system thread running user FUNCTOR as a blocking `st::fiber`
     *
     * The toplevel `st::fiber` will only be allocated on the running thread. 
     * This means the constructor of the FUNCTOR will only be called on the new
     * system thread. This allows clever usages of thread_local data where 
     * necessary.
     *
     * @param as optional arguments to the constructor of type FUNCTOR
     */
    template <typename FUNCTOR=processor, typename... As>
    static sptr<fiber> thread(As&&... as) {
        sptr<fiber> f;
        bool flag = false;
        std::condition_variable cv;
        std::mutex mtx;

        std::unique_lock<std::mutex> lk(mtx);

        std::thread([&]{
            f = fiber::make<FUNCTOR>(std::forward<As>(as)...);

            {
                std::lock_guard<std::mutex> lk(mtx);
                flag = true;
            }

            cv.notify_one();
            scheduler::task(f)(); // convert to task and execute
        }).detach();

        // block until thread is started so fiber can be constructed by the new thread
        while(!flag) {
            cv.wait(lk);
        }

        return f;
    }

    /**
     * @brief construct and schedule a new `st::fiber` created with user FUNCTOR on another running `st::fiber`
     *
     * The returned `st::fiber` will execute in a non-blocking, cooperative 
     * fashion on the target `st::fiber`.
     *
     * @param as optional arguments to the constructor of type FUNCTOR
     * @return allocated `st::sptr<st::fiber>` if successfully launched, else empty pointer
     */
    template <typename FUNCTOR=processor, typename... As>
    sptr<fiber> launch(As&&... as) {
        sptr<fiber> f = fiber::make<FUNCTOR>(std::forward<As>(as)...);
        return schedule(scheduler::task(f)) ? f : sptr<fiber>();
    }

    /** 
     * @brief allocate a fiber to listen to a channel and pass messages to 
     * template FUNCTOR for processing. The FUNCTOR will to be called whenever a 
     * message is received by the fiber's channel. 
     *
     * It is typically better and cleaner to call `st::fiber::thread()` or 
     * `st::fiber::launch()` instead of this function, because a `st::fiber` 
     * made with this function will not automatically execute. It must first  
     * be converted to a `st::scheduler::task()` which itself can be executed. 
     *
     * Furthermore, the user must be careful to only execute any 
     * `st::scheduler::task()`s created this way on a thread they intend to be 
     * owned by the `st::fiber` because it will block indefinitely unless 
     * running inside another `st::fiber`.
     *
     * @param as constructor arguments for type FUNCTOR
     * @return allocated running st::thread st::thread shared_ptr
     */
    template <typename FUNCTOR, typename... As>
    static sptr<fiber> make(As&&... as) {
        auto hdl = functorizor::generate(sptr<FUNCTOR>(new FUNCTOR(std::forward<As>(as)...)));
        auto cp = sptr<fiber>(new fiber(type_code<FUNCTOR>(), std::move(hdl)));
        cp->m_self = cp;
        return cp;
    }

    inline bool running() {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_ch->running();
    }

    inline void shutdown(bool process_remaining_messages) {
        m_ch->shutdown(process_remaining_messages);
    }
   
    inline operator scheduler::task() {
        auto self = m_self.lock();
        return scheduler::task([&,self]{ 
            sptr<message> msg;
            m_parent = tl_self();
            hold_and_restore<wptr<fiber>> har(tl_self());
            tl_self() = m_self;

            std::unique_lock<std::mutex> lk(m_mtx);

            auto process_message = [&]{
                lk.unlock();
                if(msg->is<scheduler::task>)
                   msg->data.cast_to<scheduler::task>()()) {
                    msg.reset();
                } else {
                    msg = m_hdl(msg);
                }

                if(msg) {
                    m_ch->send(msg);
                }
                lk.lock();
            };

            if(m_parent) { // child fiber 
                // receive once if we will not block
                if(m_ch->queued() && m_ch->recv(msg) && msg) { 
                    process_message();
                } 
            } else { // root fiber
                // blocking run-loop
                while(m_ch->recv(msg) && msg) { 
                    process_message();
                }
            }

            m_blocked = m_ch->running() && !m_ch->queued(); 
            scheduler::task::complete(m_blocked);
        });
    }

    inline bool send(sptr<message> msg) const {
        bool r = m_ch->send(std::move(msg));

        std::lock_guard<std::mutex> lk(m_mtx);
        if(r && m_blocked) {
            sptr<fiber> parent = m_parent.lock();

            if(parent) {
                if(r = parent->schedule(m_self.lock())) {
                    m_blocked = false;
                } // else parent has been shutdown
            } // else this fiber has not been run yet
        } // else fiber is shutdown

        return r;
    }

    /**
     * @return a copy of the fiber currently running on the calling thread,
     */
    static inline sptr<fiber> local_self() {
        return tl_self().lock();
    }

    /**
     * @brief class describing the workload of an `st::fiber`
     *
     * Useful for comparing relative `st::fiber` workloads when scheduling.
     */
    struct weight {
        /**
         * @brief represents count of queued messages on a `st::fiber`
         */
        std::size_t queued;

        /**
         * @brief represents if a `st::fiber` is currently processing a message
         */
        bool executing;

        /**
         * @return true if the weight is 0, else false
         */
        inline bool empty() const {
            return !(queued && executing);
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
    inline weight get_weight() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return weight{
            m_ch->queued(), 
            m_ch->blocked_receivers() ? false : !m_blocked
        };
    }

private:
    /**
     * @brief a utility struct to wrap user FUNCTOR implementations into a standarized API
     */
    struct functorizor {
        typedef std::function<sptr<message>(sptr<message>&)> functor;

        template <typename FUNCTOR>
        static functor generate(sptr<FUNCTOR> f) {
            return = [=](sptr<message>& msg) mutable -> sptr<message> {
                using ismsg = std::is_same<sptr<message>,decltype((*f)(msg))>::type;
                return functorizor::execute<ismsg>(*f, msg);
            }
        }

    private:
        // handler for functors returning sptr<message>
        template <std::true_type, typename FUNCTOR>
        static sptr<message> execute(FUNCTOR& f, sptr<message>& msg) {
            return f(msg); 
        }

        // handler for all other functors
        template <std::false_type, typename FUNCTOR>
        static sptr<message> execute(FUNCTOR& f, sptr<message>& msg) {
            f(msg);
            return sptr<message>();
        }
    };

    static inline wptr<fiber>& tl_self() {
        thread_local wptr<fiber> wp;
        return wp
    }

    fiber(const std::size_t type_code, functorizer::functor&& hdl) : 
        m_ch(channel::make()),
        m_blocked(true),
        m_hdl(std::move(hdl)),
        type_aware(type_code)
    { }

    mutable std::mutex m_mtx;
    mutable sptr<channel> m_ch;
    mutable bool m_blocked;
    mutable wptr<fiber> m_parent; // weak pointer to parent fiber
    sptr<message> m_msg;
    functorizer::functor m_hdl;
};

}

#endif
