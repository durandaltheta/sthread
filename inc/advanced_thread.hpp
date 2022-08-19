//SPDX-License-Identifier: LicenseRef-Apache-License-2.0
//Author: Blayne Dennis
/**
 * @file
 * @brief Advanced interprocess threading and messaging
 */

#ifndef __SIMPLE_THREADING_ADVANCED_USAGE__
#define __SIMPLE_THREADING_ADVANCED_USAGE__

#include <memory>
#include <mutex>
#include <unordered_map>
#include <map>
#include <typeinfo>
#include <functional>

#include "simple_thread.hpp"

namespace at { // advanced thread

//******************************************************************************
// ADVANCED CONCURRENCY

/**
 * @brief a class managing one or more identical worker threads 
 *
 * The `at::executor` object implements a constant time algorithm which attempts 
 * to efficiently distribute tasks among worker threads.
 *
 * The `at::executor` object is especially useful for scheduling operations 
 * which benefit from high CPU throughput and are not reliant on the specific 
 * thread upon which they run. 
 *
 * Highest CPU throughput is typically reached by an executor whose worker count 
 * matches the CPU core count of the executing machine. This optimal number of 
 * cores may be discoverable by the return value of a call to 
 * `at::executor::default_worker_count()`.
 *
 * Because `executor` manages a limited number of workers, any message whose 
 * processing blocks a worker indefinitely can cause all sorts of bad effects, 
 * including deadlock. 
 */
struct executor : public st::type_aware, 
                  protected st::self_aware<executor>,
                  public st::lifecycle_aware,
                  protected st::processor {
    /**
     @brief attempt to retrieve a sane executor worker count for maximum CPU throughput

     The standard does not enforce the return value of 
     `std::thread::hardware_concurrency()`, but it typically represents the 
     number of cores a computer has, which is also generally the ideal number of 
     threads to allocate for maximum processing throughput.

     @return maximumly efficient count of worker threads for CPU throughput
     */
    static inline std::size_t default_worker_count() {
        return std::thread::hardware_concurrency() 
               ? std::thread::hardware_concurrency() 
               : 1;
    }

    /**
     * @brief allocate an executor to manage worker threads
     *
     * The template type FUNCTOR is the same as used in
     * `st::worker::make<FUNCTOR>(constructor args...)`, allowing the user to 
     * design and specify any FUNCTOR they please. However, in many cases the 
     * user can simply use `at::processor` as the FUNCTOR type, as it is 
     * designed for processing generic operations. Doing so will also allow the 
     * user to schedule arbitary `at::cotask` coroutines on the `at::executor`.
     *
     * An intelligent value for worker_count can typically be retrieved from 
     * `default_worker_count()` if maximum CPU throughput is desired.
     *
     * @param worker_count the number of threads this executor should manage
     * @param as constructor arguments for type FUNCTOR
     * @return allocated running worker thread shared_ptr
     */
    template <typename FUNCTOR=st::thread::worker, 
              std::size_t QUEUE_MAX_SIZE=SIMPLE_THREAD_CHANNEL_DEFAULT_MAX_QUEUE_SIZE, 
              typename... As>
    static st::sptr<executor> make(std::size_t worker_count, As&&... as) {
        return st::sptr<executor>(new executor(
            st::type_code<FUNCTOR>(),
            worker_count,
            [=]() mutable -> st::sptr<worker> {
                return worker::make<FUNCTOR, QUEUE_MAX_SIZE>(std::forward<As>(as)...); 
            }));
    }
    
    /**
     * @return true if executor's worker threads are running, else false
     */
    inline bool running() const {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_workers.size() ? true : false;
    }

    /** 
     * @brief Shutdown the worker threads
     *
     * @param process_remaining_messages if true allow recv() to succeed until queue empty
     */
    inline void shutdown(bool process_remaining_messages) {
        std::lock_guard<std::mutex> lk(m_mtx);
        for(auto& w : m_workers) {
            w->shutdown(process_remaining_messages);
        }

        m_workers.clear();
    }

    /**
     * @return the count of worker threads managed by this executor
     */
    inline std::size_t worker_count() const {
        return m_worker_count;
    }


protected:
    inline st::result internal_send(st::sptr<message>&& msg, sender::op o) const {
        st::result r{ result::eStatus::closed };

        std::lock_guard<std::mutex> lk(m_mtx);
        if(m_workers.size() ? true : false) {
            auto wkr = select_worker();

            switch(o) {
                case sender::op::blocking_send:
                    r = wkr->send(std::move(msg));
                    break;
                case sender::op::try_send:
                    r = wkr->try_send(std::move(msg));
                    break;
                case sender::op::force_send:
                default:
                    r = wkr->force_send(std::move(msg));
                    break;
            }
        } 

        return r;
    }

private:
    typedef std::vector<st::sptr<worker>> worker_vector_t;
    typedef worker_vector_t::iterator worker_iter_t;

    executor(const std::size_t type_code;
             const std::size_t worker_count, 
             std::function<st::sptr<worker>()> make_worker) :
        m_worker_count(worker_count ? worker_count : 1), // enforce 1 thread 
        m_workers(m_worker_count),
        m_cur_it(m_workers.begin()),
        type_aware(type_code) {
        for(auto& w : m_workers) {
            w = make_worker();
        }
    }

    executor() = delete;
    executor(const executor& rhs) = delete;
    executor(executor&& rhs) = delete;

    // selected a worker to sechedule task on
    inline worker* select_worker() {
        if(m_worker_count > 1) {
            auto& prev_worker = *(m_cur_it);
            ++m_cur_it;

            // if at the end of the vector return the first entry
            if(m_cur_it == m_workers.end()) {
                m_cur_it = m_workers.begin();
            } 

            auto& cur_worker = *(m_cur_it);

            if(prev_worker->get_weight() < cur_worker->get_weight()) {
                return prev_worker.get();
            } else {
                return cur_worker.get();
            }
        } else {
            return m_cur_it->get();
        }
    }

    mutable std::mutex m_mtx;
    const std::size_t m_worker_count;
    mutable worker_vector_t m_workers;
    mutable worker_iter_t m_cur_it;
};

//******************************************************************************
// STATE MANAGEMENT

/**
 * A fairly simple finite state machine mechanism (FSM). FSMs are
 * somewhat infamous for being difficult to parse, too unwieldy, or otherwise 
 * opaque. As with everything else in this library, the aim of this object's 
 * design is to make sure the necessary features are kept simple without overly 
 * limiting the user. Therefore some care has been attempted to mitigate those 
 * concerns.
 *
 * The toplevel class for this feature is actually the inheritable `state` 
 * object. The user should implement classes which publicly inherit `at::state`, 
 * overriding its `enter()` and `exit()` methods as desired. A static 
 * `state::make()` function is included as convenience for the user so they do 
 * not have to manually typecast allocated pointers when constructing state 
 * objects.
 *
 * The user must create a state machine (`st::sptr<at::state::machine>`) 
 * using static function `at::state::machine::make()` to register their states
 * and trigger events. The state machine can then be notified of new events 
 * with a call to 
 * `at::state::machine::process_event(st::sptr<st::message>)`.
 */
struct state : protected type_aware {
    // explicit destructor definition to allow for proper virtual delete behavior
    virtual ~state(){} 

