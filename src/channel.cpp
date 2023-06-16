#include "channel.hpp"

void st::channel::context::close(bool soft) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if(!m_closed) {
        m_closed = true;

        if(!soft) {
            m_msg_q.clear();
        }

        if(m_msg_q.empty() && m_blockers.size()) {
            m_blockers.clear(); // allow receivers to terminate
        }
    }
}

void st::channel::context::handle_queued_messages(std::unique_lock<std::mutex>& lk) {
    st::message msg;

    while(m_msg_q.size() && m_blockers.size()) {
        std::shared_ptr<st::channel::blocker> s = m_blockers.front();
        m_blockers.pop_front();
        msg = m_msg_q.front();
        m_msg_q.pop_front();

        lk.unlock();
        s->send(msg);
        lk.lock();
    }

    if(m_closed && m_blockers.size()) {
        m_blockers.clear(); // allow receivers to terminate
    }
}

bool st::channel::context::send(st::message msg) {
    std::unique_lock<std::mutex> lk(m_mtx);

    if(m_closed) {
        return false;
    } else {
        m_msg_q.push_back(std::move(msg));
        handle_queued_messages(lk);
        return true;
    } 
}

st::channel::state st::channel::context::recv(st::message& msg, bool block) {
    std::unique_lock<std::mutex> lk(m_mtx);

    do {
        if(!m_msg_q.empty()) {
            // can safely loop here until message queue is empty or we can return 
            // a message, even when block == false
            while(!m_msg_q.empty()) {
                // retrieve message immediately
                msg = std::move(m_msg_q.front());
                m_msg_q.pop_front();

                if(process(msg)) {
                    return st::channel::success;
                }
            }
        } else if(m_closed) {
            return st::channel::closed;
        } else if(block) {
            // block until message is available or channel termination
            while(!msg && !m_closed) { 
                st::channel::blocker::data d(&msg);
                m_blockers.push_back(
                        std::shared_ptr<st::channel::blocker>(
                            new st::channel::blocker(&d)));
                d.wait(lk);
            }

            if(process(msg)) {
                return st::channel::success;
            }
        } 
    } while(block); // on blocking receive, loop till we receive a non-task message or channel is closed

    // since we didn't early return out the receive loop, then this is a failed try_recv()
    return st::channel::failure;
}

// return true if the message can be returned to the user, else false
bool st::channel::context::process(st::message& msg) {
    if(msg) { 
        if(msg.data() && 
           msg.data().is<st::channel::task>() &&
           !msg.data().cast_to<st::channel::task>().forward()) { 
            // evaluate task immediately
            msg.data().cast_to<st::channel::task>()(); 

            // we already handled the message, do not return to user
            return false;
        } else {
            // message is valid and ready to be returned to the user
            return true;
        }
    } else {
        // throw out any empty messages
        return false;
    }
}
