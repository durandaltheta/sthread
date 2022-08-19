# Advanced Threading and Communication

[Back to README](README.md)

## Quick Links

[Generic Asynchronous Code Execution](#generic-asynchronous-code-execution)

[Multiple Thread Executor](#multiple-thread-executor)

[States and Finite State Machine](#states-and-finite-state-machine)

The following documentation is for extended features of this library. They are not required to fulfill the primary purpose of this library (simple threading and communication), but they may be useful for simplifying more advanced threading applications. 

### Generic Asynchronous Code Execution

[Back to Top](#advanced-threading-and-communication) 

`at::processor` is a simple functor implementation which will attempt to execute 
any `at::processor::task` (a type alias of `std::function<void()>`) object passed 
to it as the payload of its message (all message id's are ignored).

`at::processor` can also execute `at::cotask`s.

##### Processor Example 1:
```
#include <iostream>
#include <string>
#include <sthread>

int main() {
    st::sptr<st::worker> proc = st::worker::make<at::processor>();

    std::cout << std::this_thread::get_id() << ": main thread\n";

    auto greet = [] {
        std::stringstream ss;
        ss << std::this_thread::get_id() << ": hello\n";
        std::cout << ss.str().c_str();
    };

    for(std::size_t c=0; c<5; ++c) {
        proc->send(0, at::processor::task(greet));
    }

    return 0;
}
```

Terminal output might be:
```
$./a.out 
0x800018040: main thread
0x8000990f0: hello
0x8000990f0: hello
0x8000990f0: hello
0x8000990f0: hello
0x8000990f0: hello
```

### Multiple Thread Executor

[Back to Top](#advanced-threading-and-communication) 

`at::executor` is a class managing one or more identical worker threads which 
can be allocated and started with a call to
`template <typename FUNCTOR, typename... As> static st::sptr<at::executor>
make(std::size_t worker_count, As&&... as)`. Said function executes in a 
fashion extremely similar to `template <typename FUNCTOR, typename... As> static st::sptr<st::worker> make(std::size_t worker_count, As&&... as)`, except that an 
executor represents multiple threads instead of one thread.

The API of `at::executor` is extremely similar to `st::worker`, see 
[documentation](https://durandaltheta.github.io/sthread/) for more info.

The `at::executor` object implements a constant time algorithm which attempts to 
efficiently distribute tasks among worker threads. This object is especially 
useful for scheduling operations which benefit from high CPU throughput and are 
not reliant on the specific thread upon which they run. 

Highest CPU throughput is typically reached by an executor whose worker count 
matches the CPU core count of the executing machine. This optimal number of 
cores may be discoverable by the return value of a call to 
`at::executor::default_worker_count()`, though this is not guaranteed.

Because `at::executor` manages a limited number of workers, any message whose 
processing blocks a worker indefinitely can cause all sorts of bad effects, 
including deadlock. 

#####  Executor Example 1:
```
#include <iostream>
#include <sstream>
#include <sthread>

int main() {
    std::size_t wkr_cnt = at::executor::default_worker_count();
    st::sptr<at::executor> exec = at::executor::make<at::processor>(wkr_cnt);

    std::cout << std::this_thread::get_id() << ": worker count: " << exec->worker_count() << std::endl;

    auto greet = [] {
        std::stringstream ss;
        ss << std::this_thread::get_id() << ": hello\n";
        std::cout << ss.str().c_str();
    };

    for(std::size_t c=0; c<5; ++c) {
        exec->send(0, at::processor::task(greet));
    }

    return 0;
}
```

Terminal output might be:
```
$./a.out 
0x800018040: worker count: 16
0x80009abf0: hello
0x80009b470: hello
0x80009a370: hello
0x800099af0: hello
0x80009bcf0: hello
```

### Coroutines 
This library supports a kind of coroutine called `at::cotask`. They are created 
very similarly to `st::worker` in that they are constructed using a functor type 
in `at::cotask::make<FUNCTOR>(/* ... */)`.

`at::cotask` are intended to be run on an `st::worker` executing an 
`at::processor`. Many `at::cotask`s can run on the same `at::processor`.

##### Cotask Example 1
```
#include <iostream>
#include <string>

struct my_cotask {
    enum op {
        greet,
    };

    void operator()(st::sptr<st::message> msg) {
        switch(msg->id()) {
            case op::greet:
                std::cout << "hello" << std::endl;
                break;
        }
    }
};

int main() {
    auto ch = st::channel::make();
    auto wkr = st::worker::make<at::processor>();
    auto ct = at::cotask::make<my_cotask>(); // create cotask
    wkr->send(0, ct); // schedule cotask on processor

    // send messages to running cotask 
    ct->send(my_cotask::op::greet);
    return 0;
}
```

##### Cotask Example 2
```
#include <iostream>
#include <string>

struct my_cotask {
    typedef std::function<void(std::string)> name_callback;

    enum op {
        name_req
    };

    void operator()(st::sptr<st::message> msg) {
        switch(msg->id()) {
            case op::name_req:
            {
                name_callback nc;
                if(msg->move_data_to(nc)) {
                    nc(std::string("george"));
                }
                break;
            }
        }
    }
};

int main() {
    auto wkr = st::worker::make<at::processor>();
    auto ct = at::cotask::make<my_cotask>(); // create cotask
    wkr->send(0, ct); // schedule cotask on processor

    // send request to the cotask for its name string
    auto ret_ch = st::channel::make();
    my_cotask::name_callback nc = [=](std::string s){ ch->send(0,s); };
    ct->send(my_cotask::op::name_req, nc);

    // receive the result of the name request
    st::sptr<st::message> msg;
    if(ch->recv(msg)) {
        std::string s;
        if(msg->copy_data_to(s)) {
            std::cout << "cotask's name: " << s << std::endl;
        }
    }

    return 0;
}
```



### States and Finite State Machine

[Back to Top](#advanced-threading-and-communication)

This library provides a fairly simple finite state machine (FSM) implementation 
as a design tool. 

The reasoning for including this feature in the library is that asynchronous 
programming can have complex state management. Simplifying designs with a state 
machine can *sometimes* be advantagous, when used intelligently and judiciously. 

The state machine object type is `at::state::machine`, which can register new 
state transitions with calls to `at::state::machine::register_transition()` and 
process events with `at::state::machine::process_event()`.

The user can create states by defining classes which inherit `at::state`,
optionally overriding methods and passing an allocated `shared_ptr<at::state>` 
of that class to `at::state::machine::register_transition()`. The function
`at::state::make<YourStateType>(/* YourStateType constructor args */)` is 
provided as a convenience for this process. 

##### State Example 1:
```
#include <iostream>
#include <string>
#include <sthread>

int main() {
    struct conversation {
        enum event {
            partner_speaks,
            you_speak 
        };
    };

    struct listening : public at::state {
        st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "your partner begins speaking and you listen" << std::endl;
            // a default (null) shared pointer returned from enter() causes transition to continue normally
            return st::sptr<st::message>(); 
        }
    };

    struct talking : public at::state {
        st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "you begin speaking and your partner listens" << std::endl;
            return st::sptr<st::message>();
        }
    };

    auto listening_st = at::state::make<listening>();
    auto talking_st = at::state::make<talking>();
    auto conversation_machine = at::state::machine::make();

    // register the state transitions 
    conversation_machine->register_transition(conversation::event::partner_speaks, listening_st);
    conversation_machine->register_transition(conversation::event::you_speak, talking_st);

    // set the initial machine state 
    conversation_machine->process_event(conversation::event::partner_speaks);

    // have a conversation
    conversation_machine->process_event(conversation::event::you_speak); 
    conversation_machine->process_event(conversation::event::partner_speaks); 
    return 0;
}
```

Terminal output might be:
```
$./a.out 
your partner begins speaking and you listen
you begin speaking and your partner listens
your partner begins speaking and you listen
```

#### Replacing switch statements with state machines
Since function signatures `st::sptr<st::message> at::state::enter(st::sptr<st::message>)` 
and  `bool at::state::exit(st::sptr<st::message>)` accept a message 
object as their arguments, the user can directly replace `switch` statements 
from within `st::worker` instances with calls to 
`at::state::machine::process_event()` if desired.

##### State Example 2:
```
#include <iostream>
#include <string>
#include <sthread>

int main() {
    struct conversation_worker {
        enum op {
            partner_speaks,
            you_speak 
        };

        struct listening : public at::state {
            st::sptr<st::message> enter(st::sptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "your partner speaks: " << s << std::endl;
                return st::sptr<st::message>();
            }
        };

        struct talking : public at::state {
            st::sptr<st::message> enter(st::sptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "you speak: " << s << std::endl;
                return st::sptr<st::message>();
            }
        };

        conversation_worker() { 
            auto listening_st = at::state::make<listening>();
            auto talking_st = at::state::make<talking>();
            m_machine = at::state::machine::make();

            // register the state transitions 
            m_machine->register_transition(conversation_worker::op::partner_speaks, listening_st);
            m_machine->register_transition(conversation_worker::op::you_speak, talking_st);
        }

        inline void operator()(st::sptr<st::message> msg) {
            m_machine->process_event(msg);
        }

        st::sptr<at::state::machine> m_machine;
    };

    // launch a worker thread to utilize the state machine
    auto wkr = st::worker::make<conversation_worker>();

    // set the initial machine state and begin handling events
    wkr->send(conversation_worker::op::partner_speaks, std::string("hello foo"));
    wkr->send(conversation_worker::op::you_speak, std::string("hello faa")); 
    wkr->send(conversation_worker::op::partner_speaks, std::string("goodbye foo")); 
    wkr->send(conversation_worker::op::you_speak, std::string("goodbye faa")); 
    return 0;
}
```

Terminal output might be:
```
$./a.out 
your partner speaks: hello foo
you speak: hello faa
your partner speaks: goodbye foo
you speak: goodbye faa
```

#### Implementing state transition guards
The user can implement transition guards and prevent transitioning away 
from a state by overriding the 
`bool at::state::exit(st::sptr<st::message>)` method, where the state 
will only transition if that function returns `true`.

##### State Example 3:
```
#include <iostream>
#include <string>
#include <sthread>

int main() {
    struct conversation {
        enum event {
            partner_speaks,
            you_speak 
        };
    };

    struct listening : public at::state {
        st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "your partner speaks: " << s << std::endl;
            return st::sptr<st::message>();
        }

        bool exit(st::sptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::partner_speaks) {
                return true;
            } else {
                return false;
            }
        }
    };

    struct talking : public at::state {
        st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "you speak: " << s << std::endl;
            return st::sptr<st::message>();
        }

        bool exit(st::sptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::you_speak) {
                return true;
            } else {
                return false;
            }
        }
    };

    auto listening_st = at::state::make<listening>();
    auto talking_st = at::state::make<talking>();
    auto conversation_machine = at::state::machine::make();

    // register the state transitions 
    conversation_machine->register_transition(conversation::event::partner_speaks, listening_st);
    conversation_machine->register_transition(conversation::event::you_speak, talking_st);

    // set the initial machine state and begin handling events (duplicate events 
    // will be ignored)
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo")); 
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo2")); 
    conversation_machine->process_event(conversation::event::partner_speaks, std::string("hello foo3"));
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa")); 
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa2")); 
    conversation_machine->process_event(conversation::event::you_speak, std::string("hello faa3")); 
    return 0;
}
```

Terminal output might be:
```
$./a.out 
your partner speaks: hello foo
you speak: hello faa
```

#### Processing subsequent events directly from the result of a state transition
If an implementation of `at::state::enter()` returns a non-null `st::sptr<st::message>` 
that message will be handled as if `at::state::machine::process_event()` had been 
called with that message as its argument. This allows states to directly 
transition to other states if necessary:

##### State Example 4:
```
#include <iostream>
#include <string>
#include <sthread>

int main() {
    struct events {
        enum op {
            event1,
            event2,
            event3
        };
    };

    struct state1 : public at::state {
        st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "state1" << std::endl;
            return st::message::make(events::event2);
        }
    };

    struct state2 : public at::state {
        st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "state2" << std::endl;
            return st::message::make(events::event3);
        }
    };

    struct state3 : public at::state {
        st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "state3" << std::endl;
            return st::sptr<st::message>();
        }
    };

    auto sm = at::state::machine::make();
    sm->register_transition(events::event1, at::state::make<state1>());
    sm->register_transition(events::event2, at::state::make<state2>());
    sm->register_transition(events::event3, at::state::make<state3>());

    sm->process_event(events::event1);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
state1
state2
state3
```

#### Registering non-transitioning callbacks to a state machine 
The user can register callbacks to be executed when an associated event is 
processed with 
`at::state::machine::register_callback(ID event, at::state::machine::callback cb)`. 
This allows some events to be processed without attempting to transition the 
machine state.

The type `at::state::machine::callback` is a typedef of
`std::function<st::sptr<st::message>(st::sptr<st::message>)>` 
(which, as usual, can hold a functor, lambda, or function 
pointer).

The return value of the callback is treated exactly like that of 
`st::sptr<st::message> at::state::enter(st::sptr<st::message>)`.
That is, if the return value:
- is null: operation is complete 
- is non-null: the result as treated like the argument of an additional `process_event()` call 

##### State Example 5:
```
#include <iostream>
#include <string>
#include <sthread>

int main() {
    enum class op {
        trigger_cb1,
        trigger_cb2,
        trigger_final_state
    };

    auto callback1 = [&](st::sptr<st::message> event) {
        std::cout << "We " << std::endl;
        return st::message::make(op::trigger_cb2);
    };

    auto callback2 = [&](st::sptr<st::message> event) {
        std::cout << "made " << std::endl;
        return st::message::make(op::trigger_final_state);
    };

    struct final_state : public at::state { 
        inline st::sptr<st::message> enter(st::sptr<st::message> event) {
            std::cout << "it!" << std::endl;
            return st::sptr<st::message>();
        }
    };

    auto sm = at::state::machine::make();

    sm->register_callback(op::trigger_cb1, callback1);
    sm->register_callback(op::trigger_cb2, callback2);
    sm->register_transition(op::trigger_final_state, at::state::make<final_state>());

    sm->process_event(op::trigger_cb1);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
We
made
it!
```

[Back to Top](#advanced-threading-and-communication)
