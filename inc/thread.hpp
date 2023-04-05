//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_THREADPOOL__
#define __SIMPLE_THREADING_THREADPOOL__

#include <mutex>
#include <utility>
#include <thread>

#include "utility.hpp"
#include "context.hpp"
#include "message.hpp"
#include "channel.hpp"

namespace st { // simple thread 

std::thread worker(st::channel ch);

template <typename MSG_HNDL>
std::thread worker(st::channel ch, MSG_HNDL&& hdl);

template <typename OBJ, typename MSG_HDL_METHOD>
std::thread worker(st::channel ch, OBJ&& obj, MSG_HNDL_METHOD&& hdl);

namespace detail {
namespace thread {
namespace pool {

struct context {
    context(std::size_t count) :
        shared_idx(0),
        workers(count ? 
            count : 
            std::hardware_concurrency() ? 
                std::hardware_concurrency() :
                1) { 
        for(auto& worker : workers) {
            worker.first = st::channel::make();
            worker.second = st::thread::worker(worker.first, [](st::message& msg){});
        }
    }

    ~context() {
        for(auto& worker : workers) {
            worker.first.close();
            worker.second.join();
        }
    }

    template <typename F, typename... As>
    bool schedule(F&& f, As&&... as) {
        idx=0;

        {
            std::lock_guard<std::mutex> lk(mtx);
            idx = shared_idx;
            ++shared_idx;

            if(shared_idx >= workers.size()) {
                shared_idx = 0;
            }
        }

        return workers[idx].first.schedule(
            std::forward<F>(f),
            std::forward<As>(as)...);
    }

    std::mutex mtx;
    std::size_t shared_idx=0;
    std::vector<std::pair<st::channel,std::thread>> workers;
};

}
}
}

namespace thread {

/**
 * @brief launch a worker thread which receives messages over a channel and handles the result 
 * @param ch channel to receive messages over 
 * @param hdl any Callable which can be invoked with an argument `st::message`
 * @return the launched `std::thread`
 */
template <typename MSG_HNDL>
std::thread worker(st::channel ch, MSG_HNDL&& hdl) {
    return std::thread([=]{
        for(auto& msg : ch) {
            hdl(msg);
        }
    });
}

/**
 * @brief launch a default worker thread which receives messages over a channel
 *
 * Useful for calling `st::channel::schedule` to asynchronously execute
 * arbitrary code.
 *
 * @param ch channel to receive messages over 
 * @return the launched `std::thread`
 */
inline std::thread worker(st::channel ch) {
    return worker(std::move(ch), [](st::message& msg){});
}
    
/**
 * @brief launch a worker thread which receives messages over a channel and handles the result 
 * @param ch channel to receive messages over 
 * @param obj an object 
 * @param mthd a method belonging to obj capable of being invoked with an argument `st::message`
 * @return the launched `std::thread`
 */
template <typename OBJ, typename MSG_HDL_METHOD>
std::thread worker(st::channel ch, OBJ&& obj, MSG_HNDL_METHOD&& mthd) {
    return worker(std::move(ch), [=](st::message& msg){ obj.mthd(msg); });
}

/**
 * @brief a collection of worker threads capable of executing arbitrary code 
 * 
 * When the last copy of an allocated pool goes out of scope, all workers will 
 * be killed and joined.
 */
struct pool : protected st::shared_context<pool,detail::thread::pool> {
    inline virtual ~pool() { }

    inline pool& operator=(const pool& rhs) {
        ctx() = rhs.ctx();
        return *this;
    }

    /**
     * @brief construct the shared context for this `st::thread::pool`
     * @param count the number of background worker threads to launch. If this value is 0, the algorithm will decide the count. A minimum of 1 thread will be launched
     * @return the constructed `st::thread::pool` with count running `st::thread::worker`s
     */
    static inline pool make(std::size_t count=0) {
        pool p;
        p.ctx(std::make_shared<detail::thread::pool::context>(count));
        return p;
    }

    /**
     * @brief schedule user Callable for asynchronous execution on a background worker thread
     * @param f any Callable 
     * @param as optional arguments for f 
     * @return the boolean value of the call to a worker thread's `st::channel::schedule()`
     */
    template <typename F, typename... As>
    bool schedule(F&& f, As&&... as) {
        return ctx() ? ctx()->schedule(std::forward<F>(f), std::forward<As>(as)...) : false;
    }
};

}
}

#endif
