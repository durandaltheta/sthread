//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_CONTEXT__
#define __SIMPLE_THREADING_CONTEXT__

#include <memory>
#include <typeinfo>

#include "utility.hpp"

namespace st { // simple thread

/**
 * @brief CRTP-templated interface to provide shared context api 
 *
 * Implements the the shared API between objects in this library which act as 
 * abstracted shared pointers. By inheritting this template a type can guarantee 
 * that it has a standardized implementation for various basic operators:
 * ==
 * !=
 * <
 * =
 * bool conversion 
 *
 * As well as standardized implementation for accessing the underlying pointer 
 * (of type CRTPCTX) which contains the real implementation:
 * ctx() -> shared_ptr<CRTPCTX>& // getter
 * ctx(shared_ptr<CRTPCTX>) -> void // setter
 *
 *
 * CRTP: curiously recurring template pattern, this should be equal to the child 
 * type which is implementing this object. IE: 
 * struct object : shared_context<object, object_context>
 * A CRTP type's methods typically are abstracted indirect calls to a CRTPCTX's 
 * methods.
 *
 * CRTPCTX: the CRTP's context type. This is the underlying (typically private, 
 * or hidden) type which contains the actual data and implementation for an 
 * object. 
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
        return this->m_context;
    }
   
    /**
     * @param ctx assign the context shared pointer
     */
    inline void ctx(std::shared_ptr<CRTPCTX> new_ctx) {
        this->m_context = new_ctx;
    }

public:
    virtual ~shared_context() { }

    inline void* get() {
        return ctx().get();
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
   
    /**
     * @brief assign the context
     * @return a reference to argument this object
     */
    inline CRTP& operator=(const CRTP& rhs) {
        this->ctx() = rhs.ctx();
        return *this;
    }

private:
    mutable std::shared_ptr<CRTPCTX> m_context;
};

}

#endif
