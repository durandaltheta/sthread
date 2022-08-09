//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis
/**
 * @file
 * @brief Simple interprocess threading and messaging
 */

#ifndef __SIMPLE_THREADING__
#define __SIMPLE_THREADING__

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <typeinfo>
#include <functional>

namespace st { // simple thread

/**
 * @brief convenience and readability aid type alias to std::shared_ptr 
 */
template <typename T>
using sptr = std::shared_ptr<T>;

/**
 * @brief Typedef representing the unqualified type of T
 */
template <typename T>
using base = typename std::remove_reference<typename std::remove_cv<T>::type>::type;

/**
 * The data type value is acquired by removing const and volatile 
 * qualifiers and then by acquiring the type_info::hash_type_code().
 *
 * @return an unsigned integer representing a data type.
 */
template <typename T>
static constexpr std::size_t type_code() {
    return typeid(base<T>).hash_type_code();
}

/**
 * @brief convenience inheritable type for determing type erased value at runtime
 */
struct type_aware {
    type_aware() : m_type_code(0) { }
    type_aware(const std::size_t type_code) : m_type_code(type_code) { }

    virtual ~type_aware(){}
   
    /**
     * @brief determine whether the stored data type matches the templated type.
     *
     * @return true if the unqualified type of T matches the data type, else false
     */
    template <typename T>
    bool is() const {
        return m_type_code == type_code<T>();
    }

protected: 
    std::size_t m_type_code;
};

/**
 * @brief Interthread type erased message container
 *
 * This object is *not* mutex locked beyond what is included in the 
 * `sptr` implementation.
 */
struct message : public type_aware {
    /** 
     * @brief convenience function for templating 
     * @param msg message object to immediately return 
     * @return message object passed as argument
     */
    static inline sptr<message> make(sptr<message> msg) {
        return std::move(msg);
    }

