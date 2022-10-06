#include "simple_threading_sender.hpp"

void st::channel::context::terminate(bool soft) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if(!m_closed) {
        m_closed = true;

        if(!soft) {
            m_msg_q.clear();
        }

        if(m_msg_q.empty() && m_listeners.size()) {
            m_listeners.clear(); // allow receivers to terminate
        }
    }
}

void st::channel::context::handle_queued_messages(std::unique_lock<std::mutex>& lk) {
    st::message msg;

    while(m_msg_q.size() && m_listeners.size()) {
        std::shared_ptr<st::sender_context> s = m_listeners.front().lock();
        m_listeners.pop_front();
        msg = m_msg_q.front();
        m_msg_q.pop_front();

        lk.unlock();

        bool success = s.send(msg);

        lk.lock();

        if(success) {
            if(!m_closed && s.requeue()) {
                m_listeners.push_back(std::move(s));
            }
        } else {
            // push to the front of the queue even if channel is closed to 
            // handle message send failure
            m_msg_q.push_front(std::move(msg));
        }
    }

    if(m_closed && m_listeners.size()) {
        m_listeners.clear(); // allow receivers to terminate
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
        // block until message is available or channel termination
        while(!msg && !m_closed) { 
            std::shared_ptr<st::channel::blocker> bd(new st::channel::blocker(&msg));
            listener(bd);
            bd->wait(lk);
        }

        return msg ? true : false;
    } 
}
        
bool st::channel::context::listener(std::weak_ptr<st::sender_context> snd) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if(m_closed) { 
        return false;
    } else {
        m_listeners.push_back(std::move(snd));
        return true;
    }
}

std::weak_ptr<st::thread::context>& st::thread::context::tl_self() {
    thread_local std::weak_ptr<st::thread::context> wp;
    return wp;
}

void st::thread::context::thread_loop(const std::function<void(message&)& hdl)
    // set thread local state
    detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
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
