//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_MESSAGE__
#define __SIMPLE_THREADING_MESSAGE__

#include "data.hpp"
#include "context.hpp"

namespace st { // simple thread

/**
 * @brief interthread type erased message container 
 *
 * this object is *not* mutex locked.
 */
struct message : public shared_context<message> {
    inline virtual ~message() { }

    /** 
     * @brief convenience function for templating 
     * @param msg `st::message` object to immediately return 
     * @return `st::message` object passed as argument
     */
    static inline message make(message msg) {
        return std::move(msg);
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @param t arbitrary typed data to be stored as the message data 
     * @return an allocated `st::message`
     */
    static inline message make(std::size_t id, st::data&& d) {
        message msg;
        msg.ctx(st::context::make<message::context>(
            id, 
            std::move(d)));
        return msg;
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @param t arbitrary typed data to be stored as the message data 
     * @return an allocated `st::message`
     */
    template <typename T>
    static message make(std::size_t id, T&& t) {
        return st::message::make(id, st::data::make<T>(std::forward<T>(t)));
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @return an allocated `st::message`
     */
    static inline message make(std::size_t id) {
        message msg;
        msg.ctx(st::context::make<message::context>(id));
        return msg;
    }

    /** 
     * @brief construct a message
     *
     * @return default allocated `st::message`
     */
    static inline message make() {
        return message::make(0);
    }

    /**
     * @brief an unsigned integer representing message's intended operation
     *
     * an `id` can trivially represent an enumeration, which can represent a 
     * specific request, response, or notification operation.
     */
    const std::size_t id() const {
        return this->ctx()->template cast<message::context>().m_id; }

    /**
     * @brief optional type erased payload data
     */
    inline st::data& data() {
        return this->ctx()->template cast<message::context>().m_data;
    }

    /**
     * @brief generic function wrapper for executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type. Is 
     * a new definition instead of a typedef so that it can be distinguished by 
     * receiving code. Messages processed by `st::message::handle(...)` will 
     * automatically execute any `st::message::task`s passed to it that are 
     * stored in the `st::message::data()` payload instead of passing the
     * message to be processed by the user handler.
     */
    struct task : public std::function<void()> { 
        template <typename... As>
        task(As&&... as) : std::function<void()>(std::forward<As>(as)...) { }
    };

    /**
     * @brief this function should be be called to properly process received messages 
     *
     * This only needs to be called once per message, can generally should *NOT* 
     * be called in user code, instead should be called by some higher level 
     * library object like `st::thread`.
     *
     * @param hdl Callable capable of accepting an `st::message` as an argument 
     * @param msg `st::message` to be passed to `hdl`
     */
    template <typename CUSTOM_HANDLER>
    static void handle(CUSTOM_HANDLER& hdl, st::message& msg) {
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
        if(msg) { // throw out any empty messages
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
            if(msg.data()) {
        st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
                if(msg.data().is<task>()) { // check if payload is an `st::message::task`
        st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
                    msg.data().cast_to<task>()(); // evaluate task immediately
        st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
                } else {
        st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
                    hdl(msg); // otherwise allow handler to process message 
        st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
                }
        st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
            } else {
        st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
            }
        }
    st::log(__PRETTY_FUNCTION__, "[", __LINE__, "]");
    }

    inline message& operator=(const message& rhs) {
        ctx() = rhs.ctx();
        return *this;
    }

private:
    struct context : public st::context {
        context(const std::size_t c) : m_id(c) { }

        context(const std::size_t c, st::data&& d) :
            m_id(c),
            m_data(std::move(d))
        { }

        virtual ~context() { }

        std::size_t m_id;
        st::data m_data;
    };
};

}

#endif
