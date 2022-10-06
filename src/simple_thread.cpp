#include "simple_thread.hpp"

bool& tl_fiber_printing() {
    thread_local bool printing=false;
    return printing;
}

std::string st::printable::value() const {
    return std::string("?");
}

const std::string st::channel::context::value() const { 
    std::stringstream ss;
    std::deque<message> msg_q;
    std::deque<std::weak_ptr<st::sender_context>> listeners;

    ss << "alive:(";

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ss << m_closed ? "false" : "true";

        // copy data to avoid both potential deadlock 
        for(auto& msg : m_msg_q) {
            // deep copy messages to avoid memory errors if messages are rvalue
            // manipulated 
            msg_q.push_back(msg.copy());
        }

        listeners = m_listeners;
    }

    {
        std::function<void(st::message&)> print = [&](st::message& msg) { 
            ss << msg;
            print = [&](st::message& msg) { ss << ", " << msg; };
        };

        ss << "), messages:(";

        for(auto& msg : msg_q) {
            print(msg);
        }

        ss << ")";
    }

    {
        std::function<void(st::sender_context&)> print = [&](st::sender_context& snd) { 
            ss << msg;
            print = [&](st::sender_context& msg) { ss << ", " << msg; };
        }

        ss << ", listeners:(";

        for(auto& weak_listener : listeners) {
            auto listener = weak_listener.lock();
            if(listener) {
                print(*listener);
            }
        }
        
        ss << ")";
    }
    
    return ss.str();
}


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

const std::string st::broadcast::context::value() const { 
    std::stringstream ss;
    st::broadcast::context::listener_map_t listeners;

    ss << "alive:(";

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ss << m_closed ? "false" : "true";
        listeners = m_listeners;
    }

    std::function<void(st::sender_context&)> print = [&](st::sender_context& snd) { 
        ss << msg;
        print = [&](st::sender_context& msg) { ss << ", " << msg; };
    }

    ss << "), listeners:(";

    for(auto& listener : listeners) {
        auto strong_listener = listener.second.lock();
        if(strong_listener) {
            print(*strong_listener);
        }
    }
    
    ss << ")";
    
    return ss.str();
}

bool st::broadcast::context::send(message msg) {
    std::unique_lock<std::mutex> lk(m_mtx);

    if(m_closed) {
        return false;
    } else {
        // copy listeners so that send can happen outside of lock ot avoid 
        // potential deadlock scenarios
        auto listeners = m_listeners;
        std::vector<void*> dead_listeners;

        lk.unlock();

        for(auto it = listeners.begin(); it != listeners.end(); ++it) {
            auto strong_listener = it->second.lock();
            if(strong_listener) {
                if(!strong_listener->send(msg.copy())) {
                    dead_listeners.push_back(it->first);
                }
            } else {
                dead_listeners.push_back(it->first);
            }

        }

        lk.lock();

        // cleanup any dead listeners
        for(auto listener_addr : dead_listeners) {
            auto it = m_listeners.find(listener_addr);
            if(it != m_listeners.end()) {
                m_listeners.erase(it);
            }
        }

        return true;
    } 
}

bool st::broadcast::context::listener(std::weak_ptr<st::sender_context> snd) {
    auto strong_snd = snd.lock();

    std::unique_lock<std::mutex> lk(m_mtx);
    if(m_closed || !strong_snd) {
        return false;
    } else {
        m_listeners.insert(strong_snd.get(), strong_snd);
        return true;
    }
}

std::weak_ptr<st::thread::context>& st::thread::context::tl_self() {
    thread_local std::weak_ptr<st::thread::context> wp;
    return wp;
}

const std::string st::thread::context::value() const { 
    // only print detailed information if fiber is not printing
    bool print_detail = tl_fiber_printing() ? false : true;
    std::stringstream ss;

    if(print_detail) {
        st::channel ch;

        {
            std::lock_guard<std::mutex> lk(m_mtx);
            ss << "alive:(" << m_shutdown ? "false" : "true";
            ss << "), thread id:(" << m_thread_id << ")";
            ch = m_ch;
        }

        ss << ", channel:(" << ch << ")";
    } else {
        std::lock_guard<std::mutex> lk(m_mtx);
        ss << "alive:(" << m_shutdown ? "false" : "true";
        ss << "), thread id:(" << m_thread_id << ")";
    }

    return ss.str();
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

const std::string st::fiber::context::value() const { 
    detail::hold_and_restore<bool> fiber_printing_har(tl_fiber_printing());

    std::stringstream ss;
    st::channel ch;
    st::thread parent;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ss << "alive:(" << m_shutdown ? "false" : "true";
        ch = m_ch;
        parent = m_parent;
    }

    ss << "), parent:(" << parent() << ")";
    ss << ", channel:(" << ch << ")";
    return ss.str();
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
