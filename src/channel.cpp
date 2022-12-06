#include "channel.hpp"

void st::channel::context::terminate(bool soft) {
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
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
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
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
}

bool st::channel::context::send(message msg) {
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
    std::unique_lock<std::mutex> lk(m_mtx);

    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
    if(m_closed) {
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
        return false;
    } else {
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
        m_msg_q.push_back(std::move(msg));
        handle_queued_messages(lk);
        return true;
    } 
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
}

bool st::channel::context::recv(message& msg) {
    std::unique_lock<std::mutex> lk(m_mtx);

    if(!m_msg_q.empty()) {
        // retrieve message immediately
        msg = std::move(m_msg_q.front());
        m_msg_q.pop_front();
        return true;
    } else if(m_closed) {
        return false;
    } else {
        // block until message is available or channel termination
        while(!msg && !m_closed) { 
            st::channel::blocker::data d(&msg);
            m_blockers.push_back(
                    std::shared_ptr<st::channel::blocker>(
                        new st::channel::blocker(&d)));
            d.wait(lk);
        }

        return msg ? true : false;
    } 
}
