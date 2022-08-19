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
 * @brief convenience and readability aid type alias to std::shared_ptr 
 */
template <typename T>
using sptr = std::shared_ptr<T>;

/**
 * @brief convenience and readability aid type alias to std::weak_ptr 
 */
template <typename T>
using wptr = std::weak_ptr<T>;

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
private:
    T& m_ref;
    T m_old;
};

//******************************************************************************
// INHERITABLE INTERFACES

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
     * `st::thread::cast_to<st::sender>()`
     * `st::coroutine::cast_to<st::sender>()`
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
 * @brief utility and interface to access thread_local instances of implementors  
 */
template <typename CRTP>
struct local : protected self_aware<CRTP> {
    /** 
     * This is useful for gaining a copy of the shared pointer of an 
     * implementing object that the current code is running 'inside' of. For 
     * instance, if a user has created an st::thread which is executing a user's 
     * FUNCTOR, that FUNCTOR can access its st::thread via this call:
     * ```
     * struct MyFunctor {
     *     void operator()(sptr<message> m) { 
     *         // access the thread MyFunctor is running on
     *         sptr<st::thread> self = local<st::thread>::self();
     *     }
     * };
     *
     * int main() {
     *     sptr<st::thread> thd = st::thread::make<MyFunctor>();
     *     thd->send(0, "hello"); // send some message for MyFunctor to process
     *     return 0;
     * }
     * ```
     * @return the thread_local copy of the implementor's shared pointer
     */
    static inline sptr<CRTP> self() {
        return self_ref().lock();
    }

protected:
    /**
     * Inheritors of this interface are responsible for calling this function 
     * whenever they want to set the local state to themselves. The state will 
     * *only* be set to themselves while this function is blocking.
     *
     * @param f Callable function
     * @param as optional arguments to f
     */
    template <typename F, typename... As>
    inline void execute(F&& f, As&&... as) {
        hold_and_restore<wptr<CRTP>> har(self_ref());
        self_ref() = m_self;
        f(std::forward<As>(as...));
    }

private:
    static inline wptr<CRTP>& self_ref() {
        thread_local wptr<CRTP> wp;
        return wp
    }
};

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
     * @return `true` if the object is valid/running, else `false`
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
     * must mark all members used in the implementation as mutable or const.
     *
     * @param msg message to be sent to the object 
     * @return result of the send operation
     */
    virtual result send(sptr<message>&& msg) const = 0;

    /**
     * @brief send a message with given parameters
     *
     * @param as arguments passed to `message::make()`
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::shutdown if closed
     */
    template <typename... As>
    result send(As&&... as) const {
        return send(message::make(std::forward<As>(as)...));
    }
};

/**
 * Extension of `sender` interface which enables API to schedule arbitrary tasks 
 * for execution.
 */
struct scheduler : public sender {
    /**
     * Generic function wrapper similar to std::function<bool()>.
     *
     * Used to convert and wrap code to a generically executable type.
     */
    struct task { 
        /**
         * Any object which implements this interface will be convertable to a 
         * `scheduler::task`.
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
         * Boolean conversion  
         *
         * @return `true` if task contains valid function, else `false`
         */
        inline operator bool()() {
            return m_hdl ? true : false;
        }

        /**
         * If call `operator()` returns true, then the task is complete. Else it 
         * should be requeued for further processing.
         */
        inline bool operator()() {
            hold_and_restore<bool*> har(tl_task()); 
            tl_task() = m_complete;
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
            st::thread_local bool* c=nullptr;
            return c;
        }

        bool m_complete = true;
        std::function<void()> m_hdl;
    };

    /**
     * @brief schedule a generic task for execution
     *
     * @param t function to execute on target sender
     * @return result
     */
    inline result schedule(task t) const {
        auto msg = message::make(0,std::move(t));
        msg->set_tag<task>();
        return send(std::move(msg));
    }

    /**
     * @brief schedule a shared pointer convertable to a task for execution
     *
     * @param sp shared pointer to convert to task and execute on target sender
     * @return result
     */
    template <typename T>
    result schedule(sptr<T> sp) const {
        return schedule(task(*std::dynamic_pointer_cast<task::convertable>(sp)));
    };

