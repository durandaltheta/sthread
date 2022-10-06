//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis

#ifndef __SIMPLE_THREADING_MESSAGE__
#define __SIMPLE_THREADING_MESSAGE__

#include <memory>
#include <typeinfo>

namespace st { // simple thread

/**
 * @brief typedef representing the unqualified type of T
 */
template <typename T>
using base = typename std::decay<T>::type;

/**
 * The data type value is acquired by removing const and volatile 
 * qualifiers and then by acquiring the type_info::hash_type_code().
 *
 * @return an unsigned integer representing a data type.
 */
template <typename T>
static constexpr std::size_t type_code() {
    return typeid(base<T>).hash_code();
}

/**
 * The data type compiler name is acquired by removing const and volatile 
 * qualifiers and then by acquiring the type_info::name().
 *
 * @return an unsigned integer representing a data type.
 */
template <typename T>
static constexpr const char* type_name() {
    return typeid(base<T>).name();
}

namespace detail {

// @brief template typing assistance object
template <typename T> struct hint { };

//template assistance for unusable parent type
struct null_parent { };

// get a function's return type via SFINAE
// handle pre and post c++17 
#if __cplusplus >= 201703L
template <typename F, typename... As>
using function_return_type = typename std::invoke_result<base<F>,As...>::type;
#else 
template <typename F, typename... As>
using function_return_type = typename std::result_of<base<F>(As...)>::type;
#endif

/*
 * A utility struct which will store the current value of an argument reference,
 * and restore that value to said reference when this object goes out of scope.
 *
 * One advantage of using this over manual commands is that the destructor of 
 * this object will still trigger when an exception is raised.
 */
template <typename T>
struct hold_and_restore {
    hold_and_restore() = delete; // cannot create empty value
    hold_and_restore(const hold_and_restore&) = delete; // cannot copy
    hold_and_restore(hold_and_restore&& rhs) = delete; // cannot move
    inline hold_and_restore(T& t) : m_ref(t), m_old(t) { }
    inline ~hold_and_restore() { m_ref = m_old; }
    
    T& m_ref;
    T m_old;
};

}

//------------------------------------------------------------------------------
// DATA

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
    data() : m_data(data_pointer_t(nullptr, data::no_delete)), m_code(0) { }

    /// rvalue constructor
    data(data&& rhs) : m_data(std::move(rhs.m_data)), m_code(rhs.m_code) { }

    /// type `T` deduced constructor
    template <typename T>
    data(T&& t) : data(detail::hint<T>(), std::forward<T>(t)) { }

    virtual ~data() { }

    /**
     * @brief construct a data payload using explicit template typing instead of by deduction
     *
     * This function is the most flexible way to construct data, as it does not 
     * rely on being given a pre-constructed payload `T` and can invoke any 
     * arbitrary constructor for type `T` based on arguments `as`.
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
        m_code = rhs.m_code;
        m_data = std::move(rhs.m_data);
        return *this;
    }

    /// no lvalue constructor or copy
    data(const data& rhs) = delete;
    data& operator=(const data& rhs) = delete;

    /**
     * @return `true` if the object represents an allocated data payload, else `false`
     */
    inline operator bool() const {
        return m_data ? true : false;
    }

    /**
     * @brief determine at runtime whether the type erased data type code matches the templated type code.
     * @return true if the unqualified type of T matches the data type, else false
     */
    template <typename T>
    bool is() const {
        return m_code() == st::type_code<T>();
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
        return *((base<T>*)(m_data.get()));
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
        m_code(st::type_code<T>()),
        m_data(allocate<T>(std::forward<As>(as)...),data::deleter<T>)
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

    std::size_t m_code; // type code
    data_pointer_t m_data; // stored data 
};

//------------------------------------------------------------------------------
// SHARED CONTEXT

/**
 * @brief parent context interface
 */
struct context { 
    /**
     * @brief an object containing type information for runtime usage
     *
     * `PARENT` is typically a shared context type which wraps a context, while 
     * `SELF` is the descendant type which inherits `st::context`.
     */
    template <typename PARENT, typename SELF>
    struct type_info {
        const std::size_t parent_type_code = st::type_code<PARENT>();
        const std::size_t self_type_code = st::type_code<SELF>();
        const char* parent_name = st::type_name<PARENT>();
        const char* self_name = st::type_name<SELF>();
    };

    context() = delete;
    context(const context&) = delete;
    context(context&&) = delete;

    /**
     * @brief implementors are required to call this constructor
     * Type `PARENT` should be the shared context interface type that holds
     * a copy of this context.
     *
     * Type `SELF` should be the type that implements `st::context`
     */
    template <typename PARENT, typename SELF>
    context(context::type_info<PARENT,SELF> ti) : m_type_info(ti) { }

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
     * @return this `st::context`'s `st::context::type_info`
     */
    inline type_info get_type_info() const {
        return m_type_info;
    }

    /**
     * @brief convenience method to cast context to descendant type 
     * @return reference to descendant type
     */
    template <typename context>
    constexpr context& cast() {
        return *(dynamic_cast<context*>(this));
    }

private:
    const context::type_info m_type_info;
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
