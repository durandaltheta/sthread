#include <deque>
#include "thread.hpp"

st::thread::~thread() { 
    if(m_ch && m_close_on_destruct) {
        m_ch.close(m_close_soft);
    }

    if(m_join_on_destruct) {
        join();
    }
}

void st::thread::object_recv_loop(const std::function<void(message&)>& hdl) {
    // set thread local state
    st::detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = m_self.lock();

    // block thread and listen for messages in a loop
    std::unique_lock<std::mutex> lk(m_mtx);
    while(!m_shutdown) {
        lk.unlock();

        message msg;
        
        if(m_ch.recv(msg)) {
            st::message::handle(hdl, msg); 
        }

        lk.lock();
    }
}
