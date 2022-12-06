//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CHANNEL__
#define __SIMPLE_THREADING_CHANNEL__

#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>

#include "utility.hpp"
#include "sender_context.hpp"

namespace st { // simple thread

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between system 
 * threads. This is the mechanism that other implementors of 
 * `st::shared_sender_context<CRTP>` typically use internally.
 *
 * All methods in this object are threadsafe.
 */
struct channel : public shared_sender_context<channel> {
    inline virtual ~channel() { }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        channel ch;
        ch.ctx(st::context::make<channel::context>());
        return ch;
    }

    /**
     * @return count of `st::thread`s blocked on `recv()` or are listening to this `st::channel`
     */
    inline std::size_t blocked_receivers() const {
        return ctx()->template cast<channel::context>().blocked_receivers();
    }

    /**
     * @brief receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue. This will return early if 
     * `st::channel::terminate()` is called.
     *
     * A successful call to `recv()` will remove a message queued by `send()` 
     * from the internal channel message queue.
     *
     * Multiple simultaneous `recv()` calls will be served in the order they 
     * were called.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return `true` on success, `false` if channel is terminated
     */
    inline bool recv(message& msg) {
        return ctx()->template cast<channel::context>().recv(msg);
    }

    inline channel& operator=(const channel& rhs) {
        ctx() = rhs.ctx();
        return *this;
    }

private:
    // private class used to implement `recv()` behavior
    struct blocker {
        // stack variables
        struct data {
            data(message* m) : msg(m) { }

            inline void wait(std::unique_lock<std::mutex>& lk) {
                do {
                    cv.wait(lk);
                } while(!flag);
            }

            inline void signal() {
                if(!flag) {
                    flag = true;
                    cv.notify_one(); 
                }
            }

            inline void send(message& m) {
                *msg = std::move(m);
                signal();
            }

            bool flag = false;
            std::condition_variable cv;
            message* msg;
        };

        blocker(data* d) : m_data(d) { }
        ~blocker(){ m_data->signal(); }

        inline void send(message msg){ 
            m_data->send(msg); 
        }
    
        data* m_data;
    };

    struct context : public st::sender_context {
        context() : m_closed(false) { }
        virtual ~context(){ 
            terminate(true); 
        }

        inline bool alive() const { 
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_closed;
        }
        
        void terminate(bool soft);
        
        inline std::size_t queued() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_msg_q.size();
        }

        inline std::size_t blocked_receivers() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_blockers.size();
        }

        void handle_queued_messages(std::unique_lock<std::mutex>& lk);
        bool send(message msg);
        bool recv(message& msg);

        bool m_closed;
        mutable std::mutex m_mtx;
        std::deque<message> m_msg_q;
        std::deque<std::shared_ptr<blocker>> m_blockers;
        friend st::shared_sender_context<st::channel>;
    };
};

}

#endif
