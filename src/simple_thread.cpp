#include "simple_thread.hpp"

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
            register_listener(bd);
            bd->wait(lk);
        }

        return msg ? true : false;
    } 
}

std::weak_ptr<st::thread::context>& st::thread::context::tl_self() {
    thread_local std::weak_ptr<st::thread::context> wp;
    return wp;
}

void st::thread::context::thread_loop(
        const std::function<void(message&)& msg_hdl
        const std::function<void()& blk_hdl) {
    // set thread local state
    detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = m_self.lock();

    message msg;

    // block thread and listen for messages in a loop
    while(m_ch.recv(msg) && msg) { 
        if(msg.data().is<task>()) {
            msg.data().cast_to<task>()(); // evaluate task immediately
        } else {
            msg_hdl(msg); // process message 
        }

        if(!m_ch.queued()) {
            blk_hdl(); // handle no message state
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

std::weak_ptr<st::fiber::context>& st::fiber::context::tl_self() {
    thread_local std::weak_ptr<st::fiber::context> wp;
    return wp;
}

bool st::fiber::listener::context::send(st::message msg) {
    std::shared_ptr<st::fiber::context> fib_ctx = m_fib_ctx.lock();
    
    if(fib_ctx && m_parent) {
        return parent.schedule([ctx]() mutable { fib_ctx->wakeup(fib_ctx); });
    } else {
        return false;
    }
}
        
void st::fiber::context::terminate(bool soft) {
    st::channel ch;
    std::deque<st::message> received_msgs;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if(m_alive_guard && m_ch.alive()) {
            m_alive_guard = false; // guard against multiple calls to this function
            ch = m_ch;
            m_ch = st::channel();

            if(!soft && m_received_msgs.size()) {
                received_msgs = std::move(m_received_msgs);
            }
        }
    }

    // terminate outside of lock
    if(ch) {
        ch.terminate(soft);
    }
   
    // clear outside of lock to prevent deadlock
    received_msgs.clear();
}

void st::fiber::context::process_message() {
    detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = self;

    message msg;

    std::unique_lock<std::mutex> lk(m_mtx);

    if(m_received_msgs.size()) {
        msg = std::move(m_received_msgs.front());
        m_received_msgs.pop_front();

        if(m_received_msgs.size()) {
            wakeup(m_lock.self()); // schedule self to process the next message
        }

        lk.unlock();

        if(msg) {
            m_msg_hdl(msg); // process message 
        }
    } else {
        lk.unlock();
        m_blk_hdl(); // handle no messages state
    }
}
