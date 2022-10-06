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
        std::shared_ptr<st::context> ctx(
            dynamic_cast<st::context*>(new CONTEXT(std::forward<as>(as)...)));
        return ctx;
    }

    /**
     * @brief convenience method to cast context to descendant type 
     * @return reference to descendant type
     */
    template <typename context>
    constexpr context& cast() {
        return *(dynamic_cast<context*>(this));
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
     * @return reference to shared context
     */
    inline std::shared_ptr<st::context>& context() const {
        return m_context;
    }

    /// lvalue CRTP assignment
    inline CRTP& operator=(const CRTP& rhs) {
        context() = rhs.context();
        return *(dynamic_cast<CRTP*>(this));
    }

    /// rvalue CRTP assignment
    inline CRTP& operator=(CRTP&& rhs) {
        context() = std::move(rhs.context());
        return *(dynamic_cast<CRTP*>(this));
    }
    
    /// type conversion to base shared_context<CRTP> type
    inline operator CRTP() const {
        return *(dynamic_cast<CRTP*>(this));
    }

    /**
     * @return `true` if object is allocated, else `false`
     */
    inline operator bool() const {
        return context() ? true : false;
    }

    /**
     * @return `true` if argument CRTP represents this CRTP (or no CRTP), else `false`
     */
    inline bool operator==(const CRTP& rhs) const noexcept {
        return context() == rhs.context();
    }

    /**
     * @return `true` if `this` is less than `rhs`, else `false`
     */
    inline bool operator<(const CRTP& rhs) const noexcept {
        return context() < rhs.context();
    }

private: 
    mutable std::shared_ptr<st::context> m_context;
};

}

#endif
