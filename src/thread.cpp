#include "thread.hpp"

std::weak_ptr<st::thread::context>& st::thread::context::tl_self() {
    thread_local std::weak_ptr<st::thread::context> wp;
    return wp;
}

void st::thread::context::thread_loop(const std::function<void(message&)>& hdl) {
    // set thread local state
    st::detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = m_self.lock();

    message msg;

    // block thread and listen for messages in a loop
    while(m_ch.recv(msg) && msg) { 
        if(msg.data().is<task>()) {
            msg.data().cast_to<task>()(); // evaluate task immediately
        } else {
            hdl(msg); // process message 
        }
    }
}

void st::thread::context::terminate(bool soft) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if(m_shutdown) {
        return;
    } else {
        m_shutdown = true; 
        st::channel ch = m_ch;

        lk.unlock();
    
        // terminate channel outside of lock
        ch.terminate(soft);
    } 
}