    /**
     * @brief wrap user function and arguments then schedule as a generic task for execution
     *
     * @param f function to execute on target sender 
     * @param as arguments for argument function
     * @return result
     */
    template <typename F, typename... As>
    result schedule(F&& f, As&&... as) const {
        return schedule(task([=]() mutable { f(std::forward<As>(as)...); }));
    }
};

//******************************************************************************
// CORE DATA TYPES

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
    typedef std::unique_ptr<void,deleter_t> data_pointer_t;

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
 * @brief Object representing the result of a communication operation 
 *
 * Can be used directly in if/else statements due to this class having an 
 * implementation of `operator bool`.
 */
struct result {
    /// Enumeration representing the status of the associated operation
    enum eStatus {
        success, ///< operation succeeded 
        shutdown ///< operation failed due to object being shutdown
    };

    /**
     * @return true if the result of the operation succeeded, else false
     */
    inline operator bool() const {
        return status == eStatus::success;
    }

    /// Status of the operation
    eStatus status;
};

//******************************************************************************
// INTERTHREAD CLASSES

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between managed 
 * system threads. Provided here as a convenience for communicating from managed 
 * system threads to other user `st::thread`s. All methods in this object are 
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
     * @return count of `st::thread`s blocked on `recv()`
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

    inline result send(sptr<message>&& msg) const {
        sptr<blocker> blk;

        {
            std::unique_lock<std::mutex> lk(m_mtx);

            if(m_shutdown) {
                return result{ result::eStatus::shutdown };
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

        return result{ result::eStatus::success };
    }

    /**
     * @brief optionally enqueue the argument message and receive a message over the channel
     *
     * If argument message is non-null and `exchange` equals `true` then the 
     * value of the argument message will be immediately pushed to the back of 
     * the queue. Using this feature is useful for writing loops when a 
     * previously received message needs to be requeued for further processing.
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue.
     *
     * @param msg interprocess message object reference (optionally containing a message for exchange) to contain the received message 
     * @param exchange if true, and msg is non-null, msg will be enqueued before a new message is retrieved
     * @return true on success, false if channel is closed
     */
    inline result recv(sptr<message>& msg, bool exchange = false) {
        result r;

        {
            std::unique_lock<std::mutex> lk(m_mtx);
            if(msg) { 
                if(exchange) {
                    // if msg is provided, immediately enqueue
                    m_msg_q.push_back({ std::move(msg), sptr<blocker>() });
                }
                msg.reset()
            }

            // block until message is available or channel close
            while(m_msg_q.empty() && !m_shutdown) { 
                auto recv_blk = std::make_shared<blocker>();
                m_recv_q.push_back(recv_blk);
                recv_blk->wait(lk);
            }

            if(m_msg_q.empty()) { // no more messages to process, channel closed
                r = result{ result::eStatus::shutdown };
            } else {
                msg = std::move(m_msg_q.front());
                m_msg_q.pop_front();

                r = result{ result::eStatus::success };
            } 
        }

        return r;
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

    channel() : m_running(false) { }

    channel() = delete;
    channel(const channel& rhs) = delete;
    channel(channel&& rhs) = delete;

    mutable bool m_shutdown;
    mutable std::mutex m_mtx;
    mutable std::deque<sptr<message>> m_msg_q;
    mutable std::deque<sptr<blocker>> m_recv_q;
};

struct coroutine;

/**
 * @brief managed operating system thread
 *
 * This object represents an `std::thread` running a blocking toplevel 
 * `st::coroutine` created via `st::thread::make<FUNCTOR>(...)`. It can be used 
 * to asynchronously execute arbitrary code by calling its 
 * `st::thread::schedule()` functions OR to receive messages sent via 
 * `st::thread::send()` that will be processed by the FUNCTOR.
 */
struct thread : public type_aware,
                protected local<thread>,
                public lifecycle_aware,
                public scheduler {
    /**
     * Generic FUNCTOR which only processes messages sent via `schedule()` API, 
     * ignoring all other messages.
     */
    struct processor {
        inline void operator()(sptr<message> msg) { }
    };

    /**
     * @brief construct and launch a system thread running user FUNCTOR as a blocking toplevel `coroutine`
     *
     * All `send()` and `schedule()` type operations will be forwarded to the 
     * toplevel coroutine allocated using the user FUNCTOR type.
     *
     * The toplevel `sptr<coroutine>` will only be allocated on the running 
     * `st::thread`. This means the constructor and destructor of the FUNCTOR 
     * will only be called on the system thread.
     *
     * See `coroutine::make()` documentation for further details on using 
     * FUNCTORs to process received messages.
     *
     * @param as optional arguments to the constructor of type FUNCTOR
     */
    template <typename FUNCTOR=processor, typename... As>
    static sptr<thread> make(As&&... as) {
        auto generator = [=]() mutable -> scheduler::task {
            return coroutine::make<FUNCTOR>(std::forward<As>(as)...);
        };
        sptr<thread> wp(new st::thread(std::move(generator), std::forward<As>(as)...));
        wp->start(wp); // launch system thread
        return wp;
    }

    inline bool running() {
        std::lock_guard<std::mutex> lk(m_mtx);
        bool r = m_thd.joinable() && !m_ch->running();
        return r;
    }

    inline void shutdown(bool process_remaining_messages) {
        std::lock_guard<std::mutex> lk(m_mtx);
        if(m_thd.joinable()) {
            if(m_task && !m_task->running()) {
                m_task->shutdown(process_remaining_messages);
            }
            m_thd.join();
        }
    }

    inline result send(sptr<message>&& msg) const {
        return m_task->send(std::move(msg));
    }

    /**
     * @brief class describing the workload of a `st::thread`
     *
     * Useful for comparing relative `st::thread` workloads when scheduling.
     */
    struct weight {
        /**
         * @brief represents count of queued messages on a `st::thread`
         */
        std::size_t queued;

        /**
         * @brief represents if a `st::thread` is currently processing a message
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
     * @return weight representing workload of `st::thread`
     */
    inline weight get_weight() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return weight{m_ch->queued(), 
                      m_ch->blocked_receivers() ? false 
                                                : m_thd.joinable() ? true 
                                                                   : false};
    }

private:
    template <typename FUNCTOR, typename... As>
    thread(std::function<scheduler::task()> main_task_generator, As&&... as) : 
        m_main_task_generator(std::move(main_task_generator)),
        type_aware(type_code<FUNCTOR>()) 
    { }

    thread() = delete;
    thread(const thread& rhs) = delete;
    thread(thread&& rhs) = delete;
    
    inline void start(wptr<st::thread> self) {
        m_self = self;
        bool thread_started_flag = false;
        std::condition_variable thread_start_cv;

        m_thd = std::thread([&]{
            local<st::thread>::execute([&]{
                m_task = m_main_task_generator(); // create coroutine on the new `st::thread`

                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    thread_started_flag = true;
                }

                thread_start_cv.notify_one();

                scheduler::task(m_task)(); // run & block on main thread task 
            });
        });

        std::unique_lock<std::mutex> lk(m_mtx);
        while(!thread_started_flag) {
            thread_start_cv.wait(lk);
        }
    }

    mutable std::mutex m_mtx;
    mutable std::function<sptr<coroutine>()> m_main_task_generator;
    std::thread m_thd;
    sptr<coroutine> m_task;
};

/**
 * @brief a coroutine which is intended to run on either an `st::thread`, another executing `st::coroutine`, or a system `std::thread`
 *
 * A `coroutine` definition according to wikipedia: Coroutines are computer 
 * program components that generalize subroutines for non-preemptive 
 * multitasking, by allowing execution to be suspended and resumed.
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
 * Instead this library allows the user to create FUNCTORs in the same pattern 
 * as those used in `st::thread::make()` when calling `st::coroutine::make()` 
 * (in fact, `st::thread::make()` calls `st::coroutine::make()` internally). 
 *
 * To execute the `st::coroutine` it must be first converted to an 
 * `st::scheduler::task` (which it is implicitly convertable to) and executed 
 * as said task. This conversion will be done automatically if the `coroutine` 
 * is scheduled via a `st::thread::schedule()` or `st::coroutine::schedule()` 
 * function.
 *
 * If no other `st::coroutine` is running on the current system thread, that 
 * `st::coroutine` will not return until it is shutdown, blocking the thread. 
 * However, any `st::coroutine`s that are scheduled on *ANOTHER* 
 * `st::coroutine` will run in a non-blocking fashion, giving a chance for 
 * each to run. 
 *
 * Each `st::thread` actually manages its own `st::coroutine`. When 
 * `st::thread::schedule()` is called the `st::thread` will call 
 * `st::coroutine::schedule()` on its internal `st::coroutine` with the user's 
 * arguments. This means that any `st::coroutine`s scheduled on an `st::thread` 
 * will run in a non-blocking fashion as they are children to the `st::thread`'s 
 * internal `st::coroutine`.
 *
 * If a `st::coroutine` running in a non-blocking fashion has no more messages 
 * to receive, it will suspend itself until new messages are sent to it via 
 * `st::coroutine::send()` or `st::coroutine::schedule()`, whereupon it will 
 * resume processing messages.
 */
struct coroutine : public type_aware,
                   protected local<coroutine>,
                   public lifecycle_aware,
                   public scheduler,
                   public scheduler::task::convertable {
    /** 
     * @brief allocate a coroutine to listen to a channel and pass messages to 
     * template FUNCTOR for processing. The FUNCTOR will to be called whenever a 
     * message is received by the coroutine's channel. 
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
     * Note: `st::coroutine`s automatically throw out any null messages received 
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
     * @param as constructor arguments for type FUNCTOR
     * @return allocated running st::thread st::thread shared_ptr
     */
    template <typename FUNCTOR, typename... As>
    sptr<coroutine> make(As&&... as) {
        if(proc && proc->is<coroutine>()) {
            auto hdl = coroutine::generate_handler<FUNCTOR>(std::forward<As>(as)...);
            auto cp = sptr<coroutine>(new coroutine(type_code<FUNCTOR>(), std::move(hdl)));
            cp->m_self = cp;
            return cp;
        } else {
            return sptr<task>();
        }
    }

    inline bool running() {
        return !m_ch->running();
    }

    inline void shutdown(bool process_remaining_messages) {
        m_ch->shutdown(process_remaining_messages);
    }
   
    inline operator scheduler::task() {
        auto self = m_self.lock();
        return scheduler::task([self]{ task::complete(run()); });
    }

    inline result send(sptr<message>&& msg) const {
        result r = m_ch->send(std::move(msg));

        std::lock_guard<std::mutex> lk(m_mtx);
        if(r && m_blocked) {
            sptr<coroutine> parent = m_parent.lock();

            if(parent) {
                if(r = parent->schedule(m_self.lock())) {
                    m_blocked = false;
                } // else parent has been shutdown
            } // else this coroutine has not been run yet
        } // else coroutine is shutdown

        return r;
    }

private:
    // handler for functors returning bool
    template <std::true_type, typename FUNCTOR>
    static bool process(FUNCTOR& f) {
        return f(msg); 
    }

    // handler for all other functors
    template <std::false_type, typename FUNCTOR>
    static bool process(FUNCTOR& f) {
        f(msg);
        return true;
    }

    template <typename FUNCTOR, typename... As>
    static std::function<bool()> generate_handler(As&&... as) {
        auto fp = sptr<FUNCTOR>(new FUNCTOR(std::forward<As>(as)...));
        return [fp]() -> bool { 
            using isbool = std::is_same<bool,decltype(f())>::type;
            return coroutine::process<isbool>(*fp);
        };
    }

    coroutine(const std::size_t type_code, handler&& hdl) : 
        m_ch(channel::make()),
        m_blocked(true),
        m_hdl(std::move(hdl)),
        type_aware(type_code)
    { }

    inline bool run() {
        m_parent = local<st::coroutine>::self();

        local<coroutine>::execute([&]{
            std::unique_lock<std::mutex> lk(m_mtx);

            auto process_message = [&]{
                lk.unlock();
                if(m_msg->data.is<scheduler::task>() &&
                   m_msg->data.cast_to<scheduler::task>()()) {
                    m_msg.reset(); // reset message if task completed
                } else {
                    m_msg = m_hdl(m_msg);
                }
                lk.lock();
            };

            if(m_parent) { // child coroutine 
                // only receive if we will not block
                if(m_ch->queued() && m_ch->recv(m_msg,true) && m_msg) { 
                    process_message();
                } 
            } else { // toplevel coroutine for the current st::thread
                while(m_ch->recv(m_msg,true) && m_msg) { // blocking run-loop
                    process_message();
                }
            }

            m_blocked = !m_ch->running() && !m_msg; // bool conversion of shared_ptr
        });
            
        return m_blocked; // if !m_blocked, requeue coroutine for evaluation 
    }

    mutable std::mutex m_mtx;
    mutable sptr<channel> m_ch;
    mutable bool m_blocked;
    mutable wptr<coroutine> m_parent; // weak pointer to parent coroutine
    sptr<message> m_msg;
    std::function<sptr<message>(sptr<message>)> m_hdl;
};

/**
 * @brief a class managing one or more identical st::threads 
 *
 * The `executor` object implements a constant time algorithm which attempts 
 * to efficiently distribute tasks among st::threads.
 *
 * The `executor` object is especially useful for scheduling operations 
 * which benefit from high CPU throughput and are not reliant on the specific 
 * st::thread upon which they run. 
 *
 * Highest CPU throughput is typically reached by an executor whose st::thread 
 * count matches the CPU core count of the executing machine. This optimal 
 * number of cores may be discoverable by the return value of a call to 
 * `executor::default_worker_count()`.
 *
 * Because `executor` manages a limited number of st::thread, any message whose 
 * processing blocks an st::thread indefinitely can cause all sorts of bad effects, 
 * including deadlock. 
 */
struct executor : public type_aware, 
                  protected self_aware<executor>,
                  public lifecycle_aware,
                  public scheduler {
    /**
     @brief attempt to retrieve a sane executor st::thread count for maximum CPU throughput

     The standard does not enforce the return value of 
     `std::st::thread::hardware_concurrency()`, but it typically represents the 
     number of cores a computer has, which is also generally the ideal number of 
     st::threads to allocate for maximum processing throughput.

     @return maximumly efficient count of st::threads for CPU throughput
     */
    static inline std::size_t default_worker_count() {
        return std::st::thread::hardware_concurrency() 
               ? std::st::thread::hardware_concurrency() 
               : 1;
    }

    /**
     * @brief allocate an executor to manage multiple st::threads
     *
     * The template type FUNCTOR is the same as used in
     * `st::thread::make<FUNCTOR>(constructor args...)`, allowing the user to 
     * design and specify any FUNCTOR they please. However, in many cases the 
     * user can simply use `scheduler` as the FUNCTOR type, as it is 
     * designed for processing generic operations. Doing so will also allow the 
     * user to schedule arbitary `cotask` coroutines on the `executor`.
     *
     * An intelligent value for worker_count can typically be retrieved from 
     * `default_worker_count()` if maximum CPU throughput is desired.
     *
     * @param worker_count the number of st::threads this executor should manage
     * @param as constructor arguments for type FUNCTOR
     * @return allocated running executor shared_ptr
     */
    template <typename FUNCTOR=st::thread::processor, typename... As>
    static sptr<executor> make(std::size_t worker_count, As&&... as) {
        return sptr<executor>(new executor(
            type_code<FUNCTOR>(),
            worker_count,
            [=]() mutable -> sptr<st::thread> {
                return st::thread::make<FUNCTOR>(std::forward<As>(as)...); 
            }));
    }
    
    inline bool running() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_workers.size() ? true : false;
    }

    inline void shutdown(bool process_remaining_messages) {
        std::lock_guard<std::mutex> lk(m_mtx);
        for(auto& w : m_workers) {
            w->shutdown(process_remaining_messages);
        }

        m_workers.clear();
    }

    inline result send(sptr<message>&& msg, bool blocking) const {
        result r{ result::eStatus::shutdown };

        std::lock_guard<std::mutex> lk(m_mtx);
        if(m_workers.size() ? true : false) {
            auto wkr = select_worker();

            r = wkr->send(std::move(msg));
        } 

        return r;
    }

    /**
     * @return the count of the `st::thread`s managed by this object
     */
    inline std::size_t worker_count() const {
        return m_worker_count;
    }

private:
    typedef std::vector<sptr<st::thread>> worker_vector_t;
    typedef worker_vector_t::iterator worker_iter_t;

    executor(const std::size_t type_code;
             const std::size_t worker_count, 
             std::function<sptr<st::thread>()> make_worker) :
        m_worker_count(worker_count ? worker_count : 1), // enforce 1 st::thread 
        m_workers(m_worker_count),
        m_cur_it(m_workers.begin()),
        type_aware(type_code) {
        for(auto& w : m_workers) {
            w = make_worker();
        }
    }

    executor() = delete;
    executor(const executor& rhs) = delete;
    executor(executor&& rhs) = delete;

    // selected a st::thread to sechedule task on
    inline st::thread* select_worker() {
        if(m_worker_count > 1) {
            auto& prev_worker = *(m_cur_it);
            ++m_cur_it;

            // if at the end of the vector return the first entry
            if(m_cur_it == m_workers.end()) {
                m_cur_it = m_workers.begin();
            } 

            auto& cur_worker = *(m_cur_it);

            if(prev_worker->get_weight() < cur_worker->get_weight()) {
                return prev_worker.get();
            } else {
                return cur_worker.get();
            }
        } else {
            return m_cur_it->get();
        }
    }

    mutable std::mutex m_mtx;
    const std::size_t m_worker_count;
    mutable worker_vector_t m_workers;
    mutable worker_iter_t m_cur_it;
};