    /**
     * @brief construct a message
     *
     * This is a sanity overload for handling people sending char arrays by 
     * deep copying the array into an std::string.
     *
     * @param id an unsigned integer representing which type of message
     * @param s c-string to be sent as an std::string
     * @return an allocated message
     */
    template <typename ID>
    static sptr<message> make(ID id, const char* s) {
        return sptr<message>(new message(
            static_cast<std::size_t>(id),
            type_code<std::string>(),
            data_pointer_t(
                allocate<std::string>(std::string(s),
                message::deleter<std::string>))));
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @param t arbitrary typed data to be stored as the message data 
     * @return an allocated message
     */
    template <typename ID, typename T>
    static sptr<message> make(ID id, T&& t) {
        return sptr<message>(new message(
            static_cast<std::size_t>(id),
            type_code<T>(),
            data_pointer_t(
                allocate<T>(std::forward<T>(t)),
                message::deleter<T>)));
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @return an allocated message
     */
    template <typename ID>
    static sptr<message> make(ID id) {
        return sptr<message>(new message(
            static_cast<std::size_t>(id), 
            0,
            data_pointer_t(nullptr, message::no_delete)));
    }

    /**
     * @return an unsigned integer representing message's intended operation
     *
     * An `id` can trivially represent an enumeration, which can represent a 
     * specific request, response, or notification operation.
     */
    inline std::size_t id() const {
        return m_id;
    }

    /**
     * @brief cast message data payload to templated type reference 
     *
     * NOTE: this function is *NOT* type checked. A successful call to
     * `is<T>()` is required before casting to ensure type safety. It is 
     * typically better practice and generally safer to use `copy_data_to<T>()` 
     * or `move_data_to<T>()`. 
     *
     * @return a reference of type T to the internal void pointer payload
     */
    template <typename T>
    T& cast_data() {
        return *((base<T>*)(m_data.get()));
    }

    /**
     * @brief copy the data payload to argument t
     *
     * @param t reference to templated variable t to deep copy the data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool copy_data_to(T& t) {
        if(is<T>()) {
            t = cast_data<T>();
            return true;
        } else {
            return false;
        }
    }

    /**
     * @brief rvalue swap the data payload to argument t
     *
     * @param t reference to templated variable t to rvalue swap the data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool move_data_to(T& t) {
        if(is<T>()) {
            std::swap(t, cast_data<T>());
            return true;
        } else {
            return false;
        }
    }

private:
    typedef void(*deleter_t)(void*);
    typedef std::unique_ptr<void,deleter_t> data_pointer_t;

    message() = delete;
    message(const message& rhs) = delete;
    message(message&& rhs) = delete;

    message(const std::size_t c, const std::size_t t, data_pointer_t p) :
        m_id(c),
        m_data(std::move(p)),
        type_aware(t)
    { }

    template <typename T>
    static void* allocate(T&& t) {
        return (void*)(new base<T>(std::forward<T>(t)));
    }

    template <typename T>
    static void deleter(void* p) {
        delete (base<T>*)p;
    }

    static inline void no_delete(void* p) { }

    const std::size_t m_id;
    data_pointer_t m_data;
};

/**
 * @brief Object representing the result of a communication operation 
 *
 * Can be used directly in if/else statements due to this class having an 
 * implementation of `operator bool`.
 */
struct result {
    /// Enumeration representing the status of the associated operation
    enum eStatus {
        success, ///< operation succeeded 
        full, ///< operation failed due to full buffer
        closed ///< operation failed due to object being closed
    };

    /**
     * @return true if the result of the operation succeeded, else false
     */
    inline operator bool() const {
        return status == eStatus::success;
    }

    /// Status of the operation
    eStatus status;
};

struct communication_interface {
    /**
     * @brief send a message with given parameters
     *
     * This is a blocking operation, which only blocks if the queue is full (
     * queue has reached its user specified limit), the argument message will 
     * still be queued but the calling thread will be blocked until the queue 
     * becomes non-full.
     *
     * @param as arguments passed to `message::make()`
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed
     */
    template <typename... As>
    result send(As&&... as) {
        return internal_send(message::make(std::forward<As>(as)...), op::block_send);
    }

    /**
     * @brief send a message with given parameters
     *
     * This is a non-blocking operation. If queue is full, operation will fail 
     * early.
     *
     * @param as arguments passed to `message::make()`
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed, result.status==result::eStatus::full if full
     */
    template <typename... As>
    result try_send(As&&... as) {
        return internal_send(message::make(std::forward<As>(as)...), op::try_send);
    }

    /**
     * @brief send a message with given parameters
     *
     * This is a non-blocking operation. The sent message will be queued
     * in spite of any user specified limit. This operation will fail if the 
     * channel is closed. 
     *
     * Due to ignoring user defined queue size limits, it is best to have a good 
     * reason for using this function as it can cause unexpected memory bloat.
     *
     * @param id an unsigned integer representing which type of message 
     * @param as additional argument(s) to `st::message::make()`
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed, result.status==result::eStatus::full if full
     */
    template <typename... As>
    result force_send(As&&... as) {
        return internal_send(message::make(std::forward<As>(as)...), op::force_send);
    }

protected:
    enum op {
        blocking_send,
        try_send,
        force_send
    };

    virtual result internal_send(sptr<message> msg, op o) = 0;
};

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between managed 
 * system threads. Provided here as a convenience for communicating from managed 
 * system threads to other user threads. All methods in this object are mutex 
 * locked and threadsafe.
 */
struct channel : protected communication_interface { 
    /**
     * @brief expression representing "no maximum size" for the channel's message queue
     */
    static constexpr std::size_t queue_no_limit = 0;

/**
 * @def SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE
 *
 * Default process wide maximum channel queue size. Defaults to 
 * st::channel::queue_no_limit which causes channels to have no default maximum 
 * size.
 *
 * Users can define this value themselves to set a custom process wide maximum
 * channel queue size.
 */
#ifndef SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE
#define SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE channel::queue_no_limit 
#endif

    /**
     * @brief Construct a channel as a shared_ptr 
     * @param max_queue_size maximum concurrent count of messages this channel will store before `send()` calls block
     * @return a channel shared_ptr
     */
    static inline sptr<channel> make(std::size_t max_queue_size=
            SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE) {
        auto ch = sptr<channel>(new channel(max_queue_size));
        return ch;
    }

    /**
     * @return maximum concurrent count of messages this channel will store before `send()` calls block.
     */
    inline std::size_t max_queue_size() const {
        return m_max_q_size;
    }

    /** 
     * NOTE: The returned value may be larger than than `max_queue_size()`, due 
     * to the fact that the channel *can* store more messages than its user 
     * set maximum in several circumstances.
     *
     * @return count of messages in the queue
     */
    inline std::size_t queued() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_msg_q.size();
    }

    /**
     * @return true if queue is empty, else false 
     */
    inline bool empty() const { 
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_msg_q.size() == 0;
    }

    /**
     * @return true if queue is full (causing `send()` calls to block), else false 
     */
    inline bool full() const { 
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_max_q_size && m_msg_q.size() >= m_max_q_size;
    }

    /**
     * @return count of threads blocked on `recv()`
     */
    inline std::size_t blocked_receivers() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_recv_q.size();
    }

    /**
     * @return true if channel is closed, else false 
     */
    inline bool closed() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_closed;
    }

