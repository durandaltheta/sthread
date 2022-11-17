//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_DATA__
#define __SIMPLE_THREADING_DATA__

#include <memory>
#include <typeinfo>

#include "utility.hpp"

namespace st { // simple thread

/**
 * @brief type erased data container
 *
 * The purpose of this object is similar to c++17 `std::any` but is backwards 
 * compatible to c++11.
 *
 * `st::data` can represent types that are at least lvalue constructable.
 */
struct data {
    /// default constructor
    data() : m_type_code(0), m_data_ptr(data_pointer_t(nullptr, data::no_delete)){ }

    /// rvalue constructor
    data(data&& rhs) : m_type_code(rhs.m_type_code), m_data_ptr(std::move(rhs.m_data_ptr)) { }

    virtual ~data() { }

    /**
     * @brief construct a data payload using explicit template typing instead of by deduction
     *
     * @param as optional constructor parameters 
     * @return an allocated data object
     */
    template <typename T, typename... As>
    static data make(As&&... as) {
        return data(detail::hint<T>(), std::forward<As>(as)...);
    }
 
    /// rvalue copy
    inline data& operator=(data&& rhs) {
        m_type_code = rhs.m_type_code;
        m_data_ptr = std::move(rhs.m_data_ptr);
        return *this;
    }

    /// no lvalue constructor or copy
    data(const data& rhs) = delete;
    data& operator=(const data& rhs) = delete;

    /**
     * @return `true` if the object represents an allocated data payload, else `false`
     */
    inline operator bool() const {
        return m_data_ptr.data_pointer_t::operator bool();
    }

    /**
     * @return stored payload type code
     */
    inline std::size_t type_code() const {
        return m_type_code;
    }

    /**
     * @brief determine at runtime whether the type erased data type code matches the templated type code.
     * @return true if the unqualified type of T matches the data type, else false
     */
    template <typename T>
    bool is() const {
        return m_data_ptr && m_type_code == st::type_code<T>();
    }

    /**
     * @return the internal payload pointer
     */
    inline void* get() {
        return m_data_ptr.get();
    }

    /**
     * @brief cast message data payload to templated type reference 
     *
     * NOTE: this function is *NOT* type checked. A successful call to
     * `is<T>()` is required before casting to ensure type safety. It is 
     * typically better practice and generally safer to use `copy_to<T>()` or 
     * `move_to<T>()`, which include an internal type check.
     *
     * @return a reference of type T to the dereferenced void pointer payload
     */
    template <typename T>
    T& cast_to() {
        return *((base<T>*)(get()));
    }

    /**
     * @brief copy the data payload to argument t
     *
     * @param t reference to templated variable t to deep copy the payload data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool copy_to(T& t) {
        if(is<T>()) {
            t = cast_to<T>();
            return true;
        } else {
            return false;
        }
    }

    /**
     * @brief rvalue swap the data payload to argument t
     *
     * @param t reference to templated variable t to rvalue swap the payload data to
     * @return true on success, false on type mismatch
     */
    template <typename T>
    bool move_to(T& t) {
        if(is<T>()) {
            std::swap(t, cast_to<T>());
            return true;
        } else {
            return false;
        }
    }

private:
    typedef void(*deleter_t)(void*);
    typedef std::unique_ptr<void,deleter_t> data_pointer_t;
    typedef void*(*allocator_t)(void*);

    template <typename T, typename... As>
    data(detail::hint<T> h, As&&... as) :
        m_type_code(st::type_code<T>()),
        m_data_ptr(allocate<T>(std::forward<As>(as)...),data::deleter<T>)
    { }

    template <typename T, typename... As>
    static void* allocate(As&&... as) {
        return (void*)(new base<T>(std::forward<As>(as)...));
    }

    template <typename T>
    static void deleter(void* p) {
        delete (base<T>*)p;
    }

    static inline void no_delete(void* p) { }

    std::size_t m_type_code; // type code
    data_pointer_t m_data_ptr; // stored data 
};

}

#endif
