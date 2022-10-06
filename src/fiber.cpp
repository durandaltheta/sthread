#include "fiber.hpp"

std::weak_ptr<st::fiber::context>& st::fiber::context::tl_self() {
    thread_local std::weak_ptr<st::fiber::context> wp;
    return wp;
}

bool st::fiber::listener::context::send(st::message msg) {
    std::shared_ptr<st::fiber::context> fib_ctx = m_fib_ctx.lock();
    
    if(fib_ctx && m_parent) {
        return fib_ctx->wakeup(fib_ctx, std::move(msg));
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

bool st::fiber::context::wakeup(std::shared_ptr<fiber::context>& self, message msg) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_received_msgs.push_back(std::move(msg));
    return m_parent.schedule([self]() mutable { self->process_message(); });
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
