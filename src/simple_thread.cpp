#include "simple_thread.hpp"

std::mutex& st::detail::log_mutex() {
    static std::mutex m;
    return m;
}

void st::channel::context::close(bool process_remaining_messages) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if(!m_closed) {
        m_closed = true;

        if(!process_remaining_messages) {
            m_msg_q.clear();
        }

        if(m_msg_q.empty() && m_recv_q.size()) {
            m_recv_q.clear(); // allow receivers to shutdown
        }
    }
}

void st::channel::context::handle_queued_messages(std::unique_lock<std::mutex>& lk) {
    st::message msg;

    while(m_msg_q.size() && m_recv_q.size()) {
        std::unique_ptr<blocker> b = std::move(m_recv_q.front());
        m_recv_q.pop_front();
        msg = m_msg_q.front();
        m_msg_q.pop_front();

        lk.unlock();

        b->handle(msg);

        lk.lock();
    }

    if(m_closed && m_recv_q.size()) {
        m_recv_q.clear(); // allow receivers to shutdown
    }
}

bool st::channel::context::send(message msg) {
    std::unique_lock<std::mutex> lk(m_mtx);

    if(m_closed) {
        return false;
    } else {
        m_msg_q.push_back(std::move(msg));
        handle_queued_messages(lk);
        return true;
    } 
}

bool st::channel::context::recv(message& msg) {
    std::unique_lock<std::mutex> lk(m_mtx);

    if(!m_msg_q.empty()) {
        // retrieve message 
        msg = std::move(m_msg_q.front());
        m_msg_q.pop_front();
        return true;
    } else if(m_closed) {
        return false;
    } else {
        // block until message is available or channel close 
        while(!msg && !m_closed) { 
            st::channel::context::blocker::data bd(&msg);
            m_recv_q.push_back(
                std::unique_ptr<st::channel::context::blocker>(
                    new st::channel::context::blocker(&bd)));
            bd.wait(lk);
        }

        return msg ? true : false;
    } 
}

std::weak_ptr<st::thread::context>& st::thread::context::tl_self() {
    thread_local std::weak_ptr<context> wp;
    return wp;
}

void st::thread::context::thread_loop(const std::shared_ptr<context>& self, 
                                      const std::function<void()>& do_late_init) {
    // finish initialization
    do_late_init();

    // set thread local state
    detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = self;

    message msg;

    // block thread and listen for messages in a loop
    while(m_ch.recv(msg) && msg) { 
        m_hdl(msg);
    }
}

void st::thread::context::shutdown(bool process_remaining_messages) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if(m_shutdown) {
        return;
    } else {
        m_shutdown = true; 
        st::channel ch = m_ch;

        lk.unlock();
    
        // close channel outside of lock
        ch.close(process_remaining_messages);
    } 
}