/**
 * A fairly simple finite state machine mechanism (FSM). FSMs are
 * somewhat infamous for being difficult to parse, too unwieldy, or otherwise 
 * opaque. As with everything else in this library, the aim of this object's 
 * design is to make sure the necessary features are kept simple without overly 
 * limiting the user. Therefore some care has been attempted to mitigate those 
 * concerns.
 *
 * The toplevel class for this feature is actually the inheritable state
 * object. The user should implement classes which publicly inherit `state`, 
 * overriding its `enter()` and `exit()` methods as desired. A static 
 * `state::make()` function is included as convenience for the user so they 
 * do not have to manually typecast allocated pointers when constructing state 
 * objects.
 *
 * The user must create an allocated state machine (`state::machine`) 
 * using static function `state::machine::make()` to register their states
 * and trigger events. The state machine can then be notified of new events 
 * with a call to 
 * `state::machine::process_event(sptr<message>)`.
 */
struct state : protected type_aware {
    // explicit destructor definition to allow for proper virtual delete behavior
    virtual ~state(){} 

    /**
     * @brief called during a transition when a state is entered 
     *
     * The returned value from this function can contain a further event to
     * process. 
     *
     * That is, if the return value:
     * - is null: operation is complete 
     * - is non-null: the result as treated like the argument of an additional `process_event()` call
     *
     * Thus, this function can be used to implement transitory states where 
     * logic must occur before the next state is known. 
     *
     * @param event a message containing the event id and an optional data payload
     * @return optional shared_ptr<message> containing the next event to process (if pointer is null, no futher event will be processed)
     */
    inline virtual sptr<message> enter(sptr<message> event) { 
        return sptr<message>();
    }