    /**
     * @brief Close the channel 
     *
     * Ends all current and future operations on the channel 
     *
     * @param process_remaining_messages if true allow current and future recv() to succeed until queue empty
     */
    inline void close(bool process_remaining_messages=true) {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_closed = true;

        if(!process_remaining_messages) {
            m_msg_q.clear();
            
            for(auto& blk : m_send_q) {
                blk->flag = true;
                blk->cv.notify_one();
            }
        }

        for(auto& blk : m_recv_q) {
            blk->flag = true;
            blk->cv.notify_one();
        }
    }

    /**
     * @brief optionally enqueue the argument message and receive a message over the channel
     *
     * If argument message is non-null and `exchange` equals `true`. value of 
     * the argument message will be immediately pushed to the back of the queue. 
     * This allows messages to be re-queued in the channel even when the channel 
     * is full. Using the `exchange` functionality described here is often a 
     * good alternative to using `force_send()` from within receiver code when a 
     * message needs to be requeued for further processing.
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue.
     *
     * @param msg interprocess message object reference (optionally containing a message for exchange) to contain the received message 
     * @param exchange if true, and msg is non-null, msg will be enqueued before a new message is retrieved
     * @return true on success, false if channel is closed
     */
    inline result recv(sptr<message>& msg, bool exchange = false) {
        result r;
        sptr<blocker> send_blk;

        {
            std::unique_lock<std::mutex> lk(m_mtx);
            if(msg) { 
                if(exchange) {
                    // if msg is provided, immediately enqueue
                    m_msg_q.push_back({ std::move(msg), sptr<blocker>() });
                }
                msg.reset()
            }

            // block until message is available or channel close
            while(m_msg_q.empty() && !m_closed) { 
                auto recv_blk = std::make_shared<blocker>();
                m_recv_q.push_back(recv_blk);
                recv_blk->wait(lk);
            }

            if(m_msg_q.empty()) { // no more messages to process, channel closed
                r = result{ result::eStatus::closed };
            } else {
                msg = std::move(m_msg_q.front());
                m_msg_q.pop_front();

                if(m_msg_q.size() < m_max_q_size && m_send_q.size()) {
                    send_blk = std::move(m_send_q.front());
                    m_send_q.pop_front();
                    send_blk->flag = true;
                }

                r = result{ result::eStatus::success };
            } 
        }

        if(send_blk) { // notify outside of lock scope to limit mutex blocking
            send_blk->cv.notify_one();
        }

        return r;
    }

protected:
    inline result internal_send(sptr<message> msg, communication_interface::op so) {
        sptr<blocker> blk;

        {
            std::unique_lock<std::mutex> lk(m_mtx);

            if(m_closed) {
                return result{ result::eStatus::closed };
            } else if(internal_full()) {
                switch(so) {
                    case communication_interface::op::blocking_send:
                    {
                        m_msg_q.push_back(std::move(msg));
                        blk = std::make_shared<blocker>();
                        m_send_q.push_back(blk);
                        blk->wait(lk);
                        blk.reset();
                        break;
                    }
                    case communication_interface::op::try_send:
                        return result{ result::eStatus::full };
                        break;
                    case communication_interface::op::force_send:
                    default:
                        m_msg_q.push_back(std::move(msg));
                        break;
                }
            } else {
                m_msg_q.push_back(std::move(msg));
            }

            if(m_recv_q.size()) {
                blk = std::move(m_recv_q.front());
                m_recv_q.pop_front();
                blk->flag = true;
            }
        }

        if(blk) { // notify outside of lock scope to limit mutex blocking
            blk->cv.notify_one();
        }

        return result{ result::eStatus::success };
    }

private:
    struct blocker {
        inline void wait(std::unique_lock<std::mutex>& lk) {
            do {
                cv.wait(lk);
            } while(!flag);
        }

