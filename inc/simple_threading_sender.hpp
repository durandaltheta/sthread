//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_SENDER__
#define __SIMPLE_THREADING_SENDER__

#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <deque>

#include "simple_message.hpp"

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

// forward declaration
template <CRTP> struct shared_sender_context;

/**
 * @brief object capable of sending a payload back to an `st::sender`
 *
 * This object provides a simple, lightweight way to send messages back to a 
 * requestor while abstracting the message passing details. This object can be 
 * the payload `st::data` of an `st::message`.
 */
struct reply : public shared_context<reply> {
    reply() : m_id(0) { }
    reply(const reply& rhs) { context() = rhs.context(); }
    reply(reply&& rhs) { context() = std::move(rhs.context()); }
    virtual ~reply(){ }

    /**
     * @brief main constructor 
     * @param snd any object implementing `st::shared_sender_context` to send `st::message` back to 
     * @param id unsigned int id of `st::message` sent back over `ch`
     */
    template <typename CRTP>
    static inline reply make(shared_sender_context<CRTP>& snd, std::size_t id) { 
        reply r;
        r.context() = st::context::make<reply::context>(snd.context(), id);
        return r;
    }

    /**
     * @brief send an `st::message` back to some abstracted `st::sender`
     * @param t `st::message` payload data 
     * @return `true` if internal `st::channel::send(...)` succeeds, else `false`
     */
    template <typename T>
    bool send(T&& t) {
        return context()->cast<reply::context>().send(std::forward<T>(t));
    }

private:
    struct context : public st::context {
        context(std::shared_ptr<st::context> snd_ctx, std::size_t id) :
            m_snd_ctx(std::move(snd_ctx)),
            m_id(id),
            st::context(st::context::type_info<reply, reply::context>())
        { }

        virtual ~context(){ }
    
        template <typename T>
        bool send(T&& t) {
            return m_snd_ctx.cast<st::sender_context>().send(m_id, std::forward<T>(t));
        }

        std::shared_ptr<st::context> m_snd_ctx;
        std::size_t m_id;
    };
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

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between system 
 * threads. This is the mechanism that other implementors of 
 * `st::shared_sender_context<CRTP>` typically use internally.
 *
 * Listeners registered to this object with `listener(...)` will
 * compete for `st::message`s sent over it.
 *
 * All methods in this object are threadsafe.
 */
struct channel : public shared_sender_context<channel> {
    inline channel(){}
    inline channel(const channel& rhs) { context() = rhs.context(); }
    inline channel(channel&& rhs) { context() = std::move(rhs.context()); }
    inline virtual ~channel() { }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        channel ch;
        ch.context() = st::context::make<channel::context>();
        return ch;
    }

    /**
     * @return count of `st::thread`s blocked on `recv()` or are listening to this `st::channel`
     */
    inline std::size_t blocked_receivers() const {
        return context()->cast<channel::context>().blocked_receivers();
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
     * `recv()` is a simplified, more direct, and more limited, implementation 
     * of `st::shared_sender_context<st::channel>::listener(...)`.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return `true` on success, `false` if channel is terminated
     */
    inline bool recv(message& msg) {
        return context()->cast<channel::context>().recv(msg);
    }

private:
    struct blocker : public st::sender_context {
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

        blocker(data* d) : 
            m_data(d), 
            st::sender_context(
                st::context::type_info<detail::null_parent,st::channel::blocker>())
        { }

        ~blocker(){ m_data->signal(); }
    
        inline bool alive() const {
            return !flag;
        }

        inline void terminate(bool soft) {
            m_data->signal();
        }
        
        inline std::size_t queued() const {
            return 0;
        }

        inline bool send(message msg){ 
            m_data->signal(msg); 
            return true;
        }
        
        // do nothing
        inline bool listener(std::weak_ptr<st::sender_context> snd) { } 

        // override requeue
        inline bool requeue() const {
            return false;
        }
    
        data* m_data;
    };

    struct context : public st::sender_context {
        context() : 
            m_closed(false),
            st::sender_context(st::context::type_info<channel, channel::context>())
        { }

        virtual ~context(){ }

        inline bool alive() const { 
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_closed;
        }
        
        void terminate(bool soft);
        
        inline std::size_t queued() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_msg_q.size();
        }

        inline std::size_t blocked_receivers() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_listeners.size();
        }

        void handle_queued_messages(std::unique_lock<std::mutex>& lk);
        bool send(message msg);
        bool recv(message& msg);
        bool listener(std::weak_ptr<st::sender_context> snd);

        bool m_closed;
        mutable std::mutex m_mtx;
        std::deque<message> m_msg_q;
        std::deque<std::weak_ptr<st::sender_context>> m_listeners;
        friend st::shared_sender_context<st::channel>;
    };
};

//------------------------------------------------------------------------------
// SCHEDULERS

/**
 * @brief parent of `st::context`s which can schedule code for execution
 */
struct scheduler_context : public sender_context {
    scheduler_context() = delete;
    scheduler_context(const context&) = delete;
    scheduler_context(context&&) = delete;

    template <typename PARENT, typename SELF>
    scheduler_context(context::type_info<PARENT,SELF> ti) : st::sender_context(ti) { }

    /**
     * @brief schedule a generic task for execution 
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being terminated
     */
    virtual bool schedule(std::function<void()> f) = 0;
};

/**
 * @brief interface for objects which have shared `st::scheduler_context`s and 
 * are therefore capable of scheduling arbitrary code for execution.
 *
 * CRTP: Curiously Recurring Template Pattern
 */
