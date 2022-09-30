//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis
/**
 * @file
 * @brief Simple interprocess threading and messaging 
 *
 * The overall design of code in this library relies heavily on virtual
 * interfaces. Specifically, the code is broken down into two main categories:
 * 1. context interfaces 
 * 2. shared context interfaces 
 *
 * A "context interface" represents a c++ pure virtual interface that needs to 
 * be implemented by some inheriting context object. These descendant context 
 * objects are where most of the actual work is done in the code, but they share 
 * common inherited ancestors in order to allow the code to be abstracted at a 
 * higher level. Context objects are typically stored as allocated shared 
 * pointers held by a "shared context interface".
 *
 * A "shared context interface" represents a c++ CRTP (Curiously Recurring 
 * Template Pattern) templated virtual interface which inherits a shared pointer 
 * to a base `st::context` that is dynamically cast to a specific descendant 
 * context whenever it needs to actually do things. This combination of CRTP 
 * typing and context casting allows a *lot* of functionality to be inherited by 
 * objects instead of implemented manually. It also allows abstraction of those 
 * objects into union types like `st::sender` and `st::scheduler` that can 
 * represent a variety of different objects.
 *
 * Some pseudocode to illustrate the above:
 *
 * // MySharedImpl is basically just a wrapper to an `st::context` shared pointer
 * struct MySharedImpl : public st::shared_context<MySharedImpl> {
 *     // inherits methods from st::shared_context<MySharedImpl>, including:
 *     // - allocation check operator bool 
 *     // - assignment = operators
 *     // - comparison ==, !=, < operators
 *     // - std::shared_ptr<st::context>& context();
 *    
 *     // basic constructors must be manually implemented
 *     MySharedImpl(){}
 *     MySharedImpl(const MySharedImpl& rhs) { context() = rhs.context(); }
 *     MySharedImpl(MySharedImpl&& rhs) { context() = std::move(rhs.context()); }
 *
 *     // create a virtual destructor to ensure the right destructors are called
 *     virtual ~MySharedImpl(){ }
 *     
 *     // typically implement a static function which can allocate a MySharedImpl
 *     static inline MySharedImpl make() {
 *         MySharedImpl msi;
 *         // st::context::make<T>(...) returns an allocated 
 *         // std::shared_ptr<st::context> dynamically cast from descendant type 
 *         // `std::shared_ptr<T>`
 *         msi.context() = st::context::make<MySharedImpl::context>( ...constructor args ...);
 *         return msi;
 *     }
 *
 *     // ... rest of the implementation that is not inherited
 *
 * private: 
 *     // create a new st::context implementation named MySharedImpl::context
 *     struct context : public st::context {
 *         // implements st::context, specifically calling the type_info 
 *         // constructor which allows the type codes of MySharedImpl and 
 *         // MySharedImpl::context to be set 
 *         context(... constructor args ...) : 
 *             // constructor initialization list 
 *             // ...  
 *             // call to st::context constructor with necessary template types
 *             st::context(st::context::type_info<MySharedImpl,MySharedImpl::context>()) 
 *         {
 *             // ... finish construction
 *         }
 *
 *         // ... rest of the implementation
 *     };
 * };
 *
 * All of this work is done to limit the amount of bugs in the code by limiting 
 * the amount of unique code while keeping the ability to abstract objects, 
 * instead relying on the compiler to generate as much as possible. 
 */

#ifndef __SIMPLE_THREADING__
#define __SIMPLE_THREADING__

// utility includes
#include <memory>
#include <typeinfo>
#include <functional>

// asynchronous includes
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>

// container includes
#include <deque>
#include <vector>
#include <map>

// stream includes
#include <ostream>
#include <sstream>

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

/**
 * @brief template typing assistance object
 */
template <typename T> struct hint { };

/**
 * @brief template assistance for unusable parent type
 */
struct null_parent { };

namespace detail {

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

struct printer {
    const char* name() const = 0;
    const std::size_t type_code() const = 0;
    const void* address() const = 0;
};

/**
 * @brief writes a textual representation of any object implementing `st::printer` 
 *
 * prints in the format:
 * name:type_code@0xaddress
 *
 * @return a reference to the argument ostream reference
 */
template<class CharT, class Traits, class CRTP>
std::basic_ostream<CharT,Traits>&
operator<<(std::basic_ostream<CharT,Traits>& ost, st::printer& p) {
    std::stringstream ss;
    ss << p.name() << ":" << p.type_code() << "@" << std::hex << p.address();
    ost << ss.str();
    return ost;
}

/**
 * @brief type erased data container
 *
 * The purpose of this object is similar to c++17 `std::any` but is backwards 
 * compatible to c++11.
 */
struct data : public printer {
    inline data() : 
        m_type_name(""),
        m_type_code(0),
        m_data(data_pointer_t(nullptr, data::no_delete))
    { }

    // no lvalue constructor
    data(data& rhs) = delete;

    inline data(data&& rhs) : 
        m_type_name(rhs.type_name),
        m_type_code(std::move(rhs.m_type_code)) ,
        m_data(std::move(rhs.m_data))
    { }

    /**
     * @brief type `T` deduced constructor
     */
    template <typename T>
    data(T&& t) : data(hint<T>(), std::forward<T>(t)) { }

    inline virtual ~data() { }

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
        return data(hint<T>(), std::forward<As>(as)...);
    }