        bool flag = false;
        std::condition_variable cv;
    };

    channel(std::size_t max_queue_size) : 
        m_closed(false), 
        m_max_q_size(max_queue_size),
    { }

    channel() = delete;
    channel(const channel& rhs) = delete;
    channel(channel&& rhs) = delete;

    inline bool internal_full() {
        return !m_closed && m_max_q_size && m_msg_q.size() >= m_max_q_size;
    }

    bool m_closed;
    const std::size_t m_max_q_size;
    mutable std::mutex m_mtx;
    std::deque<sptr<message>> m_msg_q;
    std::deque<sptr<blocker>> m_send_q;
    std::deque<sptr<blocker>> m_recv_q;
};

/**
 * @brief interface representing an object with a managed lifecycle in this library 
 */
struct lifecycle_interface {
    virtual ~lifecycle_interface() { 
        shutdown();
    }

    virtual bool running() = 0;
    virtual void shutdown(bool process_remaining_messages) = 0;
    virtual void shutdown() { shutdown(false); }
};

/**
 * @brief Managed system worker thread
 */
struct worker : public type_aware, 
                public lifecycle_interface,
                protected concurrent_interface {
    /** 
     * @brief allocate a worker thread to listen to a channel and pass messages to template FUNCTOR
     * Argument handler FUNCTOR to be called whenever a message is received by 
     * the worker's channel. 
     *
     * Type FUNCTOR should be a functor class. A functor is a class with call 
     * operator overload which accepts an argument sptr<message>, 
     * ex:
     * ```
     * struct MyFunctor {
     *     void operator()(sptr<st::message> m);
     * };
     * ```
     *
     * Message arguments can also be accepted by reference:
     * ```
     * struct MyFunctor {
     *     void operator()(sptr<st::message>& m);
     * };
     * ```
     *
     * Note: workers automatically throw out any null messages received from 
     * the channel.
     *
     * Functors can alternatively return a sptr<st::message>. If the 
     * functor follows this pattern any returned non-null message will be 
     * requeued in the channel via `st::channel::recv(msg, true)`:
     * ```
     * struct MyFunctor {
     *     sptr<st::message> operator()(sptr<st::message> m);
     * };
     * ```
     *
     * Care must be taken with the above pattern, as it can create an implicit
     * infinite processing loop if the operator() never returns a non-null 
     * message.
     *
     * Using a functor is useful because it allows member data to persist
     * between calls to `operator()(sptr<st::message> m)` and 
     * for all member data to be easily accessible. 
     *
     * Another distinct advantage is functors are able to make intelligent use 
     * of C++ RAII semantics, as the functor will come into existence on a 
     * running worker thread and be destroyed when that thread ends.
     *
     * @param ch channel to receive messages over
     * @param as constructor arguments for type FUNCTOR
     * @return allocated running worker thread shared_ptr
     */
    template <typename FUNCTOR, 
              std::size_t QUEUE_MAX_SIZE=SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE, 
              typename... As>
    static sptr<worker> make(As&&... as) {
        sptr<worker> wp(new worker(type_aware::hint<FUNCTOR> h, std::forward<As>(as)...));
        wp->start(wp, QUEUE_MAX_SIZE); // launch thread
        return wp;
    }

    /**
     * @return true if worker thread is running, else false
     */
    inline bool running() {
        std::lock_guard<std::mutex> lk(m_mtx);
        bool r = m_thd.joinable() && !m_ch->closed();
        return r;
    }

    /** 
     * @brief Shutdown the worker thread
     *
     * @param process_remaining_messages if true allow recv() to succeed until queue empty
     */
    inline void shutdown(bool process_remaining_messages) {
        std::lock_guard<std::mutex> lk(m_mtx);
        if(m_thd.joinable()) {
            if(m_ch && !m_ch->closed()) {
                m_ch->close(process_remaining_messages);
            }
            m_thd.join();
        }
    }

    /**
     * @brief Provide access to calling worker object pointer
     *
     * If not called by a running worker, returned shared_ptr is null. Useful 
     * when the worker pointer is needed from within the FUNCTOR handler.
     *
     * @return a shared_ptr to calling thread's worker
     */
    static inline sptr<worker> this_worker() {
        return tl_worker().lock();
    }

    /**
     * @brief class describing the workload of a worker
     *
     * Useful for comparing relative worker thread workloads when scheduling.
     */
    struct weight {
        /**
         * @brief represents count of queued messages on a worker
         */
        std::size_t queued;

        /**
         * @brief represents if a worker is currently processing a message
         */
        bool executing;

        /**
         * @return true if the weight is 0, else false
         */
        inline bool empty() const {
            return !(queued && executing);
        }

        /**
         * @return true if this weight is lighter than the other, else false
         */
        inline bool operator<(const weight& rhs) const {
            if(queued < rhs.queued) {
                return true;
            } else if(queued > rhs.queued) {
                return false;
            } else if(!executing && rhs.executing) {
                return true;
            } else {
                return false;
            }
        }
    };

    /**
     * @return weight representing workload of worker
     */
    inline weight get_weight() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return weight{m_ch->queued(), 
                      m_ch->blocked_receivers() ? false 
                                                : m_thd.joinable() ? true 
                                                                   : false};
    }

protected:
    inline result internal_send(sptr<message> msg, concurrent::op o) {
        if(o == concurrent::op::blocking_send) {
            m_ch->send(std::move(msg));
        } else if(o == concurrent::op::try_send) {
            m_ch->try_send(std::move(msg));
        } else {
            m_ch->force_send(std::move(msg));
        }
    }

private:
    typedef std::function<sptr<message>(sptr<message>&)> message_handler;

