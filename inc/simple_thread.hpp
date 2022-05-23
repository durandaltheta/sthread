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
#include <vector>
#include <list>

namespace st {

/**
 * @brief Interthread type erased message container
 */
struct message {
private:
    typedef void(*deleter_t)(void*);
    typedef std::unique_ptr<void,deleter_t> data_pointer_t;

public:
    /**
     * @brief Typedef representing the unqualified type of T
     */
    template <typename T>
    using base = typename std::remove_reference<typename std::remove_cv<T>::type>::type;

    /**
     * @return an unsigned integer representing a data type.
     *
     * The data type value is acquired by removing const and volatile 
     * qualifiers and then by acquiring the type_info::hash_code().
     */
    template <typename T>
    static constexpr std::size_t code() {
        return typeid(base<T>).hash_code();
    }
    
    template <typename T>
    static std::shared_ptr<message> make(std::shared_ptr<message> msg) {
        return msg;
    }

    /**
     * Construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @param t arbitrary typed data to be stored as the message data 
     * @return an allocated message
     */
    template <typename T>
    static std::shared_ptr<message> make(std::size_t id, T&& t) {
        return std::shared_ptr<message>(new message(
            id,
            code<T>(),
            data_pointer_t(
                allocate<T>(std::forward<T>(t)),
                message::deleter<T>)));
    }

    /**
     * Construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @return an allocated message
     */
    static inline std::shared_ptr<message> make(std::size_t id) {
        return std::shared_ptr<message>(new message(
                    id, 
                    0, 
                    data_pointer_t(nullptr, message::no_delete)));
    }
   
    /** 
     * Convenience overload for templating
     *
     * @return an allocated message
     */
    static inline std::shared_ptr<message> make(std::shared_ptr<message> msg) {
        return msg;
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
     * @return an unsigned integer representing message data's data type
     *
     * The data data type is acquired by calling code<T>().
     */
    inline std::size_t type() const {
        return m_data_type;
    }
   
    /**
     * Determine whether the stored data type matches the templated type.
     *
     * @return true if the unqualified type of T matches the data type, else false
     */
    template <typename T>
    bool is() const {
        return m_data && m_data_type == code<T>();
    }

    /**
     * Copy the data to argument t
     *
     * @param t reference to templated variable t to deep copy the data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool copy_data_to(T& t) {
        if(is<T>()) {
            t = *((base<T>*)(m_data.get()));
            return true;
        } else {
            return false;
        }
    }

    /**
     * Rvalue swap the data to argument t
     *
     * @param t reference to templated variable t to rvalue swap the data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool move_data_to(T& t) {
        if(is<T>()) {
            std::swap(t, *((base<T>*)(m_data.get())));
            return true;
        } else {
            return false;
        }
    }

private:
    message() = delete;
    message(const message& rhs) = delete;
    message(message&& rhs) = delete;

    message(std::size_t c, std::size_t t, data_pointer_t p) :
        m_id(c),
        m_data_type(t),
        m_data(std::move(p))
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
    const std::size_t m_data_type;
    data_pointer_t m_data;
};

/**
 * @brief Object representing the result of an operation 
 *
 * Can be used directly in if/else statements dues to `operator bool`
 */
struct result {
    /**
     * Enumeration representing the status of the associated operation
     */
    enum eStatus {
        success,
        full,
        closed,
        notFound
    };

    /**
     * @return true if the result of the operation succeeded, else false
     */
    inline operator bool() const {
        return status == eStatus::success;
    }

    /**
     * Status of the operation
     */
    eStatus status;
};

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between managed 
 * system threads. Provided here as a convenience for communicating from managed 
 * system threads to other user threads.
 */
struct channel { 
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
     * @param max_queue_size maximum concurrent count of messages this channel will store before send() calls fail. Default value is no limit
     * @return a channel shared_ptr
     */
    static inline std::shared_ptr<channel> make(std::size_t max_queue_size=
            SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE) {
        auto ch = std::shared_ptr<channel>(new channel(max_queue_size));
        return ch;
    }

    /**
     * @return max_size maximum concurrent count of messages this channel will store before send() calls fail.
     */
    inline std::size_t max_queue_size() const {
        return m_max_q_size;
    }

    /**
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
     * @return true if queue is full, else false 
     */
    inline bool full() const { 
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_max_q_size && m_msg_q.size() >= m_max_q_size;
    }

    /**
     * @return count of threads blocked on recv()
     */
    inline std::size_t blocked_receivers() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_receivers_count;
    }

    /**
     * @return count of threads blocked on send()
     */
    inline std::size_t blocked_senders() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_senders_count;
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
        bool notify = false;

        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_closed = true;

            if(!process_remaining_messages) {
                m_msg_q.clear();
            }

            if(m_receivers_count) {
                notify = true;
            }
        }

