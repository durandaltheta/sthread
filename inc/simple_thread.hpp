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
#include <set>

namespace st {

/**
 * @brief Interthread type erased message container
 */
struct message : public std::enable_shared_from_this<message> {
    /**
     * @brief Typedef representing the unqualified type of T
     */
    template <typename T>
    using base = typename std::remove_cv<T>::type;

    /**
     * @return an unsigned integer representing a data type.
     *
     * The data type value is acquired by removing const and volatile 
     * qualifiers and then by acquiring the type_info::hash_code().
     *
     * As a note, because this is a constexpr this can be used as switch case:
     * ```
     * switch(msg->type()) {
     *     case message::code<int>(): 
     *         int i=0;
     *         msg->copy_data_to(i);
     *         std::cout << "i: " << i << std::endl;
     *         break;
     *     case message::code<std::string>(): 
     *         std::string s;
     *         msg->copy_data_to(s);
     *         std::cout << "s: " << s << std::endl;
     *         break;
     *     default:
     *         std::cout << "unknown type" << std::endl;
     *         break;
     * }
     * ```
     */
    template <typename T>
    constexpr std::size_t code() const {
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
            t = *((base<T>*)m_data);
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
            std::swap(t, *((base<T>*)m_data));
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
        id(c),
        m_data_type(t),
        m_data(std::move(p))
    { }

    template <typename T>
    void* allocate(T&& t) {
        return (void*)(new base<T>(std::forward<T>(t)));
    }

    template <typename T>
    static void deleter(void* p) {
        delete (base<T>*)p;
    }

    typedef void(*deleter_t)(void*);
    typedef std::unique_ptr<void,deleter_t> data_pointer_t;

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
        return std::shared_ptr<channel>(new channel);
    }

    /**
     * @brief Send a message over the channel
     * @param m interprocess message object
     * @return true on success, false if channel is closed
     */
    inline bool send(std::shared_ptr<message> m) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if(!m_closed) {
                return false;
            }
            m_msg_q.push_back(std::move(m));
        }
        m_cv.notify_one();
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
     * @brief Receive a message over the channel 
     * @param m interprocess message object reference to contain the received message 
     * @return true on success, false if channel is closed
     */
    bool recv(std::shared_ptr<message>& m) {
        std::unique_lock<std::mutex> lk(m_mtx);
        while(m_msg_q.empty() && !m_closed) {
            m_cv.wait(lk);
        }

        if(m_closed) {
            return false;
        } else {
            m = m_msg_q.front();
            m_msg_q.pop_front();
            return true;
        }
    }

    /**
     * @return true if channel is closed, else false 
     */
    inline bool closed() {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_closed;
    }

    /**
     * @brief Close the channel 
     *
     * Ends all current and future operations on the channel 
     */
    inline void close() {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_closed = true;
        }

        m_cv.notify_one();
    }

private:
    channel() : m_closed(false) { }
    channel(const channel& rhs) = delete;
    channel(channel&& rhs) = delete;

    bool m_closed;
    std::mutex m_mtx;
    std:;condition_variable m_cv;
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
     *     void operator()(std::shared_ptr<message> m);
     * };
     * ```
     *
     * Using a functor is useful because it allows member data to persist
     * between calls to `void operator()(std::shared_ptr<message> m)` and for 
     * all member data to be easily accessible. 
     *
     * Another distinct advantage is functors are able to make intelligent use 
     * of C++ RAII semantics, as the functor will come into existence on a 
     * running worker thread and be destroyed when that thread ends.
     *
     * @param as constructor arguments for type FUNCTOR
     * @return allocated running worker thread shared_ptr
     */
    template <typename FUNCTOR, typename... As>
    std::shared_ptr<worker> make(As&&... as) {
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
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_thd.joinable() && !m_ch->closed();

    }

    /** 
     * @brief Shutdown the worker thread
     */
    inline void shutdown() {
        std::lock_guard<std::mutex> lk(m_mtx);
        inner_shutdown();
    }

    /**
     * @brief Restart the worker thread 
     *
     * Shutdown threads and reset state when necessary. 
     */
    inline void restart() {
        std::lock_guard<std::mutex> lk(m_mtx);
        inner_shutdown();
        m_ch = channel::make();
        m_thd = std::thread([&]{
            std::shared_ptr<message> m;
            tl_worker() = this; // set the thread local worker pointer

            auto hdl = m_generate_handler();

            while(m_ch->recv(m)) {
                (*hdl)(m);
                m.reset();
            }

            tl_worker() = nullptr; // reset the thread local worker pointer
        });
    }

    /**
     * @brief Send a message over the channel
     * @param m interprocess message object
     * @return true on success, false if channel is closed
     */
    inline bool send(std::shared_ptr<message> m) {
        return m_ch.send(std::move(m));
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
        return m_ch.send(message::make(id, std::forward<T>(t)));
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
        auto w = tl_worker();
        if(w) {
            return w->shared_from_this();
        } else {
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
    worker(type_hint<FUNCTOR> t, As&&... as) { 
        // generate handler s called late and allocates a shared_ptr to allow 
        // for a single construction and destruction of type FUNCTOR in the 
        // worker thread environment. 
        m_generate_handler = ([=]() mutable { 
            auto fp = std::shared_ptr<FUNCTOR>(new FUNCTOR(std::forward<As>(as)...));
            return [=](std::shared_ptr<message> m) { (*fp)(std::move(m)); };
        })
        restart();
    }

    // thread local worker by-reference getter 
    static inline worker*& tl_worker() {
        thread_local worker* w(nullptr);
        return w;
    }

    // close the worker channel and join the thread if necessary
    inline void inner_shutdown() {
        if(m_thd.joinable()) {
            if(m_ch && !m_ch->closed()) {
                m_ch->close();
            }
            m_thd.join();
        }
    }

    std::mutex m_mtx;
    std::function<std::shared_ptr<handler>()> m_generate_handler;
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

    inline void shutdown_all() {
        workers::instance().shutdown_all();
    }

    inline void restart_all() {
        workers::instance().restart_all();
    }

private:
    struct workers {
        static inline workers& instance() {
            workers ws;
            return ws;
        }

        inline void register_worker(worker* w) {
            std::lock_guard<std::mutex> lk(m_mtx);
            m_ws.insert(w);
        }

        inline void unregister_worker(worker* w) {
            std::lock_guard<std::mutex> lk(m_mtx);
            auto it = m_ws.find(&w);
            if(it != m_ws.end()) {
                m_ws.erase(it);
            }
        }

        inline void shutdown_all() {
            std::lock_guard<std::mutex> lk(m_mtx);

            for(auto w : m_ws) {
                m_ws->shutdown();
            }
        }

        inline void restart_all() {
            std::lock_guard<std::mutex> lk(m_mtx);

            for(auto w : m_ws) {
                m_ws->restart();
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
}
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
 */
inline void shutdown_all_services() {
    detail::service::instance().shutdown_all();
}

/**
 * @brief Restart all singleton worker threads
 *
 * All worker threads created by calls to service<FUNCTOR>() can be restarted
 * by a single call to this function.
 */
inline void restart_all_services() {
    detail::service::instance().restart_all();
}

}

#endif