    /// cannot lvalue copy
    data& operator=(const data&) = delete;
 
    /// rvalue copy
    inline data& operator=(data&& rhs) {
        m_data = std::move(rhs.m_data);
        m_type_code = std::move(rhs.m_type_code);
        return *this;
    }

    /**
     * @return `true` if the object represents an allocated data payload, else `false`
     */
    inline operator bool() const {
        return m_data ? true : false;
    }

    /**
     * @return the stored compiler derived type name
     */
    inline const const char* name() const {
        return m_type_name;
    }

    /**
     * @return the stored compiler derived type code
     */
    inline const std::size_t type_code() const {
        return m_type_code;
    }

    /**
     * @return address of payload memory stored in this object
     */
    inline const void* address() const noexcept {
        return m_data.get();
    }
   
    /**
     * @brief determine at runtime whether the type erased data type code matches the templated type code.
     * @return true if the unqualified type of T matches the data type, else false
     */
    template <typename T>
    bool is() const {
        return m_type_code == st::type_code<T>();
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

    template <typename T, typename... As>
    data(hint<T> h, As&&... as) :
        m_type_name(st::type_name<T>()),
        m_type_code(st::type_code<T>()),
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

    const char* m_type_name;
    std::size_t m_type_code; // type code of stored data
    data_pointer_t m_data; // stored data
};

//------------------------------------------------------------------------------
// SHARED CONTEXT

/**
 * @brief parent context interface
 */
struct context : public printer { 
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

        /**
         * @return `true` if `T` matches the stored parent type code, else `false`
         */
        template <typename T>
        bool parent_is() const {
            return parent_type_code == st::type_code<T>();
        }

        /**
         * @return `true` if `T` matches the stored descendant type code, else `false`
         */
        template <typename T>
        bool is() const {
            return self_type_code == st::type_code<T>();
        }
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
    
    inline const char* name() const {
        return m_type_info.self_type_name;
    }

    inline const std::size_t type_code() const {
        return m_type_info.self_type_code;
    }

