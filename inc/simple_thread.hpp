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
#include <set>
#include <map>
#include <atomic>

namespace st {

/**
 * @brief Object representing the result of an operation 
 *
 * Can be used directly in if/else statements due to `operator bool`
 */
struct result {
    /**
     * Enumeration representing the status of the associated operation
     */
    enum estatus {
        success, // operation succeeded
        argument_null, // a given shared_ptr argument is a nullptr
        full, // channel queue is full
        closed, // channel is closed and cannot be operated on anymore
        already_running, // timer is already running
        not_found // timer was not found
    };

    /**
     * @return true if the result of the operation succeeded, else false
     */
    inline operator bool() const {
        return status == estatus::success;
    }

    /**
     * Status of the operation
     */
    estatus status;
};

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

    /**
     * @brief Construct a message
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
     * @brief Convenience overload for message construction used in templates
     *
     * @return argument message
     */
    static inline std::shared_ptr<message> make(std::shared_ptr<message> msg) {
        return std::move(msg);
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
     * @brief Construct a channel as a shared_ptr 
     * @param max_queue_size maximum concurrent count of messages this channel will store before send() calls fail. Default value is no limit
     * @return a channel shared_ptr
     */
    static inline std::shared_ptr<channel> make(std::size_t max_queue_size = channel::queue_no_limit) {
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
     * Send a message over the channel with given parameters 
     *
     * This is a non-blocking operation until the internal message queue
     * becomes full, at which point it becomes a blocking operation.
     *
     * @param as arguments passed to `st::message::make()`
     * @return result::estatus::success on success, result::estatus::closed if closed
     */
    template <typename... As>
    result send(As&&... as) {
        return internal_send(message::make(std::forward<As>(as)...));
    }

    /**
     * @brief Send a message over the channel
     *
     * This is a non-blocking operation.
     *
     * @param as arguments passed to `st::message::make()`
     * @return true on success, false if channel is closed
     */
    template <typename... As>
    result try_send(As&&... as) {
        return internal_try_send(message::make(std::forward<As>(as)...));
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
        bool notify = false;

        auto no_messages = [&]{ return m_msg_q.empty() && !m_closed; };

        if(m) {
            std::unique_lock<std::mutex> lk(m_mtx);
            if(no_messages()) {
                m_receivers_count++;

                do {
                    m_receiver_cv.wait(lk);
                } while(no_messages());
                
                m_receivers_count--;
            }

            if(m_msg_q.empty()) {
                return result{ result::estatus::closed };
            } else {
                m = m_msg_q.front();
                m_msg_q.pop_front();

                if(m_senders_count) {
                    notify = true;
                }
            } 
        } else {
            return result{ result::estatus::argument_null };
        }

        if(notify) {
            m_sender_cv.notify_one();
        }

        return result{ result::estatus::success };
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

    inline result internal_send(std::shared_ptr<message> m) {
        bool notify = false;

        if(m) { 
            std::unique_lock<std::mutex> lk(m_mtx);
            if(internal_full()) {
                m_senders_count++;

                do {
                    m_sender_cv.wait(lk);
                } while(internal_full());

                m_senders_count--;
            }

            if(m_closed) {
                return result{ result::estatus::closed };
            } else {
                m_msg_q.push_back(std::move(m));

                if(m_receivers_count) {
                    notify = true;
                }
            }
        } else {
            return result{ result::estatus::argument_null };
        }

        if(notify) {
            m_receiver_cv.notify_one();
        }

        return result{ result::estatus::success };
    }

    inline result internal_try_send(std::shared_ptr<message> m) {
        bool notify = false;

        if(m) { 
            std::lock_guard<std::mutex> lk(m_mtx);
            if(m_closed) {
                return result{ result::estatus::closed };
            } else if(internal_full()) {
                return result{ result::estatus::full };
            } else {
                m_msg_q.push_back(std::move(m));

                if(m_receivers_count) {
                    notify = true;
                }
            }
        } else {
            return result{ result::estatus::argument_null };
        }

        if(notify) {
            m_receiver_cv.notify_one();
        }

        return r;
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
 *
 * System worker thread's lifecycle is directly controlled by this object. The
 * worker is capable of receiving messages sent via this object.
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
              int QUEUE_MAX_SIZE=channel::queue_no_limit, 
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
     * Send a message over the channel with given parameters 
     *
     * This is a blocking operation.
     *
     * @param as arguments passed to `st::message::make()`
     * @return result::estatus::success on success, result::estatus::closed if closed
     */
    template <typename... As>
    result send(As&&... as) {
        return m_ch->send(message::make(std::forward<As>(as)...));
    }

    /**
     * @brief Send a message over the channel
     *
     * This is a non-blocking operation.
     *
     * @param as arguments passed to `st::message::make()`
     * @return true on success, false if channel is closed
     */
    template <typename... As>
    result try_send(As&&... as) {
        return m_ch->try_send(message::make(std::forward<As>(as)...));
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

/**
 * @brief Managed timer and callback handler class  
 *
 * A timer can be started with `timer::service::start()`.
 *
 * Typical callback design is to have the callback hold a copy of a `st::worker` 
 * shared_ptr and `st::worker::send()` a message indicating the timer has timed
 * out. 
 *
 * Note: if the callback blocks indefinitely, the timer callback thread will 
 * also remain blocked. An option for addressing long running callbacks is to 
 * provide async==true to `timer::make()` which will cause a dedicated thread to 
 * be launched to execute the callback.
 */
struct timer {
    /**
     * @brief time_point type used by this class
     */
    typedef std::chrono::time_point<std::chrono::steady_clock> time_point;

    /**
     * @brief Construct a non-started timer
     *
     * @param callback function will be called when timeout occurs
     * @param sec seconds interval before timeout will occur
     * @param milli additional milliseconds interval before the timeout will occur 
     * @param repeat true if timer should repeat, else false 
     * @param async true if callback should execute on its own thread, else false 
     */
    inline std::shared_ptr<timer> make(
            std::function<void()> callback,
            std::size_t sec, 
            std::size_t milli, 
            bool repeat=false, 
            bool async=false) {
        std::shared_ptr<timer> sp(new timer(
                    std::move(handler),
                    sec,
                    milli,
                    repeat,
                    async);
        sp->m_self = sp;
        return sp;
    }

    /**
     * @return true if timer is running, else false
     */
    inline bool running() {
        return m_timeout.load() != time_point();
    }

    /**
     * @return the time_point representing the timeout
     */
    inline time_point get_timeout() {
        return m_timeout.load();
    }

    /**
     * @brief Structure representing the time left until timer times out
     */
    struct time_left {
        /**
         * @brief Seconds left until timeout
         */
        std::size_t sec; 

        /**
         * @brief Milliseconds left until timeout
         */
        std::size_t milli;
    };

    /**
     * @return the time_left on the timer till timeout
     */
    inline time_left get_time_left() {
        time_left ret{0,0};

        if(running()) {
            auto timeout_tp = m_timeout.load();
            auto now = std::chrono::steady_clock::now();

            if(timeout_tp > now) {
                auto diff = timeout_tp - now;
                auto dur_secs_left = std::chrono::duration_cast<std::chrono::seconds>(diff);

                auto dur_milli_left = 
                    std::chrono::duration_cast<std::chrono::milliseconds>(diff) -
                    std::chrono::duration_cast<std::chrono::milliseconds>(dur_secs_left);

                if(dur_secs_left.count()) {
                    ret.sec = dur_secs_left.count();
                }

                if(dur_milli_left.count()) {
                    ret.milli = dur_milli_left.count();
                }
            } 
        }

        return ret;
    }

    /**
     * @brief Timer executor object capable of starting and stopping timers
     *
     * Manages a timer callback thread which will execute timer callbacks on 
     * timeout.
     */
    struct service {
        /**
         * @brief Allocate a running timer service
         */
        inline std::shared_ptr<timer> make() {
            std::shared_ptr<service> sp(new service);
            sp->m_self = sp;
            return sp;
        }

        /**
         * @brief Destructor will end and join callback thread
         */
        ~service() {
            bool notify = false;

            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_running = false;

                if(m_interval_handler_waiting) {
                    notify = true;
                }
            }
               
            if(notify) { 
                m_cv.notify_one();
            }

            m_timeout_handler_thd.join();
        }

        /**
         * @brief Start the timer 
         * @param tim timer to be started
         * @return result::estatus::success on success, result::estatus::argument_null if argument is nullptr, else result::estatus::already_running if the timer was already started
         */
        template <typename... As>
        result start(std::shared_ptr<timer> tim) {
            bool notify = false;
            timer::time_point timeout;
            result r{result::estatus::success};

            if(!tim) { 
                return result{ result::estatus::argument_null };
            }

            {
                std::lock_guard<std::mutex> lk(m_mtx);

                // check if the timer can be started, and set the necessary 
                // internal state to started if it can be 
                r = tim->handle_start();

                if(!r) {
                    return r;
                } else {
                    timeout = tim->timeout();
                }

                auto it = m_timers.find(timeout);

                // ensure container for timers at timeout time_point exists
                if(it == m_timers.end()) {
                    it = m_timers.insert(timeout, timer_set_t());
                }

                it->second.insert(tim);

                if(m_interval_handler_waiting) {
                    notify = true;
                }
            }

            if(notify) {
                // wakeup timeout handler thread to process updated timers 
                m_cv.notify_one(); 
            }

            return r;
        }

        /**
         * @brief Stop a running timer 
         *
         * Will immediately return if timer is not running.
         *
         * @param tim timer to be stopped
         * @return result::estatus::success on success, result::estatus::argument_null if argument is nullptr, else result::estatus::not_found if timer could not be found
         */
        inline result stop(std::shared_ptr<timer> tim) {
            bool found = false;

            if(!tim) { 
                return result{ result::estatus::argument_null };
            }

            {
                std::lock_guard<std::mutex> lk(m_mtx);

                // Determine if we've already handled timeout or stopped timer, 
                // as the underlying value is only set by this class under mutex 
                // lock. If timer is not running, we have succeeded at stopping the 
                // timer.
                if(!(tim->running())) {
                    return result{ result::estatus::success };
                }

                // search through timeouts 
                auto timer_set_it = m_timers.find(tim->get_timeout());
                if(timer_set_it != m_timers.end()) {
                    // search through timer set associated with a timeout 
                    auto it = timer_set_it->second.find(tim);
                    if(it != timer_set_it->end()) {
                        found = true;
                        it->tim->handle_stop();
                        timer_set_it->erase(it);

                        // remove any empty timer containers
                        if(timer_set_it->empty()) {
                            m_timers.erase(timer_set_it);
                        }
                    }
                }
            }

            if(found) {  
                return result{ result::estatus::success };
            } else {
                return result{ result::estatus::not_found };
            }
        }

    private:
        typedef std::set<std::shared_ptr<timer>> timer_set_t;

        // Is a template to prevent compiler from generating code unless it is used.
        service() : 
            m_running(true), 
            m_interval_handler_waiting(false),
            m_timeout_handler_thd([&]{
                std::unique_lock<std::mutex> lk(m_mtx);

                auto self = m_self;

                while(m_running) {
                    std::vector<timer_set_t> ready_timer_sets;

                    for(auto it = m_timers.begin(); it!= m_timers.end(); it++) {
                        if(it->first < std::chrono::steady_clock::now()) {
                            // store timeout vectors in local container
                            ready_timer_sets.push_back(std::move(it->second));

                            // erase timeouts from member container
                            it = m_timers.erase(it);
                        } else {
                            // no more timers are ready
                            break;
                        }
                    }

                    // do not call timer handlers with lock to prevent deadlock
                    lk.unlock(); 
                        
                    // call timer handlers
                    for(auto& ready_timer_set : ready_timer_sets) { 
                        for(auto& tim : ready_timer_set) {
                            tim->handle_timeout(self);
                        }
                    }

                    // reacquire lock
                    lk.lock(); 

                    // wait until nearest timeout or indefinitely until a new 
                    // timer is started
                    if(m_running) {
                        m_interval_handler_waiting = true;

                        auto it = m_timers.begin();
                        if(it != m_timers.end()) {
                            auto timeout = it->first;
                            m_cv.wait_until(lk, timeout);
                        } else {
                            m_cv.wait(lk);
                        }

                        m_interval_handler_waiting = false;
                    } 
                }

                // set remaining timer state to indicate they are not running
                for(auto& timer_set : m_timers) {
                    for(auto& tim : timer_set) {
                        tim->handle_stop();
                    }
                }
            })
        { }

        service(const service& rhs) = delete;
        service(service&& rhs) = delete;

        std::weak_ptr<service> m_self;
        bool m_running;
        bool m_interval_handler_waiting; // heuristic to limit condition_variable signals
        std::mutex m_mtx;
        std::condition_variable m_cv;
        std::thread m_timeout_handler_thd;

        // container chosen for automatic ordering of timers by their timeout
        std::map<timer::time_point, timer_set_t> m_timers; 
    };

private:
    timer(std::function<void()> callback,
          std::size_t sec,
          std::size_t milli,
          bool repeat,
          bool async) : 
        m_callback(std::move(callback)),
        m_interval_sec(0),
        m_interval_milli(0),
        m_repeat(false),
        m_async(false)
    { }

    timer(const timer& rhs) = delete;
    timer(timer&& rhs) = delete;

    // The following timer operations are used exclusively by timer::service

    // do not start timer if the timer is already running, if no timeout has 
    // been set, or if no callback has been set
    inline result handle_start() {
        if(m_timeout.load() != time_point()) {
            return result{ result::estatus::already_running };
        } else {
            m_timeout.store(time_point{std::chrono::steady_clock::now() +
                                               std::chrono::seconds(m_interval_sec) +
                                               std::chrono::milliseconds(m_interval_milli)});
            return result{ result::estatus::success };
        }
    }

    // return true if repeat requested, else false
    inline void handle_timeout(std::weak_ptr<timer::service>& weak_svc) {
        if(m_async) {
            std::thread([=]() mutable { m_callback(); }).detach();
        } else {
            m_callback();
        }

        if(m_repeat) {
            auto svc = weak_svc.lock();
            if(svc) {
                svc->start(m_self.lock());
            } else {
                m_timeout.store(time_point());
            }
        } else {
            m_timeout.store(time_point());
        }
    }

    inline void handle_stop() {
        m_timeout.store(time_point());
    }

    std::function<void()> m_callback;
    std::size_t m_interval_sec;
    std::size_t m_interval_milli;
    bool m_repeat;
    bool m_async;
    std::weak_ptr<timer> m_self;
    std::atomic<time_point> m_timeout;
};

}

#endif
