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
    virtual ~thread() {
        // Explicitly terminate the `st::thread` because a system thread 
        // holds a copy of this `st::thread` which keeps the channel alive even 
        // though the `st::thread` is no longer reachable.
        //
        // Because this logic only triggers on `st::thread` destructor, we are 
        // fine to destroy excess `st::thread::context`s during initialization 
        // until `st::thread::make<...>(...)` returns.
        if(this->ctx() && this->ctx().use_count() <= 2) {
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
        thd.ctx(st::context::make<st::thread::context>());
        thd.ctx()->template cast<st::thread::context>().launch_async<OBJECT>(std::forward<As>(as)...);
        return thd;
    }

    /**
     * @return the `std::thread::id` of the system thread this `st::thread` is running on
     */
    inline std::thread::id get_id() const {
        return this->ctx() ? this->ctx()->template cast<st::thread::context>().get_thread_id() : std::thread::id();
    }

    /**
     * This static function is intended to be called from within an `OBJECT` 
     * running in an `st::thread`.
     *
     * @return a copy of the `st::thread` currently running on the calling thread, if none is running will return an unallocated `st::thread`
     */
    static inline st::thread self() {
        st::thread t;
        t.ctx(context::tl_self().lock());
        return t;
    }

private:
    struct context : public st::scheduler_context, 
                     public std::enable_shared_from_this<st::thread::context> {
        context() : m_shutdown(false), m_ch(channel::make()){ }
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
            OBJECT* obj = &(d.cast_to<OBJECT>());
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

        virtual inline bool alive() const {
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
            return m_ch.listener(std::move(snd));
        }
    
        virtual inline bool schedule(std::function<void()> f) {
            return m_ch.send(0, detail::task(std::move(f)));
        }

        inline std::thread::id get_thread_id() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_thread_id;
        }

        bool wakeup(st::message msg);

        inline bool requeue() { 
            return true; 
        }

        mutable std::mutex m_mtx;
        std::condition_variable m_wakeup_cond;
        bool m_shutdown;
        channel m_ch; // internal thread channel
        std::deque<st::message> m_received_msgs;
        std::weak_ptr<st::thread::context> m_self; // weak pointer to self
        std::thread::id m_thread_id; // thread id the user object is executing on
        friend st::shared_scheduler_context<st::thread>;
    };
    
    struct listener_context : public st::sender_context {
        listener_context(std::weak_ptr<st::thread::context> weak_ctx) : 
            m_weak_ctx(weak_ctx) 
        { }

        virtual bool alive() const {
            auto strong_ctx = m_weak_ctx.lock();
            return strong_ctx ? strong_ctx->alive() : false;
        }
    
        virtual void terminate(bool soft) { 
            // only reset members
            m_weak_ctx.reset();
        }

        virtual std::size_t queued() const {
            return 0;
        }

        virtual bool send(st::message msg) {
            auto strong_ctx = m_weak_ctx.lock();
            return strong_ctx ? strong_ctx->wakeup(msg) : false;
        }
    
        virtual bool listener(std::weak_ptr<st::sender_context> snd) {
            return true; // do nothing
        }

        virtual bool requeue() const {
            return true;
        }

        std::weak_ptr<st::thread::context> m_weak_ctx;
    };
};

}

#endif
