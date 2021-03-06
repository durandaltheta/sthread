# Simple Threading and Communication
## Quick Links

[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/simple_thread_tst.cpp)

[Basic Usage](#basic-usage)

[Extended Usage - Generic Asynchronous Code Execution](#generic-asynchronous-code-execution)

[Extended Usage - Multiple Thread Executor](#multiple-thread-executor)

[Extended Usage - States and Finite State Machine](#states-and-finite-state-machine)

## Purpose 
This header only library seeks to easily setup useful worker threads using a 
simple API.

Instead of functions worker threads execute c++ functors. A functor is a class 
which has a function call overload allowing you to execute the object like a 
function, IE:
```
struct MyClass {
    inline void operator()(std::shared_ptr<st::message> msg) { /* ... */ }
};
```

Functors (as used by this library) have several advantages over raw functions. 
- The worker thread message receive loop is managed by the library 
- The worker lifecycle is managed by the library 
- Sending messages to the worker is provided by the library 
- Functors allow for inheritance
- Functors allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
- Functors allow for class method definitions, instead of forcing the user to rely on lambdas, local objects or global namespace functions if further function calls are desired.
- Initialization (constructor), runtime execution (`void operator()(std::shared_ptr<st::message>`), and deinitialization (destructor) are broken in to separate functions, which I think makes them more readable. A thread running only a raw function requires everything be managed within that function.

## Requirements
- C++11 

## Git Submodules
This project uses Googletest as a submodule to build unit tests. If unit tests 
are needed try cloning this project with submodules:
- git clone --recurse-submodules https://github.com/durandaltheta/sthread

## Installation
- cmake .
- sudo make install  

Alternatively just copy the .h files in inc/ folder to your local project headers and include as normal.

## Build Unit Tests 
- cmake .
- make simple_thread_tst 

simple_thread_tst binary will be placed in tst/ 

## Basic Usage

[Back To Top](#simple-threading-and-communication)

- Install the library and include the header `sthread` or `simple_thread.hpp`
- Create a class or struct with `void operator()(std::shared_ptr<st::message>)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::worker::make<YourClassNameHere>()`
- Trigger user class `void operator()(std::shared<st::message>)` via `st::worker::send(/* ... */)` or  `st::worker::try_send(/* ... */)`

#### Example 1:
```
#include <iostream>
#include <sthread>

struct MyClass {
    enum op {
        hello,
        world
    };

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::hello:
                std::cout << "hello " << std::endl;
                break;
            case op::world:
                std::cout << "world" << std::endl;
                break;
        }
    }
};

int main() {
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    my_worker->send(MyClass::op::hello);
    my_worker->send(MyClass::op::world);
}
```

Terminal output might be:
```
$./a.out
hello world
```

### Message Payload Data
Message data payloads can be of any type and can be copied to argument `T t` with `st::message::copy_data_to<T>(T&& t)` or rvalue swapped with `st::message::move_data_to<T>(T&& t)`. 

`st::message::copy_data_to<T>(T&& t)` and `st::message::move_data_to<T>(T&& t)` will return `true` only if the stored payload type matches type `T`, otherwise it returns `false`. 

NOTE: Arguments passed to `st::worker::send(/* ... */)` or  `st::worker::try_send(/* ... */)` are subsequently passed to `std::shared_ptr<st::message>  st::message::make(/* ... */)` before the resulting `std::shared_ptr<st::message>` is passed to its destination thread and functor. The summary of the 3 basic overloads of `std::shared_ptr<st::message> st::message::make(/* ... */)` are:
- `std::shared_ptr<st::message> st::message::make(std::shared_ptr<st::message>)`: Returns its argument immediately with no changes
- `std::shared_ptr<st::message> st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` when `std::size_t st::message::id()` is called
- `template <typename T> std::shared_ptr<st::message> st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T` which can later be copied with `template <typename T> bool st::message::copy_data_to(T& t)` or moved out of the message with `template <typename T> bool st::message::move_data_to(T& t)`.

#### Example 2:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::print:
            {
                std::string s;
                if(msg->copy_data_to(s)) {
                    std::cout << s << std::endl;
                }
                break;
            }
        }
    }
};

int main() {
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    std::string s("hello again");
    my_worker->send(MyClass::op::print, s);
}
```

Terminal output might be:
```
$./a.out
hello again
```

### Payload Type Checking
Payload types can also be easily checked with `st::message::is<T>()` (returns `true` if types match, else `false`) which is useful if a message might contain several different potential types.

#### Example 3:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::print:
                if(msg->is<std::string>()) {
                    std::string s;
                    msg->copy_data_to(s);
                    std::cout << s;
                } else if(msg->is<int>()) {
                    int i = 0;
                    msg->copy_data_to(i);
                    std::cout << i;
                }
                break;
        }
    }
};

int main() {
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    std::string s("hello ");
    my_worker->send(MyClass::op::print, s);
    int i = 1;
    my_worker->send(MyClass::op::print, i);
    s = " more time\n";
    my_worker->send(MyClass::op::print, s);
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
```

`st::worker`s will automatically shutdown and join when they are destructed. This can be done early with `st::worker::shutdown()`. `st::worker`s can also be restarted at any time with `st::worker::restart()`. See 
[documentation](https://durandaltheta.github.io/sthread/) for more info.


### Worker Constructor Arguments and Lifecycle
`st::worker`s can be passed constructor arguments in `st::worker::make<FUNCTOR>(As&&...)`. The `FUNCTOR` class will be created on the new thread and destroyed before said thread ends.

An `st::worker`'s `std::thread` will be shutdown and joined when any of the following happens:
- The `st::worker`s last `std::shared_ptr` goes out of scope
- `st::worker::shutdown()` is called on a worker
- `st::worker::restart()` is called on a worker (and a new `std::thread` and `FUNCTOR` will be created before `restart()` returns)

#### Example 4
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    MyClass(std::string constructor_string, std::string destructor_string) :
        m_destructor_string(destructor_string)
    {
        std::cout << std::this_thread::get_id() << ":" << constructor_string << std::endl;
    }

    ~MyClass() {
        std::cout << std::this_thread::get_id() << ":" <<  m_destructor_string << std::endl;
    }

    inline void operator()(std::shared_ptr<st::message> msg) { }

    std::string m_destructor_string;
};

int main() {
    std::cout << std::this_thread::get_id() << ":" <<  "parent thread started" << std::endl;
    std::shared_ptr<st::worker> wkr = st::worker::make<MyClass>("hello", "goodbye");
}

```

Terminal output might be:
```
$./a.out 
0x800018040:parent thread started
0x800098150:hello
0x800098150:goodbye
```


### Abstracting Message Passing Details
One useful advanced design pattern is abstracting all of the operation details 
for message passing into some API, freeing the user from having to interact 
directly with the thread details.

#### Example 5:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    static inline MyClass make() {
        return MyClass(st::worker::make<Worker>());
    }

    inline void set_string(std::string txt) {
        m_wkr->send(op::eset_string, txt);
    }

    inline std::string get_string() {
        auto ret_ch = st::channel::make();
        m_wkr->send(op::eget_string, ret_ch);
        std::string s;
        std::shared_ptr<st::message> msg;
        ret_ch->recv(msg);
        msg->copy_data_to(s);
        return s;
    }

private:
    enum op {
        eset_string,
        eget_string
    };

    struct Worker { 
        inline void operator()(std::shared_ptr<st::message> msg) {
            switch(msg->id()) {
                case op::eset_string:
                    msg->copy_data_to(m_str);
                    break;
                case op::eget_string:
                {
                    std::shared_ptr<st::channel> ret_ch;
                    if(msg->copy_data_to(ret_ch)) {
                        ret_ch->send(0,m_str);
                    }
                    break;
                }
            }
        }

        std::string m_str;
    };

    MyClass(std::shared_ptr<st::worker> wkr) : m_wkr(wkr) { }

    std::shared_ptr<st::worker> m_wkr;
};

int main() {
    MyClass my_class = MyClass::make();
    my_class.set_string("hello");
    std::cout << my_class.get_string() << std::endl;
    my_class.set_string("hello hello");
    std::cout << my_class.get_string() << std::endl;
}
```

Terminal output might be:
```
$./a.out
hello
hello hello
```


### Channels
The object that `st::worker`s use for communication in their `send()` methods is called `st::channel`. `st::channel`s can be created and used outside of `st::worker` objects if desired. 

#### Example 6:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        forward
    };

    MyClass(std::shared_ptr<st::channel> fwd_ch) : m_fwd_ch(fwd_ch) { }

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::forward:
                m_fwd_ch->send(msg);
                break;
        }
    }

    std::shared_ptr<st::channel> m_fwd_ch;
};

