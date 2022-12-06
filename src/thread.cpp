#include <deque>
#include "thread.hpp"

st::thread::~thread() {
    // Explicitly terminate the `st::thread` because a system thread 
    // holds a copy of this `st::thread` which keeps the channel alive even 
    // though the `st::thread` is no longer reachable.
    //
    // Because this logic only triggers on `st::thread` destructor, we are 
    // fine to destroy excess `st::thread::context`s during initialization 
    // until `st::thread::make<...>(...)` returns.
    if(ctx() && ctx().use_count() <= 2) {
        terminate();
    }
}

std::weak_ptr<st::thread::context>& st::thread::context::tl_self() {
    thread_local std::weak_ptr<st::thread::context> wp;
    return wp;
}

void st::thread::context::thread_loop(const std::function<void(message&)>& hdl) {
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
    // set thread local state
    st::detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = m_self.lock();

    // block thread and listen for messages in a loop
    std::unique_lock<std::mutex> lk(m_mtx);
    while(!m_shutdown) {
        lk.unlock();

        message msg;
        
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
        if(m_ch.recv(msg)) {
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
            st::message::handle(hdl, msg); 
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
        }

        lk.lock();
    }

    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
}

void st::thread::context::terminate(bool soft) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if(!m_shutdown) {
        m_shutdown = true; 
        m_ch.terminate(soft);
    } 
}
