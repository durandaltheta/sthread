//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CHANNEL__
#define __SIMPLE_THREADING_CHANNEL__

#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>

#include "utility.hpp"
#include "sender.hpp"

namespace st { // simple thread

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between system 
 * threads. This is the mechanism that other implementors of 
 * `st::shared_sender_context<CRTP>` typically use internally.
 *
 * Listeners registered to this object with `listener(...)` will
 * compete for `st::message`s sent over it.
 *
 * All methods in this object are threadsafe.
 */
struct channel : public shared_sender_context<channel> {
    inline channel(){}
    inline channel(const channel& rhs) { context() = rhs.context(); }
    inline channel(channel&& rhs) { context() = std::move(rhs.context()); }
    inline virtual ~channel() { }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        channel ch;
        ch.context() = st::context::make<channel::context>();
        return ch;
    }

    /**
     * @return count of `st::thread`s blocked on `recv()` or are listening to this `st::channel`
     */
    inline std::size_t blocked_receivers() const {
        return context()->cast<channel::context>().blocked_receivers();
    }

    /**
     * @brief optionally enqueue the argument message and receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue.
     *
     * A successful call to `recv()` will remove a message queued by `send()` 
     * from the internal channel message queue.
     *
     * Multiple simultaneous `recv()` calls will be served in the order they 
     * were called.
     *
     * `recv()` is a simplified, more direct, and more limited, implementation 
     * of `st::shared_sender_context<st::channel>::listener(...)`.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return `true` on success, `false` if channel is terminated
     */
    inline bool recv(message& msg) {
        return context()->cast<channel::context>().recv(msg);
    }

private:
    struct blocker : public st::sender_context {
        struct data {
            data(message* m) : msg(m) { }

            inline void wait(std::unique_lock<std::mutex>& lk) {
                do {
                    cv.wait(lk);
                } while(!flag);
            }

            inline void signal() {
                flag = true;
                cv.notify_one(); 
            }

            inline void signal(message& m) {
                *msg = std::move(m);
                signal();
            }

            bool flag = false;
            std::condition_variable cv;
            message* msg;
        };

        blocker(data* d) : m_data(d) { }
        ~blocker(){ m_data->signal(); }
    
        inline bool alive() const {
            return !flag;
        }

        inline void terminate(bool soft) {
            m_data->signal();
        }
        
        inline std::size_t queued() const {
            return 0;
        }

        inline bool send(message msg){ 
            m_data->signal(msg); 
            return true;
        }
        
        // do nothing
        inline bool listener(std::weak_ptr<st::sender_context> snd) { } 

        // override requeue
        inline bool requeue() const {
            return false;
        }
    
        data* m_data;
    };

    struct context : public st::sender_context {
        context() : m_closed(false) { }
        virtual ~context(){ }

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
            return m_listeners.size();
        }

        void handle_queued_messages(std::unique_lock<std::mutex>& lk);
        bool send(message msg);
        bool recv(message& msg);
        bool listener(std::weak_ptr<st::sender_context> snd);

        bool m_closed;
        mutable std::mutex m_mtx;
        std::deque<message> m_msg_q;
        std::deque<std::weak_ptr<st::sender_context>> m_listeners;
        friend st::shared_sender_context<st::channel>;
    };
};

}

#endif
