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

#include "simple_threading_message.hpp"
#include "simple_threading_sender.hpp"

namespace st { // simple thread

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

/**
 * @brief a coroutine intended to run on a parent `st::thread`
 *
 * According to wikipedia: Coroutines are computer program components that 
 * generalize subroutines for non-preemptive multitasking, by allowing 
 * execution to be suspended and resumed.
 *
 * The general advantages of using coroutines compared to system thread`s:
 * - Changing which coroutine is running by suspending its execution is 
 *   exponentially faster than changing which system thread is running. IE, the 
 *   more concurrent operations need to occur, the more efficient coroutines 
 *   generally become in comparison to threads.
 * - Faster context switching results in faster communication between code, 
 *   particularly between coroutines running on the same system thread.
 * - Coroutines take less memory than threads 
 * - The number of coroutines is not limited by the operating system
 * - Coroutines do not require system level calls to create
 *
 * The general disadvantages of using coroutines:
 * - Coroutines are expected to use only non-blocking operations to avoid
 *   blocking their parent thread.
 * - Coroutines cannot, by themselves, leverage multiple processor cores for 
 *   increased processing throughput. Coroutines must be run on multiple system 
 *   threads (the count of which should match the count of hardware CPUs for
 *   maximum CPU throughput) to leverage multiple processor cores.
 *
 * While more powerful coroutines are possible in computing, particularly with 
 * assembler level support which allocates stacks for coroutines, the best that 
 * can be accomplished at present in C++ is stackless coroutines. This means 
 * that code cannot be *arbitrarily* suspended and resumed at will (although 
 * this can be simulated with some complicated `switch` based hacks, which add  
 * significant complexity, and come with their own limitations. Further support 
 * for this kind of coroutine is provided in C++20 and onwards). 
 *
 * `st::fiber`s scheduled on a parent `st::thread` will take turns scheduling 
 * themselves so that no `st::fiber` (or the parent `st::thread`) starves. All 
 * calls to an `st::fiber`'s `st::fiber::schedule(...)` will internally call the 
 * parent `st::thread`'s `st::thread::schedule(...)`.
 *
 * This library allows the user to create `st::fiber` instances with user 
 * defined objects as a template argument in similar fashion to an `st::thread`:
 * `st::fiber::make<OBJECT>(st::thread parent, ...)`. 
 *
 * All methods in this object are threadsafe.
 */
struct fiber : public shared_scheduler_context<fiber> {
    inline fiber(){}
    inline fiber(const fiber& rhs) { context() = rhs.context(); }
    inline fiber(fiber&& rhs) { context() = std::move(rhs.context()); }
    virtual ~fiber() { }

    /**
     * @brief statically construct a new `st::fiber` running user `OBJECT`
     *
     * `st::fiber::make<OBJECT>(...)` functions identically in regard to user 
     * `OBJECT`s as `st::thread::make<OBJECT>(...)`.
     *
     * Because `st::fiber`s allocation constructors are private, this function 
     * must be called to generate an allocated `st::fiber`. This mechanism 
     * ensures that whenever an `st::fiber` is constructed its `OBJECT` will be 
     * immediately running and capable of receiving `st::message`s as long as
     * the parent `st::thread` is running.
     *
     * `st::fiber`'s `OBJECT` will be allocated on the scheduled system thread, 
     * not the calling system thread. This allows usage of `thread_local` data 
     * where necessary.
     *
     * The user is responsible for holding a copy of the returned `st::fiber`
     * to ensure the user `OBJECT` is kept in memory.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=st::thread::processor, typename... As>
    static fiber make(st::thread parent, As&&... as) {
        fiber f;
        f.context() = st::context::make<fiber::context>(std::move(parent));
        f.context()->cast<fiber::context>().launch_fiber<OBJECT>(std::forward<As>(as)...);
        return f;
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
     * @return the `st::fiber`'s parent `st::thread`
     */
    inline st::thread parent() const {
        return context()->cast<fiber::context>().parent();
    }

private:
    // private assistance sender to wakeup `st::fiber` 
    struct listener : public st::sender_context {
        context(std::weak_ptr<fiber::context> self, st::thread parent) : 
            m_ctx(std::move(self)),
            m_parent(std::move(parent)),
            st::sender_context(st::context::type_info<detail::null_parent,st::fiber::listener>())
        { }
    
        inline bool alive() const {
            return m_fib_ctx.lock();
        }

        inline void terminate(bool soft) {
            m_fib_ctx = std::shared_ptr<st::sender_context>();
        }

        bool send(st::message msg);
        
        // do nothing
        inline bool listener(std::weak_ptr<st::sender_context> snd) { } 

        std::weak_ptr<fiber::context> m_fib_ctx;
        st::thread m_parent;
    };

    struct context : public st::scheduler_context, 
                     public std::enable_shared_from_this<st::thread::context> {
        context(st::thread thd) : 
            m_alive_guard(true),
            m_thd(std::move(thd)),
            m_ch(st::channel::make()),
            st::scheduler_context(st::context::type_info<st::fiber, st::fiber::context>())
        { }

        // thread local data
        static std::weak_ptr<context>& tl_self();

        template <typename OBJECT, typename... As>
        inline void launch_fiber(As&&... as) {
            // properly set the thread_local self `st::thread` before `OBJECT` construction
            st::shared_ptr<fiber::context> self = shared_from_this();
            m_self = self;

            if(!m_parent.schedule([self,&]{
                detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
                tl_self() = m_self.lock();

                // construct the `OBJECT` 
                m_data = std::shared_ptr<data>(new data(
                    data::make<OBJECT>(std::forward<As>(as)...)));

                // cast once to skip some processing indirection during msg handling
                OBJECT* obj = &(m_data->cast_to<OBJECT>());

                // generate a message handler wrapper for `OBJECT`
                m_msg_hdl = [obj](message& msg) mutable { obj->recv(msg); };

                // register a listener to wakeup fiber 
                auto snd = st::context::make<fiber::listener>(m_self, m_thd);
                listener(snd->cast<st::sender_context>());
            })) {
                m_ch.terminate();
            }
        }
        
        // overload 
        virtual const std::string value() const;

        inline bool alive() const {
            return m_ch.alive();
        }

        void terminate(bool soft);

        inline bool send(st::message msg) {
            m_ch.send(std::move(msg));
        }
        
        inline bool listener(std::weak_ptr<st::sender_context> snd) {
            m_ch.listener(std::move(snd));
        }
    
        inline bool schedule(std::function<void()> f) {
            return m_parent.schedule(std::move(f));
        }

        void process_message();

        inline bool wakeup(std::shared_ptr<fiber::context>& self) {
            return m_parent.schedule([self]() mutable { 
                self->process_message(); 
            });
        }
        
        inline st::thread parent() const {
            return m_parent;
        }

        mutable std::mutex m_mtx;
        bool m_alive_guard;
        st::thread m_parent; // written once before message processing
        st::channel m_ch; // written once before message processing
        std::weak_ptr<fiber::context> m_self; // written once before message processing
        std::shared_ptr<data> m_data;
        std::function<void(message&)> m_msg_hdl; // written once before message processing
        std::deque<st::message> m_received_msgs;
        friend st::shared_scheduler_context<st::fiber>;
    };
};

}

#endif