    // handler for functors which return sptr<st::message>
    template <std::true_type, typename FUNCTOR>
    static sptr<message> process(
            FUNCTOR& f, 
            sptr<message>& msg) {
        return f(msg); 
    }

    // handler for all other functors
    template <std::false_type, typename FUNCTOR>
    static sptr<message> process(
            FUNCTOR& f, 
            sptr<message>& msg) {
        f(msg);
        return sptr<message>();
    }

    template <typename FUNCTOR, typename... As>
    static message_handler generate_handler(As&&... as) {
        auto fp = sptr<FUNCTOR>(new FUNCTOR(std::forward<As>(as)...));
        return [fp](sptr<message>& msg) -> sptr<message> { 
            using ismessage = std::is_same<
                sptr<message>,
                decltype(f(sptr<message>()))>::type;
            return worker::process<ismessage>(*fp, msg);
        };
    }

    template <typename FUNCTOR, typename... As>
    static std::function<message_handler()> handler_generator(As&&... as) {
        return [=]() mutable -> std::function<message_handler()> { 
            worker::generate_handler(std::forward<As>(as)...); 
        };
    }

    template <typename FUNCTOR, typename... As>
    worker(type_aware::hint<FUNCTOR> h, As&&... as) : 
        type_aware(type_code<FUNCTOR>()) {
        // generate handler is called late and allocates a shared_ptr to allow 
        // for a single construction and destruction of type FUNCTOR in the 
        // worker thread environment. 
        m_handler_generator = 
            handler_generator<FUNCTOR>(std::forward<As>(as)...);
    }

    worker() = delete;
    worker(const worker& rhs) = delete;
    worker(worker&& rhs) = delete;

    // thread local worker by-reference getter 
    static inline worker*& tl_worker() {
        thread_local worker* w(nullptr);
        return w;
    }
    
    inline void start(std::weak_ptr<worker> self, const std::size_t queue_max_size) {
        m_self = self;
        m_ch = channel::make(queue_max_size);
        bool thread_started_flag = false;
        std::condition_variable thread_start_cv;

        m_thd = std::thread([&]{
            sptr<message> msg;
            tl_worker() = m_self; // set the thread local worker pointer

            auto hdl = m_handler_generator();

            {
                std::lock_guard<std::mutex> lk(m_mtx);
                thread_started_flag = true;
            }

            thread_start_cv.notify_one();

            // message handling loop  
            while(m_ch->recv(msg, true)) { // if msg is non-null, re-queue it
                if(msg) { // as a sanity, throw out null received messages
                    msg = hdl(msg);
                }
            }

            tl_worker() = nullptr; // reset the thread local worker pointer
        });

        std::unique_lock<std::mutex> lk(m_mtx);
        while(!thread_started_flag) {
            thread_start_cv.wait(lk);
        }
    }

    const std::size_t m_type_code;
    std::weak_ptr<worker> m_self;
    mutable std::mutex m_mtx;
    std::function<handler()> m_handler_generator;
    sptr<st::channel> m_ch;
    std::thread m_thd;
};
}

#endif
