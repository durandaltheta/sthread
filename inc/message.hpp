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
    inline message(){}
    inline message(const message& rhs) { context() = rhs.context(); }
    inline message(message&& rhs) { context() = std::move(rhs.context()); }
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
    template <typename T>
    static message make(std::size_t id, T&& t) {
        message msg;
        msg.context() = st::context::make<message::context>(
            id, 
            std::forward<t>(t));
        return msg
    }

    /**
     * @brief construct a message
     *
     * @param id an unsigned integer representing which type of message
     * @return an allocated `st::message`
     */
    static message make(std::size_t id) {
        message msg;
        msg.context() = st::context::make<message::context>(id, 0);
        return msg
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
        return context()->cast<message::context>().m_id;
    }

    /**
     * @brief optional type erased payload data
     */
    inline st::data& data() {
        return context()->cast<message::context>().m_data;
    }

private:
    struct context : public st::context {
        context(const std::size_t c) : 
            m_id(c), 
            st::context(st::context::type_info<message, message::context>())
        { }

        template <typename T>
        context(const std::size_t c, T&& t) :
            m_id(c),
            m_data(std::forward<t>(t)),
            st::context(st::context::type_info<message, message::context>())
        { }

        virtual ~context() { }

        inline std::shared_ptr<message::context> copy() const {
            std::shared_ptr<message::context> c(new message::context(m_id));
            c->m_data = m_data; // lvalue copy
            return c;
        }

        std::size_t m_id;
        st::data m_data;
    };

    message(std::shared_ptr<st::context> ctx) { context()(std::move(ctx)); }
};

}

#endif