        if(notify) {
            m_receiver_cv.notify_all();
            m_sender_cv.notify_all();
        }
    }

    /**
     * @brief Attempt to send a message over the channel
     *
     * This is a blocking operation. 
     *
     * @param m interprocess message object
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed
     */
    inline result send(std::shared_ptr<message> m) {
        bool notify = false;

        {
            std::unique_lock<std::mutex> lk(m_mtx);
            if(internal_full()) {
                m_senders_count++;

                do {
                    m_sender_cv.wait(lk);
                } while(internal_full());

                m_senders_count--;
            }

            if(m_closed) {
                return result{ result::eStatus::closed };
            }

            m_msg_q.push_back(std::move(m));

            if(m_receivers_count) {
                notify = true;
            }
        }

        if(notify) {
            m_receiver_cv.notify_one();
        }

        return result{ result::eStatus::success };
    }

    /**
     * Send a message over the channel with given parameters 
     *
     * This is a blocking operation.
     *
     * @param id an unsigned integer representing which type of message 
     * @param t template variable to package as the message payload
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed
     */
    template <typename T>
    result send(std::size_t id, T&& t) {
        return send(message::make(id, std::forward<T>(t)));
    }
    
    /**
     * Send a message over the channel with given parameter
     *
     * This is a non-blocking operation.
     *
     * @param id an unsigned integer representing which type of message 
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed
     */
    inline result send(std::size_t id) {
        return send(message::make(id));
    }

    /**
     * @brief Attempt to send a message over the channel
     *
     * This is a non-blocking operation. If queue is full, operation will fail early.
     *
     * @param m interprocess message object
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed, result.status==result::eStatus::full if full
     */
    inline result try_send(std::shared_ptr<message> m) {
        bool notify = false;

        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if(m_closed) {
                return result{ result::eStatus::closed };
            } else if(internal_full()) {
                return result{ result::eStatus::full };
            }

            m_msg_q.push_back(std::move(m));

            if(m_receivers_count) {
                notify = true;
            }
        }

        if(notify) {
            m_receiver_cv.notify_one();
        }

        return result{ result::eStatus::success };
    }

    /**
     * Send a message over the channel with given parameters 
     *
     * This is a non-blocking operation. If queue is full, operation will fail early.
     *
     * @param id an unsigned integer representing which type of message 
     * @param t template variable to package as the message payload
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed, result.status==result::eStatus::full if full
     */
    template <typename T>
    result try_send(std::size_t id, T&& t) {
        return try_send(message::make(id, std::forward<T>(t)));
    }
    
    /**
     * Send a message over the channel with given parameter
     *
     * This is a non-blocking operation. If queue is full, operation will fail early.
     *
     * @param id an unsigned integer representing which type of message 
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed, result.status==result::eStatus::full if full
     */
    inline result try_send(std::size_t id) {
        return try_send(message::make(id));
    }

    /**
     * @brief Receive a message over the channel 
     *
     * This is a blocking operation.
     *
     * @param m interprocess message object reference to contain the received message 
     * @return true on success, false if channel is closed
     */
    result recv(std::shared_ptr<message>& m) {
        result r;
        bool notify = false;

        auto no_messages = [&]{ return m_msg_q.empty() && !m_closed; };

        {
            std::unique_lock<std::mutex> lk(m_mtx);
            if(no_messages()) {
                m_receivers_count++;

                do {
                    m_receiver_cv.wait(lk);
                } while(no_messages());
                
                m_receivers_count--;
            }

            if(m_msg_q.empty()) {
                r = result{ result::eStatus::closed };
            } else {
                m = m_msg_q.front();
                m_msg_q.pop_front();

                if(m_senders_count) {
                    notify = true;
                }

                r = result{ result::eStatus::success };
            } 
        }

        if(notify) {
            m_sender_cv.notify_one();
        }

        return r;
    }