    inline const void* address() const {
        return this;
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
 * @brief crtp-templated interface to provide shared context api
 *
 * crtp: curiously recurring template pattern
 */
template <typename CRTP>
struct shared_context : public printer {
    shared_context() : m_code(st::type_code<CRTP>()) {}
    shared_context(const shared_context<CRTP>& rhs) { context() = rhs.context(); }
    shared_context(shared_context<CRTP>&& rhs) { context() = std::move(rhs.context());  }
    virtual ~shared_context() { }
   
    /**
     * WARNING: Blind manipulation of this value is dangerous.
     *
     * @return reference to shared context
     */
    inline std::shared_ptr<st::context>& context() const {
        return m_context;
    }

    inline const char* name() const {
        return context().get_type_info().parent_type_name;
    }

    const std::size_t type_code() const {
        return context().get_type_info().parent_type_code;
    }

    const void* address() const {
        return *this;
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

    /// lvalue shared_context<CRTP> assignment
    inline shared_context<CRTP>& operator=(const shared_context<CRTP>& rhs) {
        context() = rhs.context();
        return *(dynamic_cast<CRTP*>(this));
    }

    /// rvalue shared_context<CRTP> assignment
    inline shared_context<CRTP>& operator=(shared_context<CRTP>&& rhs) {
        context() = std::move(rhs.context());
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
     * @return `true` if argument CRTP represents this CRTP (or no CRTP), else `false`
     */
    inline bool operator==(CRTP&& rhs) const noexcept {
        return context() == rhs.context();
    }

    /**
     * @return `true` if `this` is less than `rhs`, else `false`
     */
    inline bool operator<(const CRTP& rhs) const noexcept {
        return context() < rhs.context();
    }

    /**
     * @return `true` if `this` is less than `rhs`, else `false`
     */
    inline bool operator<(CRTP&& rhs) const noexcept {
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

        std::size_t m_id;
        st::data m_data;
    };

    message(std::shared_ptr<st::context> ctx) { context()(std::move(ctx)); }
};

//------------------------------------------------------------------------------
// SENDERS 

namespace detail {

// get a function's return type via SFINAE
// handle pre and post c++17 
#if __cplusplus >= 201703L
template <typename F, typename... As>
using function_return_type = typename std::invoke_result<base<F>,As...>::type;
#else 
template <typename F, typename... As>
using function_return_type = typename std::result_of<base<F>(As...)>::type;
#endif

// SFINAE test for if an object has a `recv(st::message)` method
template<class>
struct sfinae_true : std::true_type{};

template<class T, class A0>
static auto test_recv(int)
  -> sfinae_true<decltype(std::declval<T>().recv(std::declval<A0>()))>;

template<class, class A0>
static auto test_recv(long) -> std::false_type;

template<class T, class Arg>
struct has_recv : decltype(test_recv<T, Arg>(0)){};

// SFINAE `recv(st::message)` method wrapper generators
template <typename OBJECT, std::true_type>
std::function<void(message&)> generate_recv_handler(OBJECT*) {
    return [](message& msg){};
}

template <typename OBJECT, std::false_type>
std::function<void(message&)> generate_recv_handler(OBJECT* obj) {
    return [obj](message& msg) mutable { obj->recv(msg); };
}

// SFINAE test for if an object has a `blocked()` method
template<class T>
static auto test_blocked(int)
  -> sfinae_true<decltype(std::declval<T>().blocked())>;

template<class>
static auto test_blocked(long) -> std::false_type;

template<class T>
struct has_blocked : decltype(test_blocked<T>(0)){};

// SFINAE `blocked()` method wrapper generators
template <typename OBJECT, std::true_type>
std::function<void()> generate_blocked_handler(OBJECT*) {
    return []{};
}

template <typename OBJECT, std::false_type>
std::function<void()> generate_blocked_handler(OBJECT* obj) {
    return [obj]() mutable { obj->block(); };
}

}

/**
 * @brief interface for objects that can have their execution terminated
 */
struct lifecycle {
    virtual ~lifecycle(){ }

    /**
     * @return `false` if object is unallocated or has been terminated, else `true`
     */
    virtual bool alive() const = 0;

    /**
     * @brief end operations on the object 
     * @param soft `true` to allow object to process remaining operations, else `false`
     */
    virtual void terminate(bool soft) = 0;

    /**
     * @brief default behavior for ending operations on the object
     */
    virtual inline void terminate() {
        terminate(true);
    }
};

/**
 * @brief parent of `st::context`s which can be sent messages
 */
struct sender_context : public context, public lifecycle {
    sender_context() = delete;
    sender_context(const context&) = delete;
    sender_context(context&&) = delete;

    template <typename PARENT, typename SELF>
    sender_context(context::type_info<PARENT,SELF> ti) : st::context(ti) { }

    virtual ~sender_context(){ 
        terminate();
    }

    /**
     * @brief send an `st::message` to the implementor 
     * @param msg `st::message` to send to the implementor
     * @return `true` on success, `false` if sender_context is terminated
     */
    virtual bool send(st::message msg) = 0;

    /**
     * @brief register a weak pointer to an `st::sender_context` as a listener 
     * @param snd any object implementing `st::sender_context` to send `st::message` back to 
     * @return `true` on success, `false` if sender_context is terminated
     */
    virtual bool register_listener(std::weak_ptr<st::sender_context> snd) = 0;
    
    /**
     * @return `true` if listener should be requeued to continue listening after successfully sending an `st::message`, else `false`
     */
    virtual inline bool requeue() const {
        return true;
    }
};

/**
 * @brief interface for objects which have shared `st::sender_context`s
 *
 * CRTP: Curiously Recurring Template Pattern
 */
template <CRTP>
struct shared_sender_context : public shared_context<CRTP>, public lifecycle {
    shared_sender_context(){ }
    shared_sender_context(const shared_sender_context<CRTP>& rhs) { context() = rhs.context(); }
    shared_sender_context(shared_sender_context<CRTP>&& rhs) { context() = std::move(rhs.context()); }
    virtual ~shared_sender_context(){ }

    /**
     * @return shared sender_context
     */
    inline std::shared_ptr<st::sender_context> sender_context() {
        return std::dynamic_pointer_cast<st::sender_context>(context());
    }

    inline bool alive() const {
        return context() && context()->cast<st::sender_context>().alive();
    }

    inline void terminate(bool soft) {
        return context()->cast<st::sender_context>().terminate(soft);
    }

    /**
     * @brief send an `st::message` with given parameters
     *
     * @param as arguments passed to `st::message::make()`
     * @return `true` on success, `false` if sender_context is terminated
     * */
    template <typename... As>
    bool send(As&&... as) {
        return context()->cast<st::sender_context>().send(
            st::message::make(std::forward<As>(as)...));
    }

    /**
     * @brief wrap user function and arguments then asynchronous execute them on a dedicated system thread and send the result of the operation to this `st::shared_sender_context<CRTP>`
     *
     * Internally calls `std::async` to asynchronously execute user function.
     * If function returns no value, then `st::message::data()` will be 
     * unallocated.
     *
     * @param resp_id id of message that will be sent back to the this `st::shared_sender_context<CRTP>` when `std::async` completes 
     * @param f function to execute on another system thread
     * @param as arguments for argument function
     */
    template <typename F, typename... As>
    void async(std::size_t resp_id, F&& f, As&&... as) {
        using isv = typename std::is_void<detail::function_return_type<F,As...>>;
        async_impl(std::integral_constant<bool,isv::value>(),
                   resp_id,
                   std::forward<F>(f),
                   std::forward<As>(as)...);
    }

    /**
     * @brief register a weak pointer of a `st::sender_context` as a listener to this object 
     *
     * NOTE: `std::weak_ptr<T>` can be created directly from a `st::shared_ptr<T>`. 
     * IE, the user can pass an `std::shared_ptr<st::context>` to this function.
     *
     * @param snd a shared_ptr to an object implementing `st::sender_context` to send `st::message` back to 
     * @return `true` on success, `false` if sender_context is terminated
     */
    inline bool register_listener(std::weak_ptr<st::sender_context> snd) {
        return context()->cast<st::sender_context>().register_listener(std::move(snd));
    }
  
    /**
     * @brief register an `st::shared_sender_context` as a listener to this object 
     *
     * WARNING: An object should never register itself as a listener to itself,
     * (even implicitly) as this can create an endless loop.
     *
     * @param snd an object implementing `st::shared_sender_context` to send `st::message` back to 
     * @return `true` on success, `false` if sender_context is terminated
     */
    template <typename RHS_CRTP>
    inline bool register_listener(shared_sender_context<RHS_CRTP>& snd) {
        return register_listener(snd.context()->cast<st::sender_context>());
    }

private:
    template <typename F, typename... As>
    void async_impl(std::true_type, std::size_t resp_id, F&& f, As&&... as) {
        shared_sender_context<CRTP> self = *this;

        // launch a thread and schedule the call
        std::async([=]() mutable { // capture a copy of the shared send context
             f(std::forward<As>(as)...);
             self.send(resp_id);
        }); 
    }
    
    template <typename F, typename... As>
    void async_impl(std::false_type, std::size_t resp_id, F&& f, As&&... as) {
        shared_sender_context<CRTP> self = *this;

        // launch a thread and schedule the call
        std::async([=]() mutable { // capture a copy of the shared send context
             auto result = f(std::forward<As>(as)...);
             self.send(resp_id, result);
        }); 
    }
}

/**
 * @brief conversion interface to convert abstracted shared contexts back to
 * specific shared contexts.
 */
template <typename CRTP>
struct abstracted_shared_context {
    /// lvalue copy
    template <typename RHS_CRTP>
    CRTP& operator=(const shared_sender_context<RHS_CRTP>& rhs) {
        context() = rhs.context();
        return *this;
    }

    /// rvalue copy
    template <typename RHS_CRTP>
    CRTP& operator=(shared_sender_context<RHS_CRTP>&& rhs) {
        context() = std::move(rhs.context());
        return *this;
    }

    /**
     * Useful API when used in determining the underlying type of an abstraction 
     * object like `st::sender` or `st::scheduler`
     *
     * @return `true` if the underlying context represents a context of type `T`, else `false`
     */
    template <typename T>
    bool is() const {
        return context()->get_type_info().parent_is<T>();
    }

    /**
     * Implicit conversion to any object which supports methods `bool is<T>()` 
     * and `std::shared_ptr<st::context>& context()`. 
     *
     * @return converted `T` if `is<T>()` returns true, else an empty `T`
     */
    inline operator T() {
        T t;
        if(is<T>()) {
            t.context() = context();
        } 
        return t;
    }
};

/**
 * @brief wrapper class for implementors of `st::shared_sender_context<CRTP>` 
 * and are therefore capable of being sent `st::message`s.
 *
 * Is primarily useful as for abstracting objects which can have messages sent 
 * to them.
 */
struct sender : public shared_sender_context<sender> , 
                public abstracted_shared_context<sender> {
    sender() { }
    virtual ~sender(){ }

    /// lvalue constructor
    template <typename CRTP>
    sender(const shared_sender_context<CRTP>& rhs) { context() = rhs.context(); }

    /// rvalue constructor
    template <typename CRTP>
    sender(shared_sender_context<CRTP>&& rhs) { context() = std::move(rhs.context()); }
};

/**
 * @brief object capable of sending a payload back to an `st::sender`
 *
 * This object provides a simple, lightweight way to send messages back to a 
 * requestor while abstracting the message passing details. This object can be 
 * the payload `st::data` of an `st::message`.
 */
struct reply : public shared_context<reply> {
    reply() : m_id(0) { }
    reply(const reply& rhs) { context() = rhs.context(); }
    reply(reply&& rhs) { context() = std::move(rhs.context()); }
    virtual ~reply(){ }

    /**
     * @brief main constructor 
     * @param snd any object implementing `st::shared_sender_context` to send `st::message` back to 
     * @param id unsigned int id of `st::message` sent back over `ch`
     */
    static inline reply make(sender snd, std::size_t id) { 
        reply r;
        r.context() = st::context::make<reply::context>(std::move(snd), id)
        return r;
    }

    /**
     * @brief send an `st::message` back to some abstracted `st::sender`
     * @param t `st::message` payload data 
     * @return `true` if internal `st::channel::send(...)` succeeds, else `false`
     */
    template <typename T>
    bool send(T&& t) {
        return context()->cast<reply::context>().m_snd.send(m_id, std::forward<T>(t));
    }

private:
    struct context : public st::context {
        context() :
            st::context(st::context::type_info<reply, reply::context>())
        { }

        virtual ~context(){ }
        sender m_snd;
        std::size_t m_id;
    };
};

/**
 * @brief Interthread message passing queue
 *
 * The internal mechanism used by this library to communicate between system 
 * threads. This is the mechanism that other implementors of 
 * `st::shared_sender_context<CRTP>` typically use internally.
 *
 * Listeners registered to this object with `register_listener(...)` will
 * compete for `st::message`s sent over it.
 *
 * All methods in this object are threadsafe.
 */
struct channel : public shared_sender_context<channel> {
    inline channel(){}
    inline channel(const channel& rhs) { context() = rhs.context(); }
    inline channel(channel&& rhs) { context() = std::move(rhs.context()); }
    inline virtual ~channel() { }

    /**
     * @brief Construct an allocated channel
     * @return the allocated channel
     */
    static inline channel make() {
        channel ch;
        ch.context() = st::context::make<channel::context>();
        return ch;
    }

    /** 
     * @return count of messages in the queue
     */
    inline std::size_t queued() const {
        return context()->cast<message::context>().queued();
    }

    /**
     * @return count of `st::thread`s blocked on `recv()` or are listening to this `st::channel`
     */
    inline std::size_t blocked_receivers() const {
        return context()->cast<message::context>().blocked_receivers();
    }

    /**
     * @brief optionally enqueue the argument message and receive a message over the channel
     *
     * This is a blocking operation that will not complete until there is a 
     * value in the message queue, after which the argument message reference 
     * will be overwritten by the front of the queue.
     *
     * A successful call to `recv()` will remove a message queued by `send()` 
     * from the internal channel message queue.
     *
     * Multiple simultaneous `recv()` calls will be served in the order they 
     * were called.
     *
     * @param msg interprocess message object reference to contain the received message 
     * @return `true` on success, `false` if channel is terminated
     */
    inline bool recv(message& msg) {
        return context()->cast<message::context>().recv(msg);
    }

private:
    struct blocker : public st::sender_context {
        struct data {
            data(message* m) : msg(m) { }

            inline void wait(std::unique_lock<std::mutex>& lk) {
                do {
                    cv.wait(lk);
                } while(!flag);
            }

            inline void signal() {
                flag = true;
                cv.notify_one(); 
            }

            inline void signal(message& m) {
                *msg = std::move(m);
                signal();
            }

            bool flag = false;
            std::condition_variable cv;
            message* msg;
        };

        blocker(data* d) : 
            m_data(d), 
            st::sender_context(
                st::context::type_info<null_parent,st::channel::blocker>())
        { }

        ~blocker(){ m_data->signal(); }
    
        inline bool alive() const {
            return !flag;
        }

        inline void terminate(bool soft) {
            m_data->signal();
        }

        inline bool send(message msg){ 
            m_data->signal(msg); 
            return true;
        }
        
        // do nothing
        inline bool register_listener(std::weak_ptr<st::sender_context> snd) { } 

        // override requeue
        inline bool requeue() const {
            return false;
        }
    
        data* m_data;
    };

    struct context : public st::sender_context {
        context() : 
            m_closed(false),
            st::sender_context(st::context::type_info<channel, channel::context>())
        { }

        virtual ~context(){ }
    
        inline bool register_listener(std::weak_ptr<st::sender_context> snd) {
            std::lock_guard<std::mutex> lk(m_mtx);
            if(m_closed) { 
                return false;
            } else {
                m_listeners.push_back(std::move(snd));
                return true;
            }
        }

        inline bool alive() const { 
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_closed;
        }
        
        inline std::size_t queued() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_msg_q.size();
        }

        inline std::size_t blocked_receivers() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_listeners.size();
        }

        void terminate(bool soft);
        void handle_queued_messages(std::unique_lock<std::mutex>& lk);
        bool send(message msg);
        bool recv(message& msg);

        bool m_closed;
        mutable std::mutex m_mtx;
        std::deque<message> m_msg_q;
        std::deque<std::weak_ptr<st::sender_context>> m_listeners;
    };
};

//------------------------------------------------------------------------------
// SCHEDULERS

/**
 * @brief parent of `st::context`s which can schedule code for execution
 */
struct scheduler_context : public sender_context {
    scheduler_context() = delete;
    scheduler_context(const context&) = delete;
    scheduler_context(context&&) = delete;

    template <typename PARENT, typename SELF>
    scheduler_context(context::type_info<PARENT,SELF> ti) : st::sender_context(ti) { }

    /**
     * @brief schedule a generic task for execution 
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being terminated
     */
    virtual bool schedule(std::function<void()> f) = 0;
};

/**
 * @brief interface for objects which have shared `st::scheduler_context`s and 
 * are therefore capable of scheduling arbitrary code for execution.
 *
 * CRTP: Curiously Recurring Template Pattern
 */
template <typename CRTP>
struct shared_scheduler_context : public shared_sender_context<CRTP> {
    /**
     * @brief schedule a generic task for execution 
     *
     * Allows for implicit conversions to `std::function<void()>`, if possible.
     *
     * @param f std::function to execute on target sender
     * @return `true` on success, `false` on failure due to object being terminated
     */
    inline bool schedule(std::function<void()> f) {
        return context()->cast<scheduler_context>().schedule(std::move(f));
    }

    /**
     * @brief wrap user function and arguments then schedule as a generic task for execution
     *
     * @param f function to execute on target sender 
     * @param as arguments for argument function
     * @return `true` on success, `false` on failure due to object being terminated
     */
    template <typename F, typename... As>
    bool schedule(F&& f, As&&... as) {
        return schedule([=]() mutable { f(std::forward<As>(as)...); });
    }
};

/**
 * @brief wrapper class for implementors of `st::shared_scheduler_context<CRTP>` 
 *
 * Is primarily useful as for abstracting objects which can have messages sent 
 * to them.
 */
struct scheduler : public shared_scheduler_context<scheduler>, 
                   public abstracted_shared_context<scheduler> {
    scheduler() { }
    virtual ~scheduler() { }

    /// lvalue constructor
    template <typename CRTP>
    scheduler(const shared_scheduler_context<CRTP>& rhs) { context() = rhs.context(); }

    /// rvalue constructor
    template <typename CRTP>
    scheduler(shared_scheduler_context<CRTP>&& rhs) { context() = std::move(rhs.context()); }
};

/**
 * @brief a thread object potentially managing its own system thread
 *
 * This library allows the user to create `st::thread` instances with user 
 * defined objects as a template argument with a call to static function:
 * `st::thread::make<OBJECT>(...)`
 *
 * Type `OBJECT` should be a class implementing the method 
 * `void recv(st::message msg)`:
 * ```
 * struct MyClass {
 *     void recv(st::message m);
 * };
 * ```
 *
 * *Technically*, the implementation of `recv()` is optional, as this library 
 * will generate a noop handler if none is defined by the user's `OBJECT` 
 * definition.
 *
 * Note: `st::threads`s automatically throw out any unallocated messages 
 * received over their internal `st::channel` instead of passing them to the 
 * `OBJECT`'s `recv()` implementation.
 *
 * Type `OBJECT` can also optionally create a function `void blocked()`, which 
 * will be triggered whenever there are no messages to process:
 * ```
 * struct MyClass {
 *     void recv(st::message m);
 *     void blocked();
 * };
 * ```
 *
 * Note: `void OBJECT::block()` implementations should *VERY* carefullly
 * consider what you use this function for. For instance, it is generally a 
 * potential cause of deadlock to call a blocking function from with `block()`, 
 * the same as anywhere else. A potential exception to this is when the user is 
 * writing a single system thread process and executing their `st::thread` via 
 * `st::thread::run(...)`, whereupon it is safe to block on an interprocess 
 * message receiving function.
 *
 * All methods in this object are threadsafe.
 */
struct thread : public shared_scheduler_context<st::thread> {
    inline thread(){}
    inline thread(const st::thread& rhs) { context() = rhs.context(); }
    inline thread(st::thread&& rhs) { context() = std::move(rhs.context()); }

    virtual ~thread() {
        // Explicitly terminate `st::thread` channel because a system thread 
        // holds a copy of this `st::thread` which keeps the channel alive even 
        // though the `st::thread` is no longer reachable.
        //
        // Because this logic only triggers on `st::thread` destructor, we are 
        // fine to destroy excess `st::thread::context`s during initialization 
        // until `st::thread::make<...>(...)` returns.
        if(context() && context().use_count() <= 2) {
            terminate();
        }
    }

    /**
     * @brief Empty `OBJECT` which only processes messages sent via `schedule()` ignoring all other messages.
     */
    struct processor { };

    /**
     * @brief statically construct a new system thread running user `OBJECT` associated with returned `st::thread`
     *
     * Because `st::thread`s allocation constructors are private, this function 
     * must be called to generate an allocated `st::thread`. This mechanism 
     * ensures that whenever an `st::thread` is constructed its `OBJECT` will be 
     * immediately running and capable of receiving `st::message`s.
     *
     * `st::thread`'s `OBJECT` will be allocated on the scheduled system thread, 
     * not the calling system thread. This allows usage of `thread_local` data 
     * where necessary.
     *
     * The user is responsible for holding a copy of the returned `st::thread`
     * to ensure the system thread does not shutdown and user `OBJECT` is kept 
     * in memory.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=processor, typename... As>
    static st::thread make(As&&... as) {
        st::thread thd;
        thd.context() = st::context::make<st::thread::context>();
        thd.context()->cast<st::thread::context>().launch_thread<OBJECT>(std::forward<As>(as)...);
        return thd;
    }

    /**
     * @brief similar to `st::thread::make(...)` except that the `st::thread` is associated with and launched on the calling system thread
     *
     * This function provides a way to launch the `st::thread` environment 
     * without launching additional system threads, making a single system 
     * thread process possible using this library. A common use for this 
     * function is for running an `st::thread` environment on a system thread 
     * launched by other code.
     *
     * WARNING: This function blocks the calling system thread until the 
     * internally launched `st::thread` has `st::thread::terminate(...)` called. 
     * The `thd` parameter reference `st::thread` will be assigned the allocated 
     * context constructed by this function, allowing code to call 
     * `st::thread::send(...)`, `st::thread::terminate(...)`, etc on `thd`. 
     * Typically, `thd` will be a global variable or a member of some managing 
     * global object.
     *
     * To do blocking receives of interprocess messages when executing this 
     * function in a single thread process the user can write a custom
     * `OBJECT::blocked()` implementation which does a blocking inter-process 
     * receive. 
     *
     * However, if a user decides to utilize inter-process blocking receives in 
     * a multi-thread process within a call to `OBJECT::blocked()`, they must 
     * send inter-process messages to the thread blocking on an inter-process 
     * receive in order to unblock it. Typically, its both simpler and safer to
     * just have a dedicated system thread block on inter-process receives
     * without calling `st::thread::run(...)` on that thread.
     *
     * @param thd `st::thread` object to contain allocated context
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=processor, typename... As>
    static void run(st::thread& thd, As&&... as) {
        thd.context() = st::context::make<st::thread::context>();
        thd.context()->cast<st::thread::context>().launch_local<OBJECT>(std::forward<As>(as)...);
    }

    /**
     * @return the `std::thread::id` of the system thread this `st::thread` is running on
     */
    inline std::thread::id get_id() const {
        return context() ? context()->cast<message::context>().get_thread_id() : std::thread::id();
    }

    /**
     * This static function is intended to be called from within an `OBJECT` 
     * running in an `st::thread`.
     *
     * @return a copy of the `st::thread` currently running on the calling thread, if none is running will return an unallocated `st::thread`
     */
    static inline st::thread self() {
        return st::thread(context::tl_self().lock());
    }

private:
    /*
     * Generic function wrapper for executing arbitrary code
     *
     * Used to convert and wrap any code to a generically executable type. Is 
     * a new definition instead of a typedef so that it can be distinguished by 
     * receiving code.
     */
    struct task : public std::function<void()> { 
        template <typename... As>
        task(As&&... as) : std::function<void()>(std::forward<As>(as)...) { }
    };

    struct context : public st::scheduler_context, 
                     public std::enable_shared_from_this<st::thread::context> {
        context() : 
            m_shutdown(false), 
            m_ch(channel::make()),
            st::scheduler_context(st::context::type_info<st::thread, st::thread::context>())
        { }

        virtual ~context() { shutdown(false); }

        // thread local data
        static std::weak_ptr<context>& tl_self();


        // looping recv function executed by a root thread
        void thread_loop(const std::function<void(message&)>& mgs_hdl
                         const std::function<void()& blk_hdl);

        /*
         * Finish initializing the `st::thread` by allocating the `OBJECT` object 
         * and related handlers and then start the thread message receive loop. 
         *
         * Should be called on the scheduled parent `st::thread`.
         */
        template <typename OBJECT, typename... As>
        void init_loop(As&&... as) {
            data d = data::make<OBJECT>(std::forward<As>(as)...);
            
            // cast once to skip some processing indirection during msg handling
            OBJECT* obj = &(d->cast_to<OBJECT>());

            thread_loop(
                detail::generate_recv_handler<OBJECT,has_recv<OBJECT,message>()>(obj),
                detail::generate_blocked_handler<OBJECT,has_recv<OBJECT>()>(obj));
        }

        // construct a thread running on a dedicated system thread
        template <typename OBJECT, typename... As>
        void launch_thread(As&&... as) {
            std::shared_ptr<context> self = shared_from_this();
            m_self = self;

            std::thread thd([&,self]{ // keep a copy of this context in existence
                init_loop<OBJECT>(std::forward<As>(as)...);
            });

            m_thread_id = thd.get_id();
            thd.detach();
        }
        
        template <typename OBJECT, typename... As>
        void launch_local(As&&... as) {
            m_self = shared_from_this();
            m_thread_id = std::thread::this_thread::get_id();
            init_loop<OBJECT>(std::forward<As>(as)...);
        }

        inline bool alive() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return !m_shutdown;
        }
    
        void terminate(bool soft);

        inline bool send(message msg) {
            return m_ch.send(std::move(msg));
        }
        
        inline bool register_listener(std::weak_ptr<st::sender_context> snd) {
            m_ch.register_listener(std::move(snd));
        }
    
        inline bool schedule(std::function<void()> f) {
            return m_ch.send(0, task(std::move(f)));
        }

        inline std::thread::id get_thread_id() const {
            std::lock_guard<std::mutex> lk(m_mtx);
            return m_thread_id;
        }

        mutable std::mutex m_mtx;
        bool m_shutdown;
        channel m_ch; // internal thread channel
        std::weak_ptr<st::thread::context> m_self; // weak pointer to self
        std::thread::id m_thread_id; // thread id the user object is executing on
    };
};

/**
 * @brief a coroutine intended to run on a parent `st::thread`
 *
 * According to wikipedia: Coroutines are computer program components that 
 * generalize subroutines for non-preemptive multitasking, by allowing 
 * execution to be suspended and resumed.
 *
 * The general advantages of using coroutines compared to system thread`s:
 * - Changing which coroutine is running by suspending its execution is 
 *   exponentially faster than changing which system thread is running. IE, the 
 *   more concurrent operations need to occur, the more efficient coroutines 
 *   generally become in comparison to threads.
 * - Faster context switching results in faster communication between code, 
 *   particularly between coroutines running on the same system thread.
 * - Coroutines take less memory than threads 
 * - The number of coroutines is not limited by the operating system
 * - Coroutines do not require system level calls to create
 *
 * The general disadvantages of using coroutines:
 * - Coroutines are expected to use only non-blocking operations to avoid
 *   blocking their parent thread.
 * - Coroutines cannot, by themselves, leverage multiple processor cores for 
 *   increased processing throughput. Coroutines must be run on multiple system 
 *   threads (the count of which should match the count of hardware CPUs for
 *   maximum CPU throughput) to leverage multiple processor cores.
 *
 * While more powerful coroutines are possible in computing, particularly with 
 * assembler level support which allocates stacks for coroutines, the best that 
 * can be accomplished at present in C++ is stackless coroutines. This means 
 * that code cannot be *arbitrarily* suspended and resumed at will (although 
 * this can be simulated with some complicated `switch` based hacks, which add  
 * significant complexity, and come with their own limitations. Further support 
 * for this kind of coroutine is provided in C++20 and onwards). 
 *
 * `st::fiber`s scheduled on a parent `st::thread` will take turns scheduling 
 * themselves so that no `st::fiber` (or the parent `st::thread`) starves. All 
 * calls to an `st::fiber`'s `st::fiber::schedule(...)` will internally call the 
 * parent `st::thread`'s `st::thread::schedule(...)`.
 *
 * This library allows the user to create `st::fiber` instances with user 
 * defined objects as a template argument in similar fashion to an `st::thread`:
 * `st::fiber::make<OBJECT>(st::thread parent, ...)`. 
 *
 * All methods in this object are threadsafe.
 */
struct fiber : public shared_scheduler_context<fiber> {
    inline fiber(){}
    inline fiber(const fiber& rhs) { context() = rhs.context(); }
    inline fiber(fiber&& rhs) { context() = std::move(rhs.context()); }
    virtual ~fiber() { }