    /**
     * @brief called during a transition when a state is exitted
     *
     * The return value determines whether the transition from the current state 
     * will be allowed to continue. Thus, this function can be used to implement 
     * transition guards.
     *
     * @param event a message containing the event id and an optional data payload
     * @return true if exit succeeded and transition can continue, else false
     */
    inline virtual bool exit(sptr<message> event) { 
        return true; 
    }

    /**
     * @brief a convenience function for generating shared_ptr's to state objects
     * @param as Constructor arguments for type T
     * @return an allocated shared_ptr to type T implementing state
     */
    template <typename T, typename... As>
    static sptr<state> make(As&&... as) {
        sptr<state> st(dynamic_cast<state*>(new T(std::forward<As>(as)...)));
        st->m_type_code = type_code<T>();
        return st;
    }

    /**
     * The actual state machine.
     *
     * This object is NOT mutex locked, as it is not intended to be used directly 
     * in an asynchronous manner. 
     */
    struct machine {
        /**
         * @return an allocated state machine
         */
        static inline sptr<machine> make() {
            return sptr<machine>(new machine);
        }

        /**
         * @brief Register a state object to be transitioned to when notified of an event
         * @param event an unsigned integer representing an event that has occurred
         * @param st a pointer to an object which implements class state  
         * @return true if state was registered, false if state pointer is null or the same event is already registered
         */
        template <typename ID>
        bool register_transition(ID event_id, sptr<state> st) {
            return register_state(static_cast<std::size_t>(event_id), 
                                  registered_type::transitional_state, 
                                  st);
        }