template <typename CRTP>
struct shared_scheduler_context : public shared_sender_context<CRTP> {
    virtual ~shared_scheduler_context() { }

    /**
     * @brief schedule a generic task for execution 
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being terminated
     */
    inline bool schedule(std::function<void()> f) {
        return context()->cast<scheduler_context>().schedule(std::move(f));
    }

    /**
     * @brief wrap user function and arguments then schedule as a generic task for execution
     *
     * @param f function to execute on target sender 
     * @param as arguments for argument function
     * @return `true` on success, `false` on failure due to object being terminated
     */
    template <typename F, typename... As>
    bool schedule(F&& f, As&&... as) {
        return schedule([=]() mutable { f(std::forward<As>(as)...); });
    }
};

/**
 * @brief a thread object potentially managing its own system thread
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
 * Note: `st::threads`s automatically throw out any unallocated messages 
 * received over their internal `st::channel` instead of passing them to the 
 * `OBJECT`'s `recv()` implementation.
 *
 * All methods in this object are threadsafe.
 */
struct thread : public shared_scheduler_context<st::thread> {
    inline thread(){}
    inline thread(const st::thread& rhs) { context() = rhs.context(); }
    inline thread(st::thread&& rhs) { context() = std::move(rhs.context()); }

    virtual ~thread() {
        // Explicitly terminate the `st::thread` because a system thread 
        // holds a copy of this `st::thread` which keeps the channel alive even 
        // though the `st::thread` is no longer reachable.
        //
        // Because this logic only triggers on `st::thread` destructor, we are 
        // fine to destroy excess `st::thread::context`s during initialization 
        // until `st::thread::make<...>(...)` returns.
        if(context() && context().use_count() <= 2) {
            terminate();
        }
    }

    /**
     * @brief Empty `OBJECT` which only processes messages sent via `schedule()` ignoring all other messages.
     */
    struct processor { 
        inline void recv(st::message& msg) { }
    };

    /**
     * @brief statically construct a new system thread running user `OBJECT` associated with returned `st::thread`
     *
     * Because `st::thread`s allocation constructors are private, this function 
     * must be called to generate an allocated `st::thread`. This mechanism 
     * ensures that whenever an `st::thread` is constructed its `OBJECT` will be 
     * immediately running and capable of receiving `st::message`s.
     *
     * `st::thread`'s `OBJECT` will be allocated on the scheduled system thread, 
     * not the calling system thread. This allows usage of `thread_local` data 
     * where necessary.
     *
     * The user is responsible for holding a copy of the returned `st::thread`
     * to ensure the system thread does not shutdown and user `OBJECT` is kept 
     * in memory.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=processor, typename... As>
    static st::thread make(As&&... as) {
        st::thread thd;
        thd.context() = st::context::make<st::thread::context>();
        thd.context()->cast<st::thread::context>().launch_async<OBJECT>(std::forward<As>(as)...);
        return thd;
    }

    /**
     * @return the `std::thread::id` of the system thread this `st::thread` is running on
     */
    inline std::thread::id get_id() const {
        return context() ? context()->cast<st::thread::context>().get_thread_id() : std::thread::id();
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

    struct context : public st::scheduler_context, 
                     public std::enable_shared_from_this<st::thread::context> {
        context() : 
            m_shutdown(false), 
            m_ch(channel::make()),
            st::scheduler_context(st::context::type_info<st::thread, st::thread::context>())
        { }

        virtual ~context() { }

        // thread local data
        static std::weak_ptr<context>& tl_self();

        // looping recv function executed by a root thread
        void thread_loop(const std::function<void(message&)>& hdl);

        /*
         * Finish initializing the `st::thread` by allocating the `OBJECT` object 
         * and related handlers and then start the thread message receive loop. 
         *
         * Should be called on the scheduled parent `st::thread`.
         */
        template <typename OBJECT, typename... As>
        void init_loop(As&&... as) {
            data d = data::make<OBJECT>(std::forward<As>(as)...);
            
            // cast once to skip some processing indirection during msg handling
            OBJECT* obj = &(d->cast_to<OBJECT>());
            thread_loop([obj](message& msg) mutable { obj->recv(msg); });
        }

        // launch an `st::thread` running on a dedicated system thread
        template <typename OBJECT, typename... As>
        void launch_async(As&&... as) {
            std::shared_ptr<context> self = shared_from_this();
            m_self = self;

            std::thread thd([&,self]{ // keep a copy of this context in existence
                init_loop<OBJECT>(std::forward<As>(as)...);
            });

            m_thread_id = thd.get_id();
            thd.detach();
        }

        inline bool alive() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_shutdown;
        }
    
        void terminate(bool soft);
    
        inline std::size_t queued() const {
            return m_ch.queued();
        }

        inline bool send(message msg) {
            return m_ch.send(std::move(msg));
        }
        
        inline bool listener(std::weak_ptr<st::sender_context> snd) {
            m_ch.listener(std::move(snd));
        }
    
        inline bool schedule(std::function<void()> f) {
            return m_ch.send(0, task(std::move(f)));
        }

        inline std::thread::id get_thread_id() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_thread_id;
        }

        mutable std::mutex m_mtx;
        bool m_shutdown;
        channel m_ch; // internal thread channel
        std::weak_ptr<st::thread::context> m_self; // weak pointer to self
        std::thread::id m_thread_id; // thread id the user object is executing on
        friend st::shared_scheduler_context<st::thread>;
    };
};

}

#endif
