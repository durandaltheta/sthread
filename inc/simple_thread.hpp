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
#include <unordered_map>
#include <set>

namespace st {

/** @cond HIDDEN_SYMBOLS */
#ifdef __SIMPLE_THREADING_DEBUG__ 
namespace log {

#include <ostream>

namespace detail {

static std::ostream& debug_stream();
static std::ostream& error_stream();

#ifdef __SIMPLE_THREADING_DEBUG_DEFAULT__
static inline std::ostream& debug_stream() {
    return std::cout;
}

static inline std::ostream& error_stream() {
    return std::cerr;
}
#endif 

inline void log(std::ostream& os) {
    os << std::endl;
}

template <typename T, typename... As>
void log(std::ostream& os, T&& t, As&&... as) {
    os << t;
    log(os, std::forward<As>(as)...);
}

struct log_mutex {
    static std::mutex& instance() {
        static std::mutex log_mutex;
        return log_mutex;
    }
};

}

template <typename... As>
void debug(const char* funcname, As&&... as) {
    std::lock_guard<std::mutex> lk(detail::log_mutex::instance());
    detail::debug_stream() << "[" << std::this_thread::get_id() << "]::" << funcname << "::";
    detail::log(detail::debug_stream(), std::forward<As>(as)...);
}

template <typename... As>
void error(const char* funcname, As&&... as) {
    std::lock_guard<std::mutex> lk(detail::log_mutex::instance());
    detail::error_stream() << "[" << std::this_thread::get_id() << "]!!" << funcname << "!!";
    detail::log(detail::error_stream(), std::forward<As>(as)...);
}

}

#define STDEBUG(...) st::log::debug(__VA_ARGS__)
#define STERROR(...) st::log::error(__VA_ARGS__)
#else 
#define STDEBUG(...)
#define STERROR(...)
#endif
/** @endcond */


/**
 * @brief Interthread type erased message container
 */
struct message : public std::enable_shared_from_this<message> {
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
        return m_data_type == code<T>();
    }

    /**
     * Copy the data to argument t
     *
     * @param t reference to templated variable t to deep copy the data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool copy_data_to(T& t) {
        std::lock_guard<std::mutex> lk(m_mtx);
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
        std::lock_guard<std::mutex> lk(m_mtx);
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

    std::mutex m_mtx;
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
struct channel : public std::enable_shared_from_this<channel> { 
    /**
     * @brief Construct a channel as a shared_ptr 
     * @return a channel shared_ptr
     */
    static inline std::shared_ptr<channel> make() {
        STDEBUG(__FUNCTION__, "+");
        auto ch = std::shared_ptr<channel>(new channel);
        STDEBUG(__FUNCTION__, "-");
        return ch;
    }

    /**
     * @return count of messages in the queue
     */
    inline std::size_t queued() {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_msg_q.size();
    }

    /**
     * @brief Send a message over the channel
     * @param m interprocess message object
     * @return true on success, false if channel is closed
     */
    inline bool send(std::shared_ptr<message> m) {
        STDEBUG(__FUNCTION__, "+");
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if(m_closed) {
                STERROR(__FUNCTION__, "closed");
                return false;
            }
            m_msg_q.push_back(std::move(m));
        }
        m_cv.notify_one();
        STDEBUG(__FUNCTION__, "sent");
        STDEBUG(__FUNCTION__, "-");
        return true;
    }

    /**
     * Send a message over the channel with given parameters 
     *
     * @param id an unsigned integer representing which type of message 
     * @param t template variable to package as the message payload
     * @return true on success, false if channel is closed
     */
    template <typename T>
    bool send(std::size_t id, T&& t) {
        return send(message::make(id, std::forward<T>(t)));
    }
    
    /**
     * Send a message over the channel with given parameter
     *
     * @param id an unsigned integer representing which type of message 
     * @return true on success, false if channel is closed
     */
    bool send(std::size_t id) {
        return send(message::make(id, 0));
    }

    /**
     * @brief Receive a message over the channel 
     * @param m interprocess message object reference to contain the received message 
     * @return true on success, false if channel is closed
     */
    bool recv(std::shared_ptr<message>& m) {
        STDEBUG(__FUNCTION__, "+");
        std::unique_lock<std::mutex> lk(m_mtx);
        while(m_msg_q.empty() && !m_closed) {
            STERROR(__FUNCTION__, "waiting");
            m_cv.wait(lk);
        }

        if(!m_closed || (!m_msg_q.empty() && m_proc_rem_msgs)) {
            m = m_msg_q.front();
            m_msg_q.pop_front();
            STERROR(__FUNCTION__, "received");
            STDEBUG(__FUNCTION__, "-");
            return true;
        } else {
            STERROR(__FUNCTION__, "closed");
            STDEBUG(__FUNCTION__, "-");
            return false;
        } 
    }

    /**
     * @return true if channel is closed, else false 
     */
    inline bool closed() {
        STDEBUG(__FUNCTION__, "+");
        std::lock_guard<std::mutex> lk(m_mtx);
        STDEBUG(__FUNCTION__, "-");
        return m_closed;
    }

    /**
     * @brief Close the channel 
     *
     * Ends all current and future operations on the channel 
     *
     * @param process_remaining_messages if true allow recv() to succeed until queue empty before closing
     */
    inline void close(bool process_remaining_messages=true) {
        STDEBUG(__FUNCTION__, "+");
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            STDEBUG(__FUNCTION__, "set to closed");
            m_closed = true;
            m_proc_rem_msgs = process_remaining_messages;
        }

        m_cv.notify_one();
        STDEBUG(__FUNCTION__, "-");
    }

