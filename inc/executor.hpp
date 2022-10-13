//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_EXECUTOR__
#define __SIMPLE_THREADING_EXECUTOR__ 

#include <vector>

#include "utility.hpp"
#include "context.hpp"
#include "channel.hpp"
#include "thread.hpp"

namespace st { // simple thread

struct executor : public st::shared_scheduler_context<executor> {
    virtual ~executor() { }
    
    /**
     * @brief allocate an `st::executor` then allocate and append `worker_count` `st::thread`s to the internal worker container 
     *
     * All hired workers will be passed to `st::channel::listener(wkr)` on the 
     * internal `st::channel` to receive `st::message`s sent to the 
     * `st::channel`. This mechanism allows `st::executor::send(...)` and 
     * `st::executor::schedule(...)` to distribute `st::message`s between its 
     * workers.
     *
     * @param worker_count count of worker `st::thread`s to append 
     * @param as optional `OBJECT` constructor arguments
     * @return an allocated `st::executor`
     */
    template <typename OBJECT=processor, typename... As>
    static st::executor make(std::size_t worker_count, As&&... as) {
        st::executor ex;
        ex.ctx(st::context::make<st::executor::context>(
                    worker_count,
                    detail::hint<OBJECT>(), 
                    std::forward<As>(as)...));
        return thd;
    }

    /**
     * The `st::executor` returned by this function is not allocated until the 
     * first call to this function. This is a convenient mechanism if the user 
     * does not want to configure or manage their own high CPU throughput 
     * `st::executor`. 
     *
     * If the instance is terminated with `st::executor::terminate(...)` it will 
     * be reconstructed on the next call to `st::executor::instance()`.
     * However, calling `st::executor::terminate(...)` can cause unexpected 
     * failures for any code already holding a copy of
     * `st::executor::instance()` and should be avoided unless truly needed.
     *
     * The statically allocated `st::executor` will always be terminated and 
     * cleaned up at program termination.
     *
     * @return a singleton instance of a process wide `st::executor`
     */
    static st::executor instance();

    /**
     * @return count of worker `st::thread`s
     */
    inline std::size_t count() const {
        return ctx().template cast<st::executor::context>().count();
    }

    /**
     * @brief "convert" an `st::executor` to an `st::thread`
     *
     * This operation returns an individual worker `st::thread` maintained by 
     * the parent `st::executor`. 
     *
     * This `st::thread` can be used normally:
     * st::executor e = st::executor::make<>();
     * st::thread thd = e; // acquire a copy of an `st::executor`'s worker 
     * e.send(...);
     *
     * This worker can also be used when making `st::fiber`s. The
     * `st::executor` will be automatically converted to `st::thread` when 
     * passed as the `st::fiber`'s parent:
     *
     * st::executor e = st::executor::make<>();
     * st::fiber f = st::fiber::make<OBJECT>(e, ...); 
     *
     * @return a worker `st::thread`
     */
    inline operator st::thread() const {
        return ctx()->template cast<st::executor::context>().get_worker()
    }

private:
    struct context : public st::scheduler_context {
        typedef std::vector<std::shared_ptr<st::scheduler_context>> worker_vector;

        template <typename OBJECT, typename... As>
        context(std::size_t worker_count, detail::hint<OBJECT>, As&&... as) : 
            m_shutdown(false),
            m_ch(st::channel::make())
        { 
            // don't forward as to avoid possible memory moving
            std::function<st::thread()> make(
                    []{ return st::thread::make<OBJECT>(as...); });
            // enforce 1 worker
            hire(worker_count ? worker_count : 1, make);
        }

        virtual inline bool alive() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_shutdown;
        }
    
        void terminate(bool soft);
    
        inline std::size_t queued() const {
            // should always return 0 as `st::message`s are immediately 
            // distributed to listeners
            return m_ch.queued(); 
        }

        inline bool send(message msg) {
            return m_ch.send(std::move(msg));
        }
        
        inline bool listener(std::weak_ptr<st::sender_context> snd) {
            return m_ch.listener(std::move(snd));
        }

        inline bool requeue() { 
            return true; 
        }
    
        inline bool schedule(std::function<void()> f) {
            return m_ch.send(0, st::message::task(std::move(f)));
        }

        void hire(std::worker_count, std::function<st::thread()>& make);
        st::thread get_worker() const; 

        inline std::size_t count() const {
            return m_workers.size();
        }

        mutable std::mutex m_mtx;
        bool m_shutdown;
        mutable std::size_t m_cur_wkr=0;
        st::channel m_ch;
        st::executor::worker_vector m_workers;
    };
};

/**
 * @brief `st::thread::schedule(...)` a function for execution on a background `st::thread`
 *
 * This operation calls `st::executor::instance()`, so if usage of said function 
 * is not desired then this function should not be called.
 *
 * @param as arguments to pass to `st::thread::schedule(...)`
 */
template <typename... As>
void schedule(As&&... as) {
    return ((st::thread)st::executor::instance()).schedule(std::forward<As>(as)...);
}

/**
 * @brief allocate and schedule an `OBJECT` as an `st::fiber` on some system thread
 *
 * This operation calls `st::executor::instance()`, so if usage of said function 
 * is not desired then this function should not be called.
 *
 * This mechanism is a good default concurrent scheduling operation in 
 * applications that need a large amount of concurrent operations to execute.
 *
 * If an `st::fiber` scheduled via this function needs to launch another 
 * concurrent operation where maximized communication speed is desired, user 
 * `OBJECT` can call `st::fiber::make<OBJECT2>(st::thread::self(), ...)` to 
 * schedule the other `st::fiber` on the same system thread.
 *
 * As usual, the caller of `st::go(...)` is responsible for keeping a copy of 
 * the returned `st::fiber` to keep it in memory.
 *
 * @param as optional `OBJECT` constructor arguments
 * @return allocated and scheduled `st::fiber`
 */
template <typename OBJECT, typename... As>
st::fiber go(As&&... as) {
    return st::fiber::make<OBJECT>(st::executor::instance(), std::forward<As>(as)...);
}

}

#endif