    /**
     * @brief called during a transition when a state is entered 
     *
     * The returned value from this function can contain a further event to
     * process. 
     *
     * That is, if the return value:
     * - is null: operation is complete 
     * - is non-null: the result as treated like the argument of an additional `process_event()` call
     *
     * Thus, this function can be used to implement transitory states where 
     * logic must occur before the next state is known. 
     *
     * @param event a message containing the event id and an optional data payload
     * @return optional shared_ptr<message> containing the next event to process (if pointer is null, no futher event will be processed)
     */
    inline virtual st::sptr<message> enter(st::sptr<message> event) { 
        return st::sptr<message>();
    }

    /**
     * @brief called during a transition when a state is exitted
     *
     * The return value determines whether the transition from the current state 
     * will be allowed to continue. Thus, this function can be used to implement 
     * transition guards.
     *
     * @param event a message containing the event id and an optional data payload
     * @return true if exit succeeded and transition can continue, else false
     */
    inline virtual bool exit(st::sptr<message> event) { 
        return true; 
    }

    /**
     * @brief a convenience function for generating shared_ptr's to state objects
     * @param as Constructor arguments for type T
     * @return an allocated shared_ptr to type T implementing state
     */
    template <typename T, typename... As>
    static st::sptr<state> make(As&&... as) {
        st::sptr<state> st(dynamic_cast<state*>(new T(std::forward<As>(as)...)));
        st->m_type_code = st::type_code<T>();
        return st;
    }

    /**
     * The actual state machine.
     *
     * This object is NOT mutex locked, as it is not intended to be used directly 
     * in an asynchronous manner. 
     */
    struct machine {
        /**
         * @return an allocated state machine
         */
        static inline st::sptr<machine> make() {
            return st::sptr<machine>(new machine);
        }

        /**
         * @brief Register a state object to be transitioned to when notified of an event
         * @param event an unsigned integer representing an event that has occurred
         * @param st a pointer to an object which implements class state  
         * @return true if state was registered, false if state pointer is null or the same event is already registered
         */
        template <typename ID>
        bool register_transition(ID event_id, st::sptr<state> st) {
            return register_state(static_cast<std::size_t>(event_id), 
                                  registered_type::transitional_state, 
                                  st);
        }