private:
    channel() : m_closed(false), m_proc_rem_msgs(false) { }
    channel(const channel& rhs) = delete;
    channel(channel&& rhs) = delete;

    bool m_closed;
    bool m_proc_rem_msgs;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<message>> m_msg_q;
};

/**
 * @brief Managed system worker thread
 */
struct worker: public std::enable_shared_from_this<worker> {
    /**
     * Launch a worker with argument handler FUNCTOR to be executed whenever a 
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
    template <typename FUNCTOR, typename... As>
    static std::shared_ptr<worker> make(As&&... as) {
        return std::shared_ptr<worker>(new worker(
                    type_hint<FUNCTOR>(), 
                    std::forward<As>(as)...));
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
        STDEBUG(__FUNCTION__, "+");
        std::lock_guard<std::mutex> lk(m_mtx);
        bool r = m_thd.joinable() && !m_ch->closed();
        STDEBUG(__FUNCTION__, "running[", r, "]");
        STDEBUG(__FUNCTION__, "+");
        return r;
    }

    /** 
     * @brief Shutdown the worker thread
     *
     * @param process_remaining_messages if true allow recv() to succeed until queue empty before closing
     */
    inline void shutdown(bool process_remaining_messages=true) {
        STDEBUG(__FUNCTION__, "+");
        std::lock_guard<std::mutex> lk(m_mtx);
        inner_shutdown(process_remaining_messages);
        STDEBUG(__FUNCTION__, "-");
    }

    /**
     * @brief Restart the worker thread 
     *
     * Shutdown threads and reset state when necessary. 
     *
     * @param process_remaining_messages if true allow recv() to succeed until queue empty before closing
     */
    inline void restart(bool process_remaining_messages=true) {
        STDEBUG(__FUNCTION__, "+");
        std::unique_lock<std::mutex> lk(m_mtx);
        inner_shutdown(process_remaining_messages);
        m_ch = channel::make();
        STDEBUG(__FUNCTION__, "launching worker");
        m_thd = std::thread([&]{
            STDEBUG("worker_thread", "+");
            std::shared_ptr<message> m;
            tl_worker() = this; // set the thread local worker pointer

            auto hdl = m_generate_handler();

            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_thread_started_flag = true;
            }
            m_cv.notify_one();

            STDEBUG("worker_thread", "receiving message");
            while(m_ch->recv(m)) {
                STDEBUG("worker_thread", "handling message");
                hdl(m);
                m.reset();
            }

            tl_worker() = nullptr; // reset the thread local worker pointer
            STDEBUG("worker_thread", "-");
        });

        while(!m_thread_started_flag) {
            m_cv.wait(lk);
        }

        STDEBUG(__FUNCTION__, "-");
    }

    /**
     * @brief Send a message over the channel
     * @param m interprocess message object
     * @return true on success, false if channel is closed
     */
    inline bool send(std::shared_ptr<message> m) {
        return m_ch->send(std::move(m));
    }

    /**
     * @brief Send a message over the channel with given parameters 
     *
     * @param id an unsigned integer representing which type of message 
     * @param t template variable to package as the message payload
     * @return true on success, false if channel is closed
     */
    template <typename T>
    bool send(std::size_t id, T&& t) {
        return m_ch->send(message::make(id, std::forward<T>(t)));
;
    }

    /**
     * Send a message over the channel with given parameter
     *
     * @param id an unsigned integer representing which type of message 
     * @return true on success, false if channel is closed
     */
    bool send(std::size_t id) {
        return send(id, 0);
;
    }

    /**
     * @brief Provide access to calling worker thread shared_ptr
     *
     * If not called by a running worker, returned pointer is null. Useful when 
     * the worker pointer is needed from within the FUNCTOR handler.
     *
     * @return calling thread's worker shared_ptr. 
     */
    static inline std::shared_ptr<worker> this_worker() {
        STDEBUG(__FUNCTION__, "+");
        auto w = tl_worker();
        if(w) {
            STDEBUG(__FUNCTION__, "got this_worker");
            STDEBUG(__FUNCTION__, "-");
            return w->shared_from_this();
        } else {
            STERROR(__FUNCTION__, "failed to get this_worker");
            STDEBUG(__FUNCTION__, "-");
            return std::shared_ptr<worker>();
        }
    }

