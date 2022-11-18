//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_REPLY__
#define __SIMPLE_THREADING_REPLY__

#include <memory>
#include <deque>

#include "utility.hpp"
#include "sender_context.hpp"

namespace st { // simple thread

/**
 * @brief object capable of sending a payload back to an `st::sender`
 *
 * This object provides a simple, lightweight way to send messages back to a 
 * requestor while abstracting the message passing details. This object can be 
 * the payload `st::data` of an `st::message`.
 */
struct reply : public shared_context<reply> {
    virtual ~reply(){}

    /**
     * @brief main constructor 
     *
     * lvalue make
     *
     * @param snd any object implementing `st::shared_sender_context` to send `st::message` back to 
     * @param id unsigned int id of `st::message` sent back over `ch`
     */
    template <typename CRTP>
    static inline reply make(shared_sender_context<CRTP>& snd, std::size_t id) { 
        reply r;
        r.ctx(st::context::make<reply::context>(snd.ctx(), id));
        return r;
    }

    /**
     * @brief main constructor 
     *
     * rvalue make
     *
     * @param snd any object implementing `st::shared_sender_context` to send `st::message` back to 
     * @param id unsigned int id of `st::message` sent back over `ch`
     */
    template <typename CRTP>
    static inline reply make(shared_sender_context<CRTP>&& snd, std::size_t id) { 
        reply r;
        r.ctx(st::context::make<reply::context>(std::move(snd.ctx()), id));
        return r;
    }

    /**
     * @brief send an `st::message` back to some abstracted `st::sender`
     * @param t `st::message` payload data 
     * @return `true` if internal `st::channel::send(...)` succeeds, else `false`
     */
    template <typename T>
    bool send(T&& t) {
        return this->ctx()->template cast<reply::context>().send(std::forward<T>(t));
    }

    inline reply& operator=(const reply& rhs) {
        ctx() = rhs.ctx();
        return *this;
    }

private:
    struct context : public st::context {
        context(std::shared_ptr<st::context> snd_ctx, std::size_t id) :
            m_snd_ctx(std::move(snd_ctx)),
            m_id(id)
        { }

        virtual ~context(){ }
    
        template <typename T>
        bool send(T&& t) {
            return m_snd_ctx->template cast<st::sender_context>().send(m_id, std::forward<T>(t));
        }

        std::shared_ptr<st::context> m_snd_ctx;
        std::size_t m_id;
    };
};

}

#endif
