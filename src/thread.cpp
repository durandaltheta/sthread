#include <deque>
#include "thread.hpp"

std::weak_ptr<st::thread::context>& st::thread::context::tl_self() {
    thread_local std::weak_ptr<st::thread::context> wp;
    return wp;
}

void st::thread::context::thread_loop(const std::function<void(message&)>& hdl) {
    // set thread local state
    st::detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = m_self.lock();

    // set this thread a listener to the channel
    std::shared_ptr<st::thread::listener_context> lst(
            new st::thread::listener_context(shared_from_this()));
    m_ch.listener(std::dynamic_pointer_cast<sender_context>(lst));

    message msg;

    // block thread and listen for messages in a loop
    std::unique_lock<std::mutex> lk(m_mtx);
    while(!m_shutdown) {
        while(m_received_msgs.size()) {
            msg = m_received_msgs.front();
            m_received_msgs.pop_front();

            lk.unlock();

            st::message::handle(hdl, msg); 

            lk.lock();
        }

        m_wakeup_cond.wait(lk);
    }
}

void st::thread::context::terminate(bool soft) {
    std::deque<st::message> received_msgs;

    std::unique_lock<std::mutex> lk(m_mtx);
    if(m_shutdown) {
        return;
    } else {
        m_shutdown = true; 
        st::channel ch = m_ch;

        if(!soft) {
            received_msgs = std::move(m_received_msgs);
            m_received_msgs.clear();
        }

        lk.unlock();
    
        // terminate channel outside of lock to deal with listener destructors
        ch.terminate(soft);

        if(!soft) {
            // terminate messages outside of lock to deal with payload destructors
            received_msgs.clear();
        }
    } 
}

bool st::thread::context::wakeup(st::message msg) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if(m_shutdown) {
        return false;
    } else {
        m_received_msgs.push_back(std::move(msg));
        m_wakeup_cond.notify_one();
        return true;
    }
}
