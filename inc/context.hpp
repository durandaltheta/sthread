//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CONTEXT__
#define __SIMPLE_THREADING_CONTEXT__

#include <memory>
#include <typeinfo>

#include "utility.hpp"

namespace st { // simple thread

/**
 * @brief parent context definition
 */
struct context { 
    virtual ~context() { }
};

/**
 * @brief CRTP-templated interface to provide shared context api
 *
 * CRTP: curiously recurring template pattern
 * CRTPCTX: the CRTP's context type
 */
template <typename CRTP, typename CRTPCTX>
struct shared_context {
protected:
    /**
     * WARNING: Blind manipulation of this value is dangerous.
     *
     * @return context shared pointer reference
     */
    inline std::shared_ptr<CRTPCTX>& ctx() const {
        return std::dynamic_pointer_cast<CRTPCTX>(this->m_context);
    }
   
    /**
     * @param ctx assign the context shared pointer
     */
    inline void ctx(std::shared_ptr<CRTPCTX> new_ctx) {
        this->m_context = std::dynamic_pointer_cast<context>(new_ctx);
    }

public:
    virtual ~shared_context() { }

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() const {
        return this->ctx().operator bool();
    }

    /**
     * @return `true` if argument CRTP represents this CRTP (or no CRTP), else `false`
     */
    inline bool operator==(const CRTP& rhs) const noexcept {
        return this->ctx() == rhs.ctx();
    }

    /**
     * @return `true` if argument CRTP represents this CRTP (or no CRTP), else `false`
     */
    inline bool operator!=(const CRTP& rhs) const noexcept {
        return this->ctx() != rhs.ctx();
    }

    /**
     * @return `true` if `this` is less than `rhs`, else `false`
     */
    inline bool operator<(const CRTP& rhs) const noexcept {
        return this->ctx() < rhs.ctx();
    }
    
    inline CRTP& operator=(const CRTP& rhs) {
        ctx() = rhs.ctx();
        return *this;
    }

private:
    mutable std::shared_ptr<st::context> m_context;
};

}

#endif
