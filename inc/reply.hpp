//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_REPLY__
#define __SIMPLE_THREADING_REPLY__

#include <memory>
#include <deque>

#include "utility.hpp"
#include "context.hpp"
#include "channel.hpp"

namespace st { // simple thread
namespace detail {
namespace reply {

struct context {
    context(st::channel ch, std::size_t id) :
        m_ch(std::move(ch)),
        m_id(id)
    { }

    virtual ~context(){ }

    template <typename T>
    bool send(T&& t) {
        return m_ch.send(m_id, std::forward<T>(t));
    }

    st::channel m_ch;
    std::size_t m_id;
};

}
}

/**
 * @brief object capable of sending a `st::message` back to an `st::channel`
 *
 * This object provides a simple, lightweight way to send messages back to a 
 * requestor while abstracting the message passing details. This object can be 
 * the payload `st::data` of an `st::message`.
 */
struct reply : protected st::shared_context<reply, detail::reply::context> {
    virtual ~reply(){}

    inline reply& operator=(const reply& rhs) {
        ctx() = rhs.ctx();
        return *this;
    }

    /**
     * @brief main constructor 
     *
     * lvalue make
     *
     * @param ch an `st::channel` to send `st::message` back to 
     * @param id unsigned int id of `st::message` sent back over `ch`
     */
    static inline reply make(st::channel& ch, std::size_t id = 0) { 
        reply r;
        r.ctx(std::make_shared<detail::reply::context>(ch, id));
        return r;
    }

    /**
     * @brief main constructor 
     *
     * rvalue make
     *
     * @param ch an `st::channel` to send `st::message` back to 
     * @param id unsigned int id of `st::message` sent back over `ch`
     */
    static inline reply make(st::channel&& ch, std::size_t id = 0) { 
        reply r;
        r.ctx(std::make_shared<detail::reply::context>(std::move(ch), id));
        return r;
    }

    /**
     * @brief send an `st::message` back to some abstracted `st::channel`
     * @param t `st::message` payload data 
     * @return `true` if internal `st::channel::send(...)` succeeds, else `false`
     */
    template <typename T>
    bool send(T&& t) {
        return ctx() ? ctx()->send(std::forward<T>(t)) : false;
    }
};

}

#endif