        /**
         * 
         */
        typedef std::function<st::sptr<message>(st::sptr<message>)> callback;

        /**
         * @brief Register a callback to be triggered when its associated event is processed.
         *
         * When the corresponding event is processed for this callback *only* 
         * this function will be processed, as no state is exitted or entered. 
         *
         * The return value of the callback is treated exactly like that of 
         * `st::sptr<st::message> at::state::enter(st::sptr<st::message>)`.
         * That is, if the return value:
         * - is null: operation is complete 
         * - is non-null: the result as treated like the argument of an additional `process_event()` call
         *
         * @param event an unsigned integer representing an event that has occurred
         * @param cb a callback function
         * @return true if state was registered, false if state pointer is null or the same event is already registered
         */
        template <typename ID>
        bool register_callback(ID event_id, callback cb) {
            struct callback_state : public state {
                callback_state(callback&& cb) : m_cb(std::move(cb)) { }
                
                inline st::sptr<message> enter(st::sptr<message> event) { 
                    return m_cb(std::move(event));
                }

                callback m_cb;
            };

            return register_state(static_cast<std::size_t>(event_id), 
                                  registered_type::callback_state, 
                                  at::state::make<callback_state>(std::move(cb)));
        }

        /**
         * @brief process_event the state machine an event has occurred 
         *
         * If no call to `process_event()` has previously occurred on this state 
         * machine then no state `exit()` method will be called before the 
         * new state's `enter()` method is called.
         *
         * Otherwise, the current state's `exit()` method will be called first.
         * If `exit()` returns false, the transition will not occur. Otherwise,
         * the new state's `enter()` method will be called, and the current 
         * state will be set to the new state. 
         *
         * If `enter()` returns a new valid event message, then the entire 
         * algorithm will repeat until no allocated or valid event messages 
         * are returned by `enter()`.
         *
         * @param as argument(s) to `st::message::make()`
         * @return true if the event was processed successfully, else false
         */
        template <typename... As>
        bool process_event(As&&... as) {
            return internal_process_event(message::make(std::forward<As>(as)...));
        }

        /**
         * @brief a utility object to report information about the machine's current status
         */
        struct status {
            /**
             * @return true if the status is valid, else return false
             */
            inline operator bool() {
                return event && state;
            }

            /// the last event processed by the machine
            std::size_t event; 

            /// the current state held by the machine
            st::sptr<at::state> state; 
        };

        /**
         * @brief retrieve the current event and state information of the machine 
         *
         * If the returned `status` object is invalid, machine has not yet 
         * successfully processed any events.
         *
         * @return an object containing the most recently processed event and current state
         */
        inline status current_status() {
            if(m_cur_state != m_transition_table.end()) {
                return status{m_cur_state->first, m_cur_state->second.second}; 
            } else {
                return status{0, st::sptr<state>()};
            }
        }

    private:
        enum registered_type {
            transitional_state, // indicates state can be transitioned to
            callback_state // indicates state represents a callback and will not be transitioned to
        };

        typedef std::pair<registered_type,st::sptr<state>> state_info;
        typedef std::unordered_map<std::size_t,state_info> transition_table_t;

        machine() : m_cur_state(m_transition_table.end()) { }
        machine(const machine& rhs) = delete;
        machine(machine&& rhs) = delete;

        bool register_state(std::size_t event_id, registered_type tp, st::sptr<state> st) {
            auto it = m_transition_table.find(event_id);
            if(st && it == m_transition_table.end()) {
                m_transition_table[event_id] = state_info(tp, st);
                return true;
            } else {
                return false;
            }
        }

        inline bool internal_process_event(st::sptr<message> event) {
            if(event) {
                // process events
                do {
                    auto it = m_transition_table.find(event->id());

                    if(it != m_transition_table.end()) {
                        switch(it->second.first) {
                            case registered_type::transitional_state: 
                                // exit old state 
                                if(m_cur_state != m_transition_table.end()) {
                                    if(!m_cur_state->second.second->exit(event)) {
                                        // exit early as transition cannot continue
                                        return true; 
                                    }
                                }

                                // enter new state(s)
                                event = it->second.second->enter(event);
                                m_cur_state = it;
                                break;
                            case registered_type::callback_state:
                                // execute callback
                                event = it->second.second->enter(event);
                                break;
                            default:
                                event.reset();
                                break;
                        }
                    } else { 
                        return false;
                    }
                } while(event);

                return true;
            } else {
                return false;
            }
        }

        transition_table_t m_transition_table;
        transition_table_t::iterator m_cur_state;
    };

private:
    // number representing compiler type of the derived state object
    std::size_t m_type_code;
};

}

#endif