    /**
     * @brief statically construct a new `st::fiber` running user `OBJECT`
     *
     * `st::fiber::make<OBJECT>(...)` functions identically in regard to user 
     * `OBJECT`s as `st::thread::make<OBJECT>(...)`.
     *
     * Because `st::fiber`s allocation constructors are private, this function 
     * must be called to generate an allocated `st::fiber`. This mechanism 
     * ensures that whenever an `st::fiber` is constructed its `OBJECT` will be 
     * immediately running and capable of receiving `st::message`s as long as
     * the parent `st::thread` is running.
     *
     * `st::fiber`'s `OBJECT` will be allocated on the scheduled system thread, 
     * not the calling system thread. This allows usage of `thread_local` data 
     * where necessary.
     *
     * The user is responsible for holding a copy of the returned `st::fiber`
     * to ensure the user `OBJECT` is kept in memory.
     *
     * @param as optional arguments to the constructor of type `OBJECT`
     */
    template <typename OBJECT=st::thread::processor, typename... As>
    static fiber make(st::thread parent, As&&... as) {
        fiber f;
        f.context() = st::context::make<fiber::context>(std::move(parent));
        f.context()->cast<fiber::context>().launch_fiber<OBJECT>(std::forward<As>(as)...);
        return f;
    }

    /**
     * This static function is intended to be called from within an `OBJECT` 
     * running in an `st::fiber`.
     *
     * @return a copy of the `st::fiber` currently running on the calling thread, if none is running will return an unallocated `st::fiber`
     */
    static inline fiber self() {
        return fiber(context::tl_self().lock());
    }