        /**
         * Type definition of a state callback function
         */
        typedef std::function<sptr<message>(sptr<message>)> callback;

        /**
         * @brief Register a callback to be triggered when its associated event is processed.
         *
         * When the corresponding event is processed for this callback *only* 
         * the callback function will be processed, as no state is exitted or 
         * entered. 
         *
         * The return value of the callback is treated exactly like that of 
         * `sptr<message> state::enter(sptr<message>)`.
         * That is, if the return value:
         * - is null: operation is complete 
         * - is non-null: the result as treated like the argument of an additional `process_event()` call
         *
         * @param event an unsigned integer representing an event that has occurred
         * @param cb a callback function
         * @return true if state was registered, false if state pointer is null or the same event is already registered
         */
        template <typename ID>
        bool register_callback(ID event_id, callback cb) {
            struct callback_state : public state {
                callback_state(callback&& cb) : m_cb(std::move(cb)) { }
                
                inline sptr<message> enter(sptr<message> event) { 
                    return m_cb(std::move(event));
                }

                callback m_cb;
            };

            return register_state(static_cast<std::size_t>(event_id), 
                                  registered_type::callback_state, 
                                  state::make<callback_state>(std::move(cb)));
        }

        /**
         * @brief process_event the state machine an event has occurred 
         *
         * If no call to `process_event()` has previously occurred on this state 
         * machine then no state `exit()` method will be called before the 
         * new state's `enter()` method is called.
         *
         * Otherwise, the current state's `exit()` method will be called first.
         * If `exit()` returns false, the transition will not occur. Otherwise,
         * the new state's `enter()` method will be called, and the current 
         * state will be set to the new state. 
         *
         * If `enter()` returns a new valid event message, then the entire 
         * algorithm will repeat until no allocated or valid event messages 
         * are returned by `enter()`.
         *
         * @param as argument(s) to `message::make()`
         * @return true if the event was processed successfully, else false
         */
        template <typename... As>
        bool process_event(As&&... as) {
            return internal_process_event(message::make(std::forward<As>(as)...));
        }