private:
    channel(std::size_t max_queue_size) : 
        m_closed(false), 
        m_max_q_size(max_queue_size), 
        m_receivers_count(0) 
    { }

    channel(const channel& rhs) = delete;
    channel(channel&& rhs) = delete;

    inline bool internal_full() {
        return !m_closed && m_max_q_size && m_msg_q.size() >= m_max_q_size;
    }

    bool m_closed;
    const std::size_t m_max_q_size;
    std::size_t m_receivers_count; // heuristic to limit condition_variable signals
    std::size_t m_senders_count; // heuristic to limit condition_variable signals
    mutable std::mutex m_mtx;
    std::condition_variable m_receiver_cv;
    std::condition_variable m_sender_cv;
    std::deque<std::shared_ptr<message>> m_msg_q;
};

/**
 * @brief Managed system worker thread
 */
struct worker {
    /**
     * Launch a worker with argument handler FUNCTOR to be scheduled whenever a 
     * message is received by the worker's channel. 
     *
     * Type FUNCTOR should be a functor class. A functor is a class with call 
     * operator overload which accepts an std::shared_ptr<message>, ex:
     * ```
     * struct MyFunctor {
     *     void operator()(std::shared_ptr<st::message> m);
     * };
     * ```
     *
     * Using a functor is useful because it allows member data to persist
     * between calls to `void operator()(std::shared_ptr<st::message> m)` and 
     * for all member data to be easily accessible. 
     *
     * Another distinct advantage is functors are able to make intelligent use 
     * of C++ RAII semantics, as the functor will come into existence on a 
     * running worker thread and be destroyed when that thread ends.
     *
     * @param as constructor arguments for type FUNCTOR
     * @return allocated running worker thread shared_ptr
     */
    template <typename FUNCTOR, 
              int QUEUE_MAX_SIZE=SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE, 
              typename... As>
    static std::shared_ptr<worker> make(As&&... as) {
        std::shared_ptr<worker> wp(new worker(
                    type_hint<FUNCTOR, QUEUE_MAX_SIZE>(), 
                    std::forward<As>(as)...));
        wp->set_self(wp);
        wp->restart(); // launch thread
        return wp;
    }

    worker(const worker& rhs) = delete;
    worker(worker&&) = delete;

