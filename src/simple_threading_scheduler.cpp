#include "simple_threading_scheduler.hpp"

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
