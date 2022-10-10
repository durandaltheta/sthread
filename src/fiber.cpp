#include "fiber.hpp"

std::weak_ptr<st::fiber::context>& st::fiber::context::tl_self() {
    thread_local std::weak_ptr<st::fiber::context> wp;
    return wp;
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

std::size_t st::fiber::context::queued() const {
    std::size_t recvd = 0;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        recvd = m_received_msgs.size();
    }

    return m_ch.queued() + recvd;
}

bool st::fiber::context::schedule(std::function<void()> f) {
    auto self = m_self.lock(); // hold a copy of self to keep fiber in memory
    return m_parent.schedule([self,f]() mutable {
        // set thread_local fiber context 
        detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
        tl_self() = self;
        f();
    });
}

bool st::fiber::context::wakeup(message msg) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_received_msgs.push_back(std::move(msg));
    return schedule([&]{ process_message(); });
}

void st::fiber::context::process_message() {
    message msg;

    std::unique_lock<std::mutex> lk(m_mtx);

    if(m_received_msgs.size()) {
        msg = std::move(m_received_msgs.front());
        m_received_msgs.pop_front();

        lk.unlock();

        if(msg) {
            m_msg_hdl(msg); // process message 
        }
    } 
}

bool st::fiber::wakeup::send(st::message msg) {
    std::shared_ptr<st::fiber::context> fib_ctx = m_fib_ctx.lock();
    
    if(fib_ctx && m_parent) {
        return fib_ctx->wakeup(std::move(msg));
    } else {
        return false;
    }
}