    ~worker() { 
        inner_shutdown();
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
    inline void shutdown(bool process_remaining_messages=true) {
        std::lock_guard<std::mutex> lk(m_mtx);
        inner_shutdown(process_remaining_messages);
    }

    /**
     * @brief Restart the worker thread 
     *
     * Shutdown threads and reset state when necessary. 
     *
     * @param process_remaining_messages if true allow recv() to succeed until queue empty
     */
    inline void restart(bool process_remaining_messages=true) {
        std::unique_lock<std::mutex> lk(m_mtx);
        inner_shutdown(process_remaining_messages);
        m_ch = channel::make(m_ch_queue_max_size);
        m_thread_started_flag = false;
        m_thd = std::thread([&]{
            std::shared_ptr<message> m;
            tl_worker() = this; // set the thread local worker pointer

            auto hdl = m_generate_handler();

            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_thread_started_flag = true;
            }
            m_thread_start_cv.notify_one();

            while(m_ch->recv(m)) {
                hdl(m);
                m.reset();
            }

            tl_worker() = nullptr; // reset the thread local worker pointer
        });

        while(!m_thread_started_flag) {
            m_thread_start_cv.wait(lk);
        }
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

    /**
     * @brief Send a message over the channel
     * @param m interprocess message object
     * @return true on success, false if channel is closed
     */
    inline result send(std::shared_ptr<message> m) {
        return m_ch->send(std::move(m));
    }

    /**
     * Send a message over the channel with given parameters 
     *
     * This is a blocking operation.
     *
     * @param id an unsigned integer representing which type of message 
     * @param t template variable to package as the message payload
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed
     */
    template <typename T>
    result send(std::size_t id, T&& t) {
        return m_ch->send(id, std::forward<T>(t));
    }
    
    /**
     * Send a message over the channel with given parameter
     *
     * This is a non-blocking operation.
     *
     * @param id an unsigned integer representing which type of message 
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed
     */
    inline result send(std::size_t id) {
        return m_ch->send(id);
    }

    /**
     * @brief Send a message over the channel
     * @param m interprocess message object
     * @return true on success, false if channel is closed
     */
    inline result try_send(std::shared_ptr<message> m) {
        return m_ch->try_send(std::move(m));
    }

    /**
     * Send a message over the channel with given parameters 
     *
     * This is a non-blocking operation. If queue is full, operation will fail early.
     *
     * @param id an unsigned integer representing which type of message 
     * @param t template variable to package as the message payload
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed, result.status==result::eStatus::full if full
     */
    template <typename T>
    result try_send(std::size_t id, T&& t) {
        return m_ch->try_send(id, std::forward<T>(t));
    }
    
    /**
     * Send a message over the channel with given parameter
     *
     * This is a non-blocking operation. If queue is full, operation will fail early.
     *
     * @param id an unsigned integer representing which type of message 
     * @return result.status==result::eStatus::success on success, result.status==result::eStatus::closed if closed, result.status==result::eStatus::full if full
     */
    inline result try_send(std::size_t id) {
        return m_ch->try_send(id);
    }
    /**
     * @brief Provide access to calling worker object pointer
     *
     * If not called by a running worker, returned shared_ptr is null. Useful 
     * when the worker pointer is needed from within the FUNCTOR handler.
     *
     * Care must be taken when keeping a copy of this shared_ptr in existence, 
     * as mishandling of this pointer can cause memory leak.
     *
     * @return a shared_ptr to calling thread's worker
     */
    static inline std::shared_ptr<worker> this_worker() {
        auto wp = tl_worker();
        if(wp) {
            return wp->m_self.lock();
        } else {
            return std::shared_ptr<worker>();
        }
    }

private:
    typedef std::function<void(std::shared_ptr<message>)> handler;

    template <typename FUNCTOR, int QUEUE_MAX_SIZE>
    struct type_hint { };

    template <typename FUNCTOR, int QUEUE_MAX_SIZE, typename... As>
    worker(type_hint<FUNCTOR, QUEUE_MAX_SIZE> t, As&&... as) : 
        m_ch_queue_max_size(QUEUE_MAX_SIZE),
        m_thread_started_flag(false) {
        // generate handler is called late and allocates a shared_ptr to allow 
        // for a single construction and destruction of type FUNCTOR in the 
        // worker thread environment. 
        m_generate_handler = [=]() mutable -> handler{ 
            auto fp = std::shared_ptr<FUNCTOR>(new FUNCTOR(std::forward<As>(as)...));
            return [=](std::shared_ptr<message> m) { (*fp)(std::move(m)); };
        };
    }

    inline void set_self(std::weak_ptr<worker> self) {
        m_self = self;
    }

    // thread local worker by-reference getter 
    static inline worker*& tl_worker() {
        thread_local worker* w(nullptr);
        return w;
    }

    // close the worker channel and join the thread if necessary
    inline void inner_shutdown(bool proc_rem_msgs=true) {
        if(m_thd.joinable()) {
            if(m_ch && !m_ch->closed()) {
                m_ch->close(proc_rem_msgs);
            }
            m_thd.join();
        }

        m_thread_started_flag = false;
    }

    std::size_t m_ch_queue_max_size;
    bool m_thread_started_flag;
    std::weak_ptr<worker> m_self;
    mutable std::mutex m_mtx;
    std::condition_variable m_thread_start_cv;
    std::function<handler()> m_generate_handler;
    std::shared_ptr<channel> m_ch;
    std::thread m_thd;
};

struct timer {
    /**
     * @brief Construct and start a timer 
     *
     * On timeout handler will be called. If argument handler is convertable to 
     * std::function<void()> this overload will be selected.
     *
     * @param sec seconds before timeout will occur
     * @param milli additional milliseconds before the timeout will occur 
     * @param handler function will be called when timeout occurs
     * @return allocated and running timer
     */
    inline std::shared_ptr<timer> make(std::size_t sec, 
                                       std::size_t milli, 
                                       std::function<void()> handler) {
        std::shared_ptr<timer> sp(new timer(std::move(handler));
        sp->m_self = sp;
        TimerService::instance().start(
                std:;chrono::steady_clock::now() +
                std::chrono::seconds(sec) +
                std::chrono::milliseconds(milli),
                sp);
        return sp;
    }

    /**
     * @brief Construct and start a timer 
     *
     * On timeout handler will be called.
     *
     * @param sec seconds before timeout will occur
     * @param milli additional milliseconds before the timeout will occur 
     * @param f function that will be called with remaining arguments when timeout occurs
     * @param t first argument to be applied to f 
     * @param as remaining arguments to be applied to function f
     * @return allocated and running timer
     */
    template <typename F, typename T, typename... As>
    inline std::shared_ptr<timer> make(std::size_t sec, 
                                       std::size_t milli, 
                                       F&& f, 
                                       T&& t, 
                                       As&&... as) {
        return make(sec, 
                    milli, 
                    [=]() mutable { 
                        f(std::forward<T>(t), std::forward<As>(as)...); 
                    });
    }

    /**
     * @brief Stop a running timer 
     *
     * This operation is more expensive than starting a timer
     *
     * @param id unique id of running timer
     * @return successful result on success, else result.status==result::eStatus::notFound
     */
    inline result stop() {
        return TimerService::instance().stop_timer(m_self.lock());
    }

private:
    timer(std::function<void()> handler) : m_handler(std::move(handler)) {}

    timer(const timer& rhs) = delete;
    timer(timer&& rhs) = delete;

    // timer feature
    struct TimerService {
        typedef std::chrono::steady_clock::time_point time_point_t;
        typedef std::vector<std::shared_ptr<timer>> timer_vec_t;
        typedef std::pair<time_point_t, timer_vec_t> timer_pair_t;
        typedef std::map<time_point_t, timer_vec_t> timer_map_t;

        static inline TimerService& instance() {
            static TimerService ts;
            return ts;
        }

        TimerService() : 
            m_running(true), 
            out_handler_thd([&]{
                std::unique_lock<std::mutex> lk(m_mtx);

                while(m_running) {
                    std::vector<timer_vec_t> ready_timer_vecs;

                    for(timer_map_t::iterator it = rs.begin(); it!= m_timers.end(); it++) {
                        if(it->first < std::chrono::steady_clock::now()) {
                            // store timeout vectors in local container
                            ready_timer_vecs.push_back(std::move(it->second));

                            // erase timeouts from member container
                            it = rs.erase(it);
                        } else {
                            // no more timers are ready
                            break;
                        }
                    }

                    // do not call timer handlers with lock to prevent deadlock
                    lk.unlock(); 
                        
                    // call timer handlers
                    for(auto& read_timer_vec : ready_timer_vecs) { 
                        for(auto& timer : ready_timeout_vec) {
                            timer.m_handler();
                        }
                    }

                    // reacquire lock
                    lk.lock(); 

                    // wait until nearest timeout or indefinitely until a new 
                    // timer is started
                    auto it = rs.begin();
                    if(it != rs.end()) {
                        m_cv.wait_until(lk, it->first);
                    } else {
                        m_cv.wait(lk);
                    }
                }
            })
        { }

        ~TimerService() {
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_running = false;
                m_cv.notify_one();
            }

            out_handler_thd.join();
        }

        template <typename... As>
        void start_timer(std::chrono::steady_clock::time_point timeout, 
                         std::shared_ptr<timer> tim) {
            std::lock_guard<std::mutex> lk(m_mtx);

            {
                std::unique_lock<std::mutex> lk(m_mtx);
                auto it = rs.find(timeout);

                // ensure container for timers at timeout time_point exists
                if(it == rs.end()) {
                    it = rs.insert(timeout, timer_vec_t>());
                }

                it->second.push_back(tim);
            }

            // wakeup timeout handler thread to process updated timers
            m_cv.notify_one(); 
        }

        inline result stop_timer(std::shared_ptr<timer> tim) {
            std::lock_guard<std::mutex> lk(m_mtx);

            bool found = false;

            // search through all timeouts
            for(auto timer_data_vec_it = rs.begin();
                timer_data_vec_it < rs.end();
                timer_data_vec_it++) {

                // search through all timer data associated with a timeout
                for(auto it = timer_data_vec_it->begin(); 
                    it != timer_data_vec_it->end(); 
                    it++) {
                    if(it->tim == tim) {
                        timer_data_vec_it->erase(it);
                        found = true;

                        // if timer is found end early
                        break;
                    }
                }

                if(timer_data_vec_it->empty()) {
                    timer_data_vec_it = rs.erase(timer_data_vec_it);
                }

                if(found) {
                    break;
                }
            }

            if(found) {  
                return result{result::eStatus::success};
            } else {
                return result{result::eStatus::notFound};
            }
        }

        bool m_running;
        std::mutex m_mtx;
        std::condition_variable m_cv;
        std::thread out_handler_thd;
        timer_map_t rs;
    };

    std::function<void()> m_handler;
    std::weak_ptr<timer> m_self;
};

}

#endif
