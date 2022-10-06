//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_FIBER__
#define __SIMPLE_THREADING_FIBER__

#include <memory>
#include <mutex>
#include <thread>
#include <deque>

#include "message.hpp"
#include "scheduler.hpp"

namespace st { // simple thread

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
            m_parent(std::move(parent))
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
            m_ch(st::channel::make())
        { }

        // thread local data
        static std::weak_ptr<context>& tl_self();

        template <typename OBJECT, typename... As>
        inline void launch_fiber(As&&... as) {
            // properly set the thread_local self `st::thread` before `OBJECT` construction
            st::shared_ptr<fiber::context> self = shared_from_this();
            m_self = self;

            // finish initialization on parent thread
            if(!schedule([self,&]{
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
            return m_parent.schedule([f]{
                // set thread_local fiber context 
                detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
                tl_self() = m_self.lock();
                f();
            });
        }

        void process_message();

        bool wakeup(std::shared_ptr<fiber::context>& self, message msg);
        
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
