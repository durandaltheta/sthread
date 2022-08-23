# Simple Threading and Communication
## Quick Links

This library makes extensive use of inherited functionality via interfaces. If 
code lacks documentation, look at the interfaces for more information.

[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/simple_thread_tst.cpp)

#### Usage:
[Basic Usage](#basic-usage)

[Send Operations](#send-operations)

[Type Checking](#type-checking)

[Fiber Constructor Arguments](#fiber-constructor-arguments)

[Sending Messages To Standard Threads With Channels](#sending-messages-to-standard-threads-with-channels)

[Dealing With Blocking Functions](#dealing-with-blocking-functions)

[Running Functions on Fibers](#running-functions-on-fibers)

[Running Fibers On Fibers](#running-fibers-on-other-fibers)

[Object Lifecycles](#object-lifecycles)

## Purpose 
This header only library seeks to easily setup useful threading & concurrency with a simple API.

The main thread-like object provided by the library is `st::fiber`, an object 
which can manage a system thread and process incoming messages.

Instead of functions `st::fiber`s execute c++ functors. A functor is a class 
which has a function call overload allowing you to execute the object like a 
function, IE:
```
struct MyClass {
    void operator()(std::shared_ptr<st::message> msg) { /* ... */ }
};
```

Functors (as used by this library) have several advantages over raw functions. 
- The thread's message receive loop is managed by the library 
- The thread's lifecycle is managed by the library 
- Sending messages to the thread is provided by the library 
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

## Shared Pointers
This class makes extensive use of `std::shared_ptr` instances. Unfortunately,
this can make fairly simple code unnecessarily verbose. As a convenience this 
library provides `st::sptr` as a typedef to `std::shared_ptr`, `st::wptr` as 
a typedef to `std::weak_ptr`, and `st::uptr` as a typedef to `st::unique_ptr`. 

Usage of these typedefs is completely optional in user code.

## Basic Usage
- Install the library and include the header `sthread` or `simple_thread.hpp`
- Create a class or struct with `void operator()(st::sptr<st::message>)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::fiber::thread<YourClassNameHere>()`
- Trigger user class's `void operator()(st::sptr<st::message>)` via `st::fiber::send(/* ... */)` 

#### Example 1:
```
#include <iostream>
#include <sthread>

struct MyClass {
    enum op {
        hello,
        world
    };

    void operator()(st::sptr<st::message> msg) {
        switch(msg->id) {
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
    st::sptr<st::fiber> my_thread = st::fiber::thread<MyClass>();

    my_thread->send(MyClass::op::hello);
    my_thread->send(MyClass::op::world);
}
```

Terminal output might be:
```
$./a.out
hello
world
```

[Back To Top](#simple-threading-and-communication)

### Message Payload Data 
Messages store their (optional) payload data in an object called `st::data`.
The message's data object is a member also called `data` (`st::message::data`).

Message data can be of any type and can be copied to argument `T t` with `st::data::copy_to<T>(T&& t)` or rvalue swapped with `st::message::move_data_to<T>(T&& t)`. 

`st::data::copy_to<T>(T&& t)` and `st::data::move_to<T>(T&& t)` will return `true` only if the stored payload type matches type `T`, otherwise it returns `false`. 

#### Example 2:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    void operator()(st::sptr<st::message> msg) {
        switch(msg->id) {
            case op::print:
            {
                std::string s;
                if(msg->data.copy_to(s)) {
                    std::cout << s << std::endl;
                }
                break;
            }
        }
    }
};

int main() {
    st::sptr<st::fiber> my_thread = st::fiber::thread<MyClass>();

    my_thread->send(MyClass::op::print, "hello again");
}
```

Terminal output might be:
```
$./a.out
hello again
```

[Back To Top](#simple-threading-and-communication)

### Send Operations
Several classes in this library support the ability to send messages:
- `st::channel::send(/* ... *)`
- `st::fiber::send(/* ... *)`
- `st::executor::send(/* ... *)`

Arguments passed to `send(/* ... */)` are subsequently passed to `st::sptr<st::message>  st::message::make(/* ... */)` before the resulting `st::sptr<st::message>` is passed to its destination thread and functor. The summary of the 3 basic overloads of `st::sptr<st::message> st::message::make(/* ... */)` are:
- `st::sptr<st::message> st::message::make(st::sptr<st::message>)`: Returns its argument immediately with no changes
- `template <typename ID> st::sptr<st::message> st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id`.
- `template <typename ID, typename T> st::sptr<st::message> st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T` which can later be copied with `template <typename T> bool st::data::copy_to(T& t)` or moved out of the message with `template <typename T> bool st:data::move_to(T& t)`.

[Back To Top](#simple-threading-and-communication)

### Type Checking
Payload types can also be easily checked with `st::data::is<T>()` (returns `true` if types match, else `false`) which is useful if a message's data might contain several different potential types. 

NOTE: `st::data` can store a payload of any type. However, it does provide one special exception when passed explicit c-style `char*` strings, where it will automatically convert the argument `char*` into a `std::string` to protect the user against unexpected behavior. However, this means the the user must use `std::string` type when trying to copy or move the data back out. If this behavior is not desired, the user will have to wrap their `char*` in some other object.

Some classes support a similar method also named `is<T>()`:
- `st::fiber::is<T>()` // compares against the type of the fiber's `FUNCTOR`

#### Example 3:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    void operator()(st::sptr<st::message> msg) {
        switch(msg->id) {
            case op::print:
                if(msg->data.is<std::string>()) {
                    std::string s;
                    msg->data.copy_to(s);
                    std::cout << s;
                } else if(msg->data.is<int>()) {
                    int i = 0;
                    msg->data.copy_to(i);
                    std::cout << i;
                }
                break;
        }
    }
};

int main() {
    st::sptr<st::fiber> my_thread = st::fiber::thread<MyClass>();

    std::string s("hello ");
    my_thread->send(MyClass::op::print, s);
    int i = 1;
    my_thread->send(MyClass::op::print, i);
    s = " more time\n";
    my_thread->send(MyClass::op::print, s);
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
```

`st::fiber`s will automatically shutdown and join when they are destructed. This can be done early with `st::fiber::shutdown()`. 
[documentation](https://durandaltheta.github.io/sthread/) for more info.

[Back To Top](#simple-threading-and-communication)

### Fiber Constructor Arguments
`st::fiber`s can be passed constructor arguments in `st::fiber::thread<FUNCTOR>(As&&...)`. The `FUNCTOR` class will be created on the new thread and destroyed before said thread ends.

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

    void operator()(st::sptr<st::message> msg) { }

    std::string m_destructor_string;
};

int main() {
    std::cout << std::this_thread::get_id() << ":" <<  "parent thread started" << std::endl;
    st::sptr<st::fiber> wkr = st::fiber::thread<MyClass>("hello", "goodbye");
}

```

Terminal output might be:
```
$./a.out 
0x800018040:parent thread started
0x800098150:hello
0x800098150:goodbye
```

[Back To Top](#simple-threading-and-communication)

### Sending Messages To Standard Threads With Channels
The object that `st::fiber`s use for communication in their `send()` methods is called `st::channel`. `st::channel`s can be created and used outside of `st::fiber` objects if desired. This allows the user, for example, to send messages to threads which were not launched with `st::fiber::thread<T>()`.

#### Example 5:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        forward
    };

    MyClass(st::sptr<st::channel> fwd_ch) : m_fwd_ch(fwd_ch) { }

    void operator()(st::sptr<st::message> msg) {
        switch(msg->id) {
            case op::forward:
                m_fwd_ch->send(msg);
                break;
        }
    }

    st::sptr<st::channel> m_fwd_ch;
};

int main() {
    st::sptr<st::channel> my_channel = st::channel::make();
    st::sptr<st::fiber> my_thread = st::fiber::thread<MyClass>(my_channel);

    my_thread->send(MyClass::op::forward, "forward this string");
    
    st::sptr<st::message> msg;
    my_channel->recv(msg); // main listens on the channel

    std::string s;
    if(msg->data.copy_to(s)) {
        std::cout << s << std::endl;
    }
}
```

Terminal output might be:
```
$./a.out 
forward this string
```

[Back To Top](#simple-threading-and-communication)

### Dealing With Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid 
calling functions which will block for indeterminate periods within an `st::fiber`. If the user needs to call such a function, a simple solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a new thread, then `send()` the result back to the `st::fiber` when the call completes.

#### Example 6:
```
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sthread>

std::string blocking_function() {
    std::this_thread::sleep_for(std::chrono::seconds(5));A
    return "that's all folks!"
}

struct MyFunctor {
    MyFunctor(st::sptr<st::channel> main_ch) : m_main_ch(main_ch) { }

    enum op {
        call_blocker,
        print,
        unblock_main
    };

    void operator()(st::sptr<st::message> msg) {
        switch(msg->id) {
            case op::call_blocker:
                // acquire a copy of the running `st::fiber` shared pointer
                st::sptr<st::fiber> self = st::fiber::local_self();
               
                // use `std::async` to execute our code on a new thread
                std::async([self] {
                    std::string result = blocking_function();
                    self->send(op::print, result);
                    self->send(op::unblock_main);
                });
                break;
            case op::print:
                std::string s;
                if(msg.data->copy_to(s)) {
                    std::cout << s << std::endl;
                }
                break;
            case op::unblock_main:
                m_main_ch->send(0); // unblock main
                break;
        }
    }

    st::sptr<st::channel> m_main_ch;
}

int main() {
    st::sptr<st::channel> ch = st::channel::make();
    st::sptr<st::message> msg;
    st::sptr<st::fiber> my_fiber = st::fiber::thread<MyFunctor>(ch);
    my_fiber->send(MyFunctor::op::call_blocker);
    my_fiber->send(MyFunctor::op::print, "1");
    my_fiber->send(MyFunctor::op::print, "2");
    my_fiber->send(MyFunctor::op::print, "3");
    ch->recv(msg); // block so program doesn't end before our functions can run
    return 0;
};
```

Terminal output might be:
```
$./a.out
1
2
3
that's all folks!
```

[Back To Top](#simple-threading-and-communication)

### Running Functions on Fibers 
`st::fiber`s provide the ability to enqueue arbitrary code for execution with `st::fiber::schedule(...)` API. Any `st::fiber` can be used for this purpose, though the default `st::fiber::thread()` `FUNCTOR` template type `st::fiber::processor` is often useful for generating worker threads dedicated to scheduling other code.

`st::fiber::schedule()` can accept a function, functor, or lambda function, in a similar fashion to standard library features `std::async()` and `std::thread()`.

#### Example 7:
```
#include <iostream>
#include <string>
#include <sthread>

void print(std::string s) {
    std::cout << s << std::endl;
}

struct printer { 
    void operator()(std::string s) {
        std::cout << s << std::endl;
    }
};

int main() {
    // specifying `st::fiber::processor` inside the template `<>` is optional
    st::sptr<st::fiber> my_processor = st::fiber::thread<>();
    my_processor->schedule(print, std::string("what a beautiful day"));
    my_processor->schedule(printer, std::string("looks like rain"));
    my_processor->schedule([]{ std::cout << "what a beautiful sunset" << std::endl; });
}
```

Terminal output might be:
```
$./a.out 
what a beautiful day 
looks like rain 
what a beautiful sunset
```

[Back To Top](#simple-threading-and-communication)
### Running Fibers On Fibers 
`st::fiber` is actually an example of a stackless coroutine. According to wikipedia: 
```
Coroutines are computer program components that generalize subroutines for 
non-preemptive multitasking, by allowing execution to be suspended and resumed.
```

Without going into to much detail, here are some advantages of coroutines compared to threads:
 - changing which coroutine is running by suspending its execution is 
   exponentially faster than changing which system thread is running. IE, the 
   more concurrent operations need to occur, the more efficient coroutines 
   become in comparison to threads. 
 - faster context switching results in faster communication between code
 - coroutines take less memory than threads 
 - the number of coroutines is not limited by the operating system
 - coroutines do not require system level calls to create 

`st::fiber`s are designed to run inside of each other in a cooperative fashion by allowing other `st::fiber`s to process messages after processing its own message.

`st::fiber`s have a parent and child relationship. That is, they are either running in a blocking fashion at the top level of a thread or they are running in a non-blocking fashion inside of another `st::fiber`. `st::fiber`s running as children of a parent `st::fiber` will suspend themselves to allow give a chance for their sibling `st::fiber`s to run. 

Multiple children can run on a single `st::fiber`. In fact, child `st::fiber`s can be parents to their own children, creating a family tree as extensive as desired.

The simple way to create a child `st::fiber` is by calling `st::fiber::launch()` on a pre-existing `st::fiber` that was created by a call to `st::fiber::thread()`. `st::fiber:launch()` takes a template `FUNCTOR` and optional constructor arguments in the same way as `st::fiber::thread()`. `st::fiber`s can hold shared pointers to one another and use each other's `st::fiber::send()` functions to communicate.

#### Example 8:
```
// in main.h
#include <sthread>

struct parent {
    enum op {
        say_hello,
        unblock_main,
    };

    parent(st::sptr<st::channel>);
    void operator()(st::sptr<st::message>);

    st::sptr<st::channel> m_main_ch;
    st::sptr<st::fiber> m_child;
};

struct child {
    enum op {
        say_goodbye
    };

    child(st::sptr<st::fiber> par);
    void operator()(st::sptr<st::message>);

    st::sptr<st::fiber> m_parent;
};

// in main.c
#include <iostream>
#include <string>
#include "main.h"

parent::parent(st::sptr<st::channel> main_ch) : m_main_ch(main_ch) { 
    // acquire the pointer to `st::fiber` running on this thread
    st::sptr<st::fiber> self = st::fiber::local_self(); 

    // launch the child fiber on the parent fiber
    m_child(self->launch<child>(self));
}

void parent::operator(st::sptr<st::message> msg) {
    switch(msg->id) {
        case op::say_hello:
            std::cout << "hello" << std::endl;
            // tell the child fiber to say goodbye
            m_child->send(child::op::say_goodbye);
            break;
        case op::unblock_main:
            // allow the program to exit
            m_main_ch->send(0);
            break;
    }
}

child::child(st::sptr<st::fiber> par) : m_parent(par) {
    // tell the parent fiber to say hello
    m_parent->send(parent::op::say_hello);
}

void child::operator(st::sptr<st::message> msg) {
    switch(msg->id) {
        case op::say_goodbye:
            std::cout << "goodbye" << std::endl;
            m_parent->send(parent::op::unblock_main);
            break;
    }
}

int main() {
    st::sptr<st::channel> chan;
    st::sptr<st::message> msg;
    st::sptr<st::fiber> par = st::fiber::thread<parent>(chan);
    chan->recv(msg); // block until parent sends a message
    return 0;
}

```

Terminal output might be:
```
$./a.out 
hello 
goodbye
```

The only concern a user may have for scheduling is to know that parent `st::fiber`s are of an implicit higher priority to handle their own messages. As simple solution to create evenly balanced priorities between all working `st::fiber`s is to have the parent thread launched with `st::fiber::thread()` be created with the default `FUNCTOR` (`st::fiber::processor`) which itself will process no messages except to handle scheduling of other code:
``` 
// call `st::fiber::thread` with empty template to use default `st::fiber::processor`
st::sptr<st::fiber> processor = st::fiber::thread<>();
processor->launch<ChildFiber1>();
processor->launch<ChildFiber2>();
// etc...
```

[Back To Top](#simple-threading-and-communication)

### Object Lifecycles
In looping `st::channel::recv()` operations `st::channel::shutdown()` can be manually called to force all operations to cease on the `st::channel` (operations will return `false`). The default behavior for `st::channel::shutdown()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. 

This is the default behavior of several objects which use `st::channel` internally:
- `st::channel::shutdown(/* default true */)`
- `st::fiber::shutdown(/* default true */)`

Alternatively, the user can call said functions with explicit `false` to immediately end all operations on the channel:
- `st::channel::shutdown(false)`
- `st::fiber::shutdown(false)`

NOTE: When an closable/shutdownable object goes out of scope (no more `st::sptr` for that object instance exists), the object will be shutdown with default behavior (if the object is not already shutdown).

#### Example 9:
```
#include <iostream>
#include <string>
#include <sthread>

void looping_recv(st::sptr<st::channel> ch) {
    st::sptr<st::message> msg;

    while(ch->recv(msg)) {
        std::string s;
        if(msg->data.copy_to(s)) {
            std::cout << s << std::endl;
        }
    }

    std::cout << "thread done" << std::endl;
}

int main() {
    st::sptr<st::channel> my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);
    st::sptr<st::message> msg;

    my_channel->send(0, "You say goodbye");
    my_channel->send(0, "And I say hello");

    my_channel->shutdown(); // end thread looping 
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

[Back To Top](#simple-threading-and-communication)