    /**
     * @return the `st::fiber`'s parent `st::thread`
     */
    inline st::thread parent() const {
        return context()->cast<fiber::context>().parent();
    }

private:
    // private assistance sender to wakeup `st::fiber` 
    struct listener : public st::sender_context {
        context(std::weak_ptr<fiber::context> self, st::thread parent) : 
            m_ctx(std::move(self)),
            m_parent(std::move(parent)),
            st::sender_context(st::context::type_info<st::null_parent,st::fiber::listener>())
        { }
    
        inline bool alive() const {
            return m_fib_ctx.lock();
        }

        inline void terminate(bool soft) {
            m_fib_ctx = std::shared_ptr<st::sender_context>();
        }

        bool send(st::message msg);
        
        // do nothing
        inline bool register_listener(std::weak_ptr<st::sender_context> snd) { } 

        std::weak_ptr<fiber::context> m_fib_ctx;
        st::thread m_parent;
    };

    struct context : public st::scheduler_context, 
                     public std::enable_shared_from_this<st::thread::context> {
        context(st::thread thd) : 
            m_alive_guard(true),
            m_thd(std::move(thd)),
            m_ch(st::channel::make()),
            st::scheduler_context(st::context::type_info<st::fiber, st::fiber::context>())
        { }

        // thread local data
        static std::weak_ptr<context>& tl_self();

        template <typename OBJECT, typename... As>
        inline void launch_fiber(As&&... as) {
            // properly set the thread_local self `st::thread` before `OBJECT` construction
            st::shared_ptr<fiber::context> self = shared_from_this();
            m_self = self;

            if(!m_parent.schedule([self,&]{
                detail::hold_and_restore<std::weak_ptr<context>> self_har(tl_self());
                tl_self() = m_self.lock();

                // construct the `OBJECT` 
                m_data = std::shared_ptr<data>(new data(
                    data::make<OBJECT>(std::forward<As>(as)...)));

                // cast once to skip some processing indirection during msg handling
                OBJECT* obj = &(m_data->cast_to<OBJECT>());

                // generate a message handler wrapper for `OBJECT`
                m_msg_hdl = detail::generate_recv_handler<OBJECT,has_recv<OBJECT,message>()>(obj);
                m_blk_hdl = detail::generate_blocked_handler<OBJECT,has_recv<OBJECT>()>(obj);

                // register a listener to wakeup fiber 
                auto snd = st::context::make<fiber::listener>(m_self, m_thd);
                register_listener(snd->cast<st::sender_context>());
            })) {
                m_ch.terminate();
            }
        }
        
        inline bool register_listener(std::weak_ptr<st::sender_context> snd) {
            m_ch.register_listener(std::move(snd));
        }
    
        inline bool schedule(std::function<void()> f) {
            return m_parent.schedule(std::move(f));
        }

        inline bool alive() const {
            return m_ch.alive();
        }

        void terminate(bool soft);

        inline bool send(st::message msg) {
            m_ch.send(std::move(msg));
        }

        void process_message();

        inline bool wakeup(std::shared_ptr<fiber::context>& self) {
            return m_parent.schedule([self]() mutable { 
                self->process_message(); 
            });
        }
        
        inline st::thread parent() const {
            return m_parent;
        }

        mutable std::mutex m_mtx;
        bool m_alive_guard;
        st::thread m_parent; // written once before message processing
        st::channel m_ch; // written once before message processing
        std::weak_ptr<fiber::context> m_self; // written once before message processing
        std::shared_ptr<data> m_data;
        std::function<void(message&)> m_msg_hdl; // written once before message processing
        std::function<void()> m_blk_hdl; // written once before message processing
        std::deque<st::message> m_received_msgs;
    };
};

}

#endif
