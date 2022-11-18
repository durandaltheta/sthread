//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CONTEXT__
#define __SIMPLE_THREADING_CONTEXT__

#include <memory>
#include <typeinfo>

#include "utility.hpp"

namespace st { // simple thread

/**
 * @brief parent context interface
 */
struct context { 
    virtual ~context() { }

    /**
     * @brief convenience method to construct descendant contexts 
     * @param as optional constructor arguments for type `context`
     * @return an allocated shared pointer of an `st::context` dynamically cast from type `CONTEXT`
     */
    template <typename CONTEXT, typename... As>
    static std::shared_ptr<st::context> make(As&&... as) {
        // store as a raw shared_ptr to enable `std::shared_from_this` semantics
        std::shared_ptr<CONTEXT> ctx(new CONTEXT(std::forward<As>(as)...));

        // cast to parent `st::context`
        return std::dynamic_pointer_cast<st::context>(ctx);
    }

    /**
     * @brief convenience method to cast context to descendant type 
     * @return reference to descendant type
     */
    template <typename CONTEXT>
    CONTEXT& cast() {
        return *(dynamic_cast<CONTEXT*>(this));
    }
};

/**
 * @brief CRTP-templated interface to provide shared context api
 *
 * CRTP: curiously recurring template pattern
 */
template <typename CRTP>
struct shared_context {
    virtual ~shared_context() { }

    /**
     * WARNING: Blind manipulation of this value is dangerous.
     *
     * @return context shared pointer reference
     */
    inline std::shared_ptr<st::context>& ctx() const {
        return m_context;
    }
   
    /**
     * @param ctx assign the context shared pointer
     */
    inline void ctx(std::shared_ptr<st::context> new_ctx) {
        m_context = new_ctx;
    }

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

private:
    mutable std::shared_ptr<st::context> m_context;
};

}

#endif
