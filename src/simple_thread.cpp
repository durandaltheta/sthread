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

void st::channel::handle_queued_messages(std::unique_lock<std::mutex>& lk) {
    st::messsage msg;

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
            blocker::data blkd(&msg);
            m_recv_q.push_back(std::unique_ptr<blocker>(
                std::dynamic_cast<blocker*>(new blocker(&blkd))));
            blkd->wait(lk);
        }

        return msg ? true : false;
    } 
}

bool st::channel::context::listener(std::unique_ptr<st::channel::receiver> rcv) {
    std::unique_lock<std::mutex> lk(m_mtx);

    if(m_closed) {
        return false;
    } else {
        m_recv_q.push_back(std::move(rcv));
        handle_queued_messages(lk);
        return true;
    }
}

st::fiber::~fiber() {
    // explicitly shutdown root fiber channel because a system thread 
    // holds a copy of this fiber which keeps the channel alive even 
    // though the root fiber is no longer reachable
    if(m_context &&
       m_context.get() == root().m_context.get() &&
       m_context.use_count() < 3) {
        shutdown();
    }
}

st::fiber::context::~context() {
    shutdown();
}

std::weak_ptr<context>& st::fiber::context::tl_self() {
    thread_local std::weak_ptr<context> wp;
    return wp;
}

std::weak_ptr<context>& st::fiber::context::tl_root() {
    thread_local std::weak_ptr<context> wp;
    return wp;
}

void st::fiber::context::thread_loop(const std::shared_ptr<context>& self, 
                                     const std::function<void()>& do_late_init) {
    // finish initialization
    do_late_init();

    // set thread local state
    detail::hold_and_restore<std::weak_ptr<context>> root_har(tl_root());
    detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_root() = self;
    tl_self() = self;

    message msg;

    // block thread and listen for messages in a loop
    while(m_ch.recv(msg) && msg) { 
        m_hdl(msg);
    }
}

st::message::task st::fiber::context::nonblocking_fiber_task() const {
    // hold a copy of self to keep memory allocated during processing
    std::shared_ptr<context> self = m_self.lock();
    return [&,self]{ nonblocking_process_message(); };
}
        
bool st::fiber::context::nonblocking_process_message() {
    message msg;
    detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
    tl_self() = self;
    
    std::unique_lock<std::mutex> lk(m_mtx);

    // process one received message
    if(m_received_msgs.size()) {
        msg = std::move(m_received_msgs.front());
        m_received_msgs.pop_front();

        lk.unlock();

        if(msg) {
            m_hdl(msg);
        }

        lk.lock();
    } 

    wakeup(lk);
}

void st::fiber::context::shutdown(bool process_remaining_messages) {
    st::channel ch;
    std::unique_ptr<std::vector<fiber>> threadpool_children; 

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if(!m_shutdown) {
            m_shutdown = true; 

            if(!process_remaining_messages) {
                m_received_msgs.clear();
            }
        
            ch = m_ch;

            if(m_threadpool_children) {
                threadpool_children = std::move(m_threadpool_children);
            }
        } else {
            return;
        }
    }

    // close channel and child fibers outside of lock
    if(ch) {
        ch.close(process_remaining_messages)
    }

    if(threadpool_children) {
        for(auto& child : *threadpool_children) {
            child.shutdown(process_remaining_messages);
        }
    }
}

bool st::fiber::context::handle_received_message(st::message& msg) {
    bool success = true;

    std::unique_lock<std::mutex> lk(m_mtx);
    if(m_shutdown) {
        success = false;
    } else {
        m_received_msgs.push_back(msg);
        success = wakeup(lk);

        if(success) {
            msg = st::message();
        }
    } // else fiber is already running 

    return success;
}
