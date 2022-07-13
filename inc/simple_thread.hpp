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
#include <unordered_map>

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

    /**
     * Construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @param t arbitrary typed data to be stored as the message data 
     * @return an allocated message
     */
    template <typename ID, typename T>
    static std::shared_ptr<message> make(ID id, T&& t) {
        return std::shared_ptr<message>(new message(
            static_cast<std::size_t>(id),
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
    template <typename ID>
    static std::shared_ptr<message> make(ID id) {
        return std::shared_ptr<message>(new message(
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
        closed
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
    template <typename ID, typename T>
    result send(ID id, T&& t) {
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
    template <typename ID>
    result send(ID id) {
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
    template <typename ID, typename T>
    result try_send(ID id, T&& t) {
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
    template <typename ID>
    result try_send(ID id) {
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
    template <typename ID, typename T>
    result send(ID id, T&& t) {
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
    template <typename ID>
    result send(ID id) {
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
    template <typename ID, typename T>
    result try_send(ID id, T&& t) {
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
    template <typename ID>
    result try_send(ID id) {
        return m_ch->try_send(id);
    }

    /**
     * @brief Provide access to calling worker object pointer
     *
     * If not called by a running worker, returned weak_ptr is expired. Useful 
     * when the worker pointer is needed from within the FUNCTOR handler.
     *
     * Provided as a weak_ptr to avoid preventing the worker from going out of 
     * scope unless the user explicitly acquires a shared_ptr from it.
     *
     * @return a weak_ptr to calling thread's worker
     */
    static inline std::weak_ptr<worker> this_worker() {
        auto wp = tl_worker();
        if(wp) {
            return wp->m_self;
        } else {
            return std::weak_ptr<worker>();
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

/*
 * A simple and extensible finite state machine mechanism (FSM). FSMs are
 * somewhat infamous for being difficult to parse, too unwieldy, or otherwise 
 * opaque. As with everything else in this library, the aim of this object's 
 * design is to make sure the necessary features are kept simple without overly 
 * limiting the user.
 *
 * The reasoning for even including this in the library is that the inherent 
 * complexity of asynchronous programming can lead to complex state management.
 * Simplifying designs with a state machine can *sometimes* be advantagous, 
 * when used intelligently and judiciously. 
 *
 * The toplevel class for this feature is actually the `state` object. The user 
 * should implement classes which inherit `st::state`. A static `state::make()` 
 * function is included as convenience for the user so they do not have to 
 * manually typecast their allocated pointers. 
 *
 * User must create a state machine (`std::shared_ptr<st::state::machine>`) 
 * using static function `st:;state::machine::make()` to register their states.
 * The state machine can then be notified of new events with a call to 
 * `st::state::machine::notify(std::shared_ptr<st::message>)`.
 */
struct state {
    // explicit destructor definition to allow for proper delete behavior
    virtual ~state(){} 

    /*
     * @brief called when during a transition when a state is entered 
     * param event a message containing the event id and an optional data payload
     */
    virtual void enter(std::shared_ptr<message> event){ }

    /*
     * @brief called when during a transition when a state is exitted
     * param event a message containing the event id and an optional data payload
     * return true if exit succeeded and transition can continue, else false
     */
    virtual bool exit(std::shared_ptr<message> event){ return true; }

    /*
     * @brief a convenience function for generating shared_ptr's to state objects
     * param as Constructor arguments for type T
     * return an allocated shared_ptr to type T implementing state
     */
    template <typename T, typename... As>
    static std::shared_ptr<state> make(As&&... as) {
        return std::shared_ptr<state>(dynamic_cast<state*>(new T(std::forward<As>(as)...)));
    }

    /*
     * The actual state machine.
     *
     * This object is NOT mutex locked, as it is not intended to be used directly 
     * in an asynchronous manner. 
     */
    struct machine {
        static inline std::shared_ptr<machine> make() {
            return std::shared_ptr<machine>(new machine);
        }

        /*
         * @brief Register a state object to be transitioned to when notified of an event
         * param event an unsigned integer representing an event that has occurred
         * param st a pointer to an object which implements abstract class state  
         * return true if state was registered, false if state pointer is null or the same event is already registered
         */
        inline bool register_transition(std::size_t event_id, std::shared_ptr<state> st) {
            auto it = m_transition_table.find(event_id);
            if(st && it == m_transition_table.end()) {
                m_transition_table[event_id] = st;
                return true;
            } else {
                return false;
            }
        }

        template <typename ID>
        bool register_transition(ID event_id, std::shared_ptr<state> st) {
            return register_transition(static_cast<std::size_t>(event_id), std::move(st));
        }

        /*
         * @brief process_event the state machine an event has occurred 
         * param event a message containing the state event id and optional data payload 
         * return true if the event was processed successfully, else false
         */
        inline bool process_event(std::shared_ptr<message> event) {
            if(event) {
                auto it = m_transition_table.find(event->id());

                if(it != m_transition_table.end()) {
                    // exit old state 
                    if(m_cur_state != m_transition_table.end()) {
                        if(!m_cur_state->second->exit(event)) {
                            // exit early as transition cannot continue
                            return true; 
                        }
                    }

                    // enter new state
                    it->second->enter(event);
                    m_cur_state = it;
                    return true;
                } else { 
                    return false;
                }
            } else {
                return false;
            }
        }
       
        template <typename ID, typename T>
        bool process_event(ID id, T&& t) {
            return process_event(st::message::make(id, std::forward<T>(t)));
        }
       
        template <typename ID>
        bool process_event(ID id) {
            return process_event(st::message::make(id));
        }

    private:
        machine() : m_cur_state(m_transition_table.end()) { }

        typedef std::unordered_map<std::size_t,std::shared_ptr<state>> transition_table_t;
        transition_table_t m_transition_table;
        transition_table_t::iterator m_cur_state;
    };
};

}

#endif