        /**
         * @brief a utility object to report information about the machine's current status
         */
        struct status {
            /**
             * @return true if the status is valid, else return false
             */
            inline operator bool() {
                return event && state;
            }

            /// the last event processed by the machine
            std::size_t event; 

            /// the current state held by the machine
            sptr<state> state; 
        };

        /**
         * @brief retrieve the current event and state information of the machine 
         *
         * If the returned `status` object is invalid, machine has not yet 
         * successfully processed any events.
         *
         * @return an object containing the most recently processed event and current state
         */
        inline status current_status() {
            if(m_cur_state != m_transition_table.end()) {
                return status{m_cur_state->first, m_cur_state->second.second}; 
            } else {
                return status{0, sptr<state>()};
            }
        }

    private:
        enum registered_type {
            transitional_state, // indicates state can be transitioned to
            callback_state // indicates state represents a callback and will not be transitioned to
        };

        typedef std::pair<registered_type,sptr<state>> state_info;
        typedef std::unordered_map<std::size_t,state_info> transition_table_t;

        machine() : m_cur_state(m_transition_table.end()) { }
        machine(const machine& rhs) = delete;
        machine(machine&& rhs) = delete;

        bool register_state(std::size_t event_id, registered_type tp, sptr<state> st) {
            auto it = m_transition_table.find(event_id);
            if(st && it == m_transition_table.end()) {
                m_transition_table[event_id] = state_info(tp, st);
                return true;
            } else {
                return false;
            }
        }

        inline bool internal_process_event(sptr<message> event) {
            if(event) {
                // process events
                do {
                    auto it = m_transition_table.find(event->id());

                    if(it != m_transition_table.end()) {
                        switch(it->second.first) {
                            case registered_type::transitional_state: 
                                // exit old state 
                                if(m_cur_state != m_transition_table.end()) {
                                    if(!m_cur_state->second.second->exit(event)) {
                                        // exit early as transition cannot continue
                                        return true; 
                                    }
                                }

                                // enter new state(s)
                                event = it->second.second->enter(event);
                                m_cur_state = it;
                                break;
                            case registered_type::callback_state:
                                // execute callback
                                event = it->second.second->enter(event);
                                break;
                            default:
                                event.reset();
                                break;
                        }
                    } else { 
                        return false;
                    }
                } while(event);

                return true;
            } else {
                return false;
            }
        }

        transition_table_t m_transition_table;
        transition_table_t::iterator m_cur_state;
    };

private:
    // number representing compiler type of the derived state object
    std::size_t m_type_code;
};

}

#endif
