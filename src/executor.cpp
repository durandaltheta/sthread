#include <thread>

#include "executor.hpp"

void st::executor::context::terminate(bool soft) {
    st::channel ch;
    st::executor::context::worker_vector workers;

    std::unique_lock<std::mutex> lk(m_mtx);

    if(!m_shutdown) { // guard against multiple shutdowns
        m_shutdown = true;
        ch = m_ch;
        workers = std::move(m_workers);
        m_workers.clear();

        lk.unlock();

        // terminate outside the lock 
        ch.terminate(soft);

        for(auto& wkr : workers) {
            wkr.terminate(soft);
        }
    }
}

void st::executor::context::hire(
        std::size_t worker_count,
        std::function<st::thread()>& make) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_workers = st::executor::context::worker_vector(worker_count);

    for(auto& wkr : m_workers) {
        auto thd = make(); // allocate the worker
        listener(thd); // set the worker as a listener
        m_workers.push_back(std::move(thd));
    }
}
    
st::thread get_worker() const {
    st::thread wkr;

    std::lock_guard<std::mutex> lk(m_mtx);

    wkr = m_workers[m_cur_wkr];

    // rotate current worker index
    if(m_cur_wkr+1 < m_workers.size()) {
        ++m_cur_wkr;
    } else {
        m_cur_wkr = 0;
    }

    return wkr;
}

st::executor st::executor::instance() {
    static std::mutex mtx;
    static st::executor ex;

    // compiler will inline
    auto make = []{ 
        return st::executor::make<>(std::thread::hardware_concurrency()); 
    };

    std::lock_guard<std::mutex> lk(mtx);
    return ex ? ex.alive() ? ex 
                           : ex = make();
              : ex = make();
}