int main() {
    std::shared_ptr<st::channel> my_channel = st::channel::make();
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>(my_channel);

    my_worker->send(MyClass::op::forward, std::string("forward this string"));
    
    std::shared_ptr<st::message> msg;
    my_channel->recv(msg);

    std::string s;
    if(msg->copy_data_to(s)) {
        std::cout << s << std::endl;
    }
}
```

Terminal output might be:
```
$./a.out 
forward this string
```

### Close, Shutdown, and Restart
In looping `st::channel::recv()` operations `st::channel::close()` can be manually called to force all operations to cease on the `st::channel` (operations will return `false`). The default behavior for `st::channel::close()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. 

This is the default behavior of several functions:
- `st::channel::close(/* default true */)`
- `st::worker::shutdown(/* default true */)`
- `st::worker::restart(/* default true */)`

Alternatively, the user can call said functions with explicit `false` to immediately end all operations on the channel:
- `st::channel::close(false)`
- `st::worker::shutdown(false)`
- `st::worker::restart(false)`

NOTE: When an `st::worker` or `st::channel` goes out of scope (no more `std::shared_ptr` for that object instance exists), the object will be shutdown/closed with default behavior (if the object is not already shutdown/closed).

#### Example 7:
```
#include <iostream>
#include <string>
#include <sthread>

void looping_recv(std::shared_ptr<st::channel> ch) {
    std::shared_ptr<st::message> msg;

    while(ch->recv(msg)) {
        std::string s;
        if(msg->copy_data_to(s)) {
            std::cout << s << std::endl;
        }
    }

    std::cout << "thread done" << std::endl;
}

int main() {
    std::shared_ptr<st::channel> my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);
    std::shared_ptr<st::message> msg;

    my_channel->send(0, std::string("You say goodbye"));
    my_channel->send(0, std::string("And I say hello"));

    my_channel->close(); // end thread looping 
    my_thread.join(); // join thread
}
```