private:
    typedef std::function<void(std::shared_ptr<message>)> handler;

    template <typename FUNCTOR>
    struct type_hint { };

    template <typename T>
    static void deleter(void* p) {
        delete (T*)p;
    }

    template <typename FUNCTOR, typename... As>
    worker(type_hint<FUNCTOR> t, As&&... as) : m_thread_started_flag(false) { 
        STDEBUG(__FUNCTION__, "+");
        // generate handler s called late and allocates a shared_ptr to allow 
        // for a single construction and destruction of type FUNCTOR in the 
        // worker thread environment. 
        m_generate_handler = [=]() mutable -> handler{ 
            auto fp = std::shared_ptr<FUNCTOR>(new FUNCTOR(std::forward<As>(as)...));
            return [=](std::shared_ptr<message> m) { (*fp)(std::move(m)); };
        };
        restart();
        STDEBUG(__FUNCTION__, "-");
    }

    // thread local worker by-reference getter 
    static inline worker*& tl_worker() {
        thread_local worker* w(nullptr);
        return w;
    }

    // close the worker channel and join the thread if necessary
    inline void inner_shutdown(bool proc_rem_msgs=true) {
        STDEBUG(__FUNCTION__, "+");
        if(m_thd.joinable()) {
            if(m_ch && !m_ch->closed()) {
                STDEBUG(__FUNCTION__, "closing worker channel");
                m_ch->close(proc_rem_msgs);
            }
            STDEBUG(__FUNCTION__, "joining worker");
            m_thd.join();
        }

        m_thread_started_flag = false;
        STDEBUG(__FUNCTION__, "-");
    }

    bool m_thread_started_flag;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::function<handler()> m_generate_handler;
    std::shared_ptr<channel> m_ch;
    std::thread m_thd;
};

/** @cond HIDDEN_SYMBOLS */
namespace detail {
struct service {
    template <typename FUNCTOR>
    static inline worker& instance() {
        static service_worker<FUNCTOR> sw;
        return *(sw.m_wkr);
    }

    static inline void shutdown_all(bool proc_rem_msgs) {
        workers::instance().shutdown_all(proc_rem_msgs);
    }

    static inline void restart_all(bool proc_rem_msgs) {
        workers::instance().restart_all(proc_rem_msgs);
    }

private:
    struct workers {
        static inline workers& instance() {
            static workers ws;
            return ws;
        }

        inline void register_worker(worker* w) {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_ws.insert(w);
        }

        inline void unregister_worker(worker* w) {
            std::lock_guard<std::mutex> lk(m_mtx);
            auto it = m_ws.find(w);
            if(it != m_ws.end()) {
                m_ws.erase(it);
            }
        }

        inline void shutdown_all(bool proc_rem_msgs) {
            std::lock_guard<std::mutex> lk(m_mtx);

            for(auto w : m_ws) {
                w->shutdown(proc_rem_msgs);
            }
        }

        inline void restart_all(bool proc_rem_msgs) {
            std::lock_guard<std::mutex> lk(m_mtx);

            for(auto w : m_ws) {
                w->restart(proc_rem_msgs);
            }
        }

        std::mutex m_mtx;
        std::set<worker*> m_ws;
    };

    template <typename FUNCTOR>
    struct service_worker {
        service_worker() : m_wkr(worker::make<FUNCTOR>()) {
            workers::instance().register_worker(m_wkr.get());
        }

        ~service_worker() {
            workers::instance().unregister_worker(m_wkr.get());
        }

        std::shared_ptr<worker> m_wkr;
    };
};
}
/** @endcond */

/** 
 * @brief Access a singleton instance of a worker thread 
 *
 * Worker thread will be running template type FUNCTOR as its handler. A 
 * limitation of service worker threads is that type FUNCTOR is always default 
 * constructed (there's no place constructor arguments can be provided).
 *
 * The worker is returned as a reference instead of a shared_ptr as a minor 
 * usage convenience because the underlying worker object is guaranteed to 
 * remain in existence for the runtime of the program. 
 *
 * @return singleton worker thread reference
 */
template <typename FUNCTOR>
static inline worker& service() {
    return detail::service::instance<FUNCTOR>();
}

/**
 * @brief Shutdown all singleton worker threads
 *
 * All worker threads created by calls to service<FUNCTOR>() can be shutdown 
 * by a single call to this function.
 *
 * @param process_remaining_messages if true allow recv() to succeed until queue empty before closing
 */
inline void shutdown_all_services(bool process_remaining_messages=true) {
    detail::service::shutdown_all(process_remaining_messages);
}

/**
 * @brief Restart all singleton worker threads
 *
 * All worker threads created by calls to service<FUNCTOR>() can be restarted
 * by a single call to this function.
 *
 * @param process_remaining_messages if true allow recv() to succeed until queue empty before closing
 */
inline void restart_all_services(bool process_remaining_messages=true) {
    detail::service::restart_all(process_remaining_messages);
}

}

#endif