Terminal output might be:
```
$./a.out 
You say goodbye
And I say hello
thread done 
```


### Extended Usage  

The following documentation is for extended features of this library. They are not required to fulfill the primary purpose of this library (simple threading and communication), but they may be useful for simplifying advanced threading applications. 

#### Generic Asynchronous Code Execution

[Back To Top](#simple-threading-and-communication) 

`st::processor` is a simple functor implementation which will attempt to execute 
any `st::processor::task` (a type alias of `std::function<void()>`) object passed 
to it as the payload of its message (all message id's are ignored).

##### Example 8:
```
#include <iostream>
#include <string>
#include <sthread>

int main() {
    std::shared_ptr<st::worker> proc = st::worker::make<st::processor>();

    std::cout << std::this_thread::get_id() << ": main thread\n";

    auto greet = [] {
        std::stringstream ss;
        ss << std::this_thread::get_id() << ": hello\n";
        std::cout << ss.str().c_str();
    };

    for(std::size_t c=0; c<5; ++c) {
        proc->send(0, st::processor::task(greet));
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

#### Multiple Thread Executor

[Back To Top](#simple-threading-and-communication) 

`st::executor` is a class managing one or more identical worker threads which 
can be allocated and started with a call to
`template <typename FUNCTOR, typename... As> static std::shared_ptr<st::executor>
make(std::size_t worker_count, As&&... as)`. Said function executes in a 
fashion extremely similar to `template <typename FUNCTOR, typename... As> static std::shared_ptr<st::worker> make(std::size_t worker_count, As&&... as)`, except that an 
executor represents multiple threads instead of one thread.

The API of `st::executor` is extremely similar to `st::worker`, see 
[documentation](https://durandaltheta.github.io/sthread/) for more info.

The `st::executor` object implements a constant time algorithm which attempts to 
efficiently distribute tasks among worker threads. This object is especially 
useful for scheduling operations which benefit from high CPU throughput and are 
not reliant on the specific thread upon which they run. 

Highest CPU throughput is typically reached by an executor whose worker count 
matches the CPU core count of the executing machine. This optimal number of 
cores may be discoverable by the return value of a call to 
`st::executor::default_worker_count()`, though this is not guaranteed by the
c++ standard.

Because `st::executor` manages a limited number of workers, any message whose 
processing blocks a worker indefinitely can cause all sorts of bad effects, 
including deadlock. 

##### Example 9:
```
#include <iostream>
#include <sstream>
#include <sthread>

int main() {
    std::size_t wkr_cnt = st::executor::default_worker_count();
    std::shared_ptr<st::executor> exec = st::executor::make<st::processor>(wkr_cnt);

    std::cout << std::this_thread::get_id() << ": worker count: " << exec->worker_count() << std::endl;

    auto greet = [] {
        std::stringstream ss;
        ss << std::this_thread::get_id() << ": hello\n";
        std::cout << ss.str().c_str();
    };

    for(std::size_t c=0; c<5; ++c) {
        exec->send(0, st::processor::task(greet));
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

#### States and Finite State Machine

[Back To Top](#simple-threading-and-communication)

This library provides a fairly simple finite state machine (FSM) implementation 
as a design tool. 

The reasoning for including this feature in the library is that asynchronous 
programming can have complex state management. Simplifying designs with a state 
machine can *sometimes* be advantagous, when used intelligently and judiciously. 

The state machine object type is `st::state::machine`, which can register new 
state transitions with calls to `st::state::machine::register_transition()` and 
process events with `st::state::machine::process_event()`.

The user can create states by defining classes which inherit `st::state`,
optionally overriding methods and passing an allocated `shared_ptr<st::state>` 
of that class to `st::state::machine::register_transition()`. The function
`st::state::make<YourStateType>(/* YourStateType constructor args */)` is 
provided as a convenience for this process. 

##### Example 10:
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

    struct listening : public st::state {
        std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::cout << "your partner begins speaking and you listen" << std::endl;
            // a default (null) shared pointer returned from enter() causes transition to continue normally
            return std::shared_ptr<st::message>(); 
        }
    };

    struct talking : public st::state {
        std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::cout << "you begin speaking and your partner listens" << std::endl;
            return std::shared_ptr<st::message>();
        }
    };

    auto listening_st = st::state::make<listening>();
    auto talking_st = st::state::make<talking>();
    auto conversation_machine = st::state::machine::make();

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
Since function signatures `std::shared_ptr<st::message> st::state::enter(std::shared_ptr<st::message>)` 
and  `bool st::state::exit(std::shared_ptr<st::message>)` accept a message 
object as their arguments, the user can directly replace `switch` statements 
from within `st::worker` instances with calls to 
`st::state::machine::process_event()` if desired.

##### Example 11:
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

        struct listening : public st::state {
            std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "your partner speaks: " << s << std::endl;
                return std::shared_ptr<st::message>();
            }
        };

        struct talking : public st::state {
            std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
                std::string s;
                event->copy_data_to(s);
                std::cout << "you speak: " << s << std::endl;
                return std::shared_ptr<st::message>();
            }
        };

        conversation_worker() { 
            auto listening_st = st::state::make<listening>();
            auto talking_st = st::state::make<talking>();
            m_machine = st::state::machine::make();

            // register the state transitions 
            m_machine->register_transition(conversation_worker::op::partner_speaks, listening_st);
            m_machine->register_transition(conversation_worker::op::you_speak, talking_st);
        }

        inline void operator()(std::shared_ptr<st::message> msg) {
            m_machine->process_event(msg);
        }

        std::shared_ptr<st::state::machine> m_machine;
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
`bool st::state::exit(std::shared_ptr<st::message>)` method, where the state 
will only transition if that function returns `true`.

##### Example 12:
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

    struct listening : public st::state {
        std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "your partner speaks: " << s << std::endl;
            return std::shared_ptr<st::message>();
        }

        bool exit(std::shared_ptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::partner_speaks) {
                return true;
            } else {
                return false;
            }
        }
    };

    struct talking : public st::state {
        std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::string s;
            event->copy_data_to(s);
            std::cout << "you speak: " << s << std::endl;
            return std::shared_ptr<st::message>();
        }

        bool exit(std::shared_ptr<st::message> event) {
            // standard guard preventing transitioning to the same event as we are leaving
            if(event->id() != conversation::event::you_speak) {
                return true;
            } else {
                return false;
            }
        }
    };

    auto listening_st = st::state::make<listening>();
    auto talking_st = st::state::make<talking>();
    auto conversation_machine = st::state::machine::make();

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
If an implementation of `st::state::enter()` returns a non-null `std::shared_ptr<st::message>` 
that message will be handled as if `st::state::machine::process_event()` had been 
called with that message as its argument. This allows states to directly 
transition to other states if necessary:

##### Example 13:
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

    struct state1 : public st::state {
        std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::cout << "state1" << std::endl;
            return st::message::make(events::event2);
        }
    };

    struct state2 : public st::state {
        std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::cout << "state2" << std::endl;
            return st::message::make(events::event3);
        }
    };

    struct state3 : public st::state {
        std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::cout << "state3" << std::endl;
            return std::shared_ptr<st::message>();
        }
    };

    auto sm = st::state::machine::make();
    sm->register_transition(events::event1, st::state::make<state1>());
    sm->register_transition(events::event2, st::state::make<state2>());
    sm->register_transition(events::event3, st::state::make<state3>());

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
`st::state::machine::register_callback(ID event, st::state::machine::callback cb)`. 
This allows some events to be processed without attempting to transition the 
machine state.

The type `st::state::machine::callback` is a typedef of
`std::function<std::shared_ptr<st::message>(std::shared_ptr<st::message>)>` 
(which, as usual, can hold a functor, lambda, or function 
pointer).

The return value of the callback is treated exactly like that of 
`std::shared_ptr<st::message> st::state::enter(std::shared_ptr<st::message>)`.
That is, if the return value:
- is null: operation is complete 
- is non-null: the result as treated like the argument of an additional `process_event()` call 

##### Example 14:
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

    auto callback1 = [&](std::shared_ptr<st::message> event) {
        std::cout << "We " << std::endl;
        return st::message::make(op::trigger_cb2);
    };

    auto callback2 = [&](std::shared_ptr<st::message> event) {
        std::cout << "made " << std::endl;
        return st::message::make(op::trigger_final_state);
    };

    struct final_state : public st::state { 
        inline std::shared_ptr<st::message> enter(std::shared_ptr<st::message> event) {
            std::cout << "it!" << std::endl;
            return std::shared_ptr<st::message>();
        }
    };

    auto sm = st::state::machine::make();

    sm->register_callback(op::trigger_cb1, callback1);
    sm->register_callback(op::trigger_cb2, callback2);
    sm->register_transition(op::trigger_final_state, st::state::make<final_state>());

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

[Back To Top](#simple-threading-and-communication)
