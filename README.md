# Simple Threading and Communication
## Quick Links

This library makes use of inherited functionality via interfaces. If code lacks documentation, look at the interfaces for more information.

[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/simple_thread_tst.cpp)

#### Basic Usage:
[Creating Threads from Objects](#creating-threads-from-objects)

[Send Operations](#send-operations) 

[Type Checking](#type-checking)

[Fiber Constructor Arguments](#fiber-constructor-arguments)

[Sending Messages Between Threads with Channels](#sending-messages-between-threads-with-channels)

[Sending Messages Between Fibers](#sending-messages-between-fibers)

[Sending Messages Between Fibers Using Channels](#sending-messages-between-fibers-using-channels)

[Sending Messages Between Fibers Using Callbacks](#sending-messages-between-fibers-using-channels)

[Dealing with Blocking Functions](#dealing-with-blocking-functions)

[Object Lifecycles](#object-lifecycles)

#### Advanced Usage:
[Running Functions on Fibers](#running-functions-on-fibers)

[Running Fibers on Fibers](#running-fibers-on-fibers)

[Fiber Trees](#fiber-trees)

[Creating a Pool of Worker Fibers](#creating-a-pool-of-worker-fibers)

## Purpose 
This header only library seeks to easily setup useful concurrency with a simple API.

The main thread-like object provided by the library is `st::fiber`, an object which can manage a system thread and process incoming messages.

Instead of functions `st::fiber`s execute an object's `recv()` method:
```
struct MyClass {
    void recv(st::message msg) { /* ... */ }
};
```

Thread objects (as used by this library) have several advantages over raw functions. 
- The thread's message receive loop is managed by the library 
- The thread's lifecycle is managed by the library 
- Sending messages to the thread is provided by the library 
- Objects allow for inheritance
- Objectss allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
- Objects allow for class method definitions, instead of forcing the user to rely on lambdas, local objects or global namespace functions if further function calls are desired.
- Initialization (constructor), runtime execution (`void recv(st::message`), and deinitialization (destructor) are broken in to separate functions, which I think makes them more readable. A thread running only a raw function requires everything be managed within that function or within child objects managed by that function. 

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
### Creating Threads from Objects
- Install the library and include the header `sthread` or `simple_thread.hpp`
- Create a class or struct and implement method `void recv(st::message)` to handle received messages 
- Define some enum to distinguish different messages 
- Launch your thread with `st::fiber::thread<YourClassNameHere>()`
- Trigger user class's `void recv(st::message)` via `st::fiber::send(/* ... */)` 
- User object can distinguish `st::message`s by their unsigned integer id (possibly representing an enumeration) with a call to `st::message::id()`. 

#### Example 1
```
#include <iostream>
#include <sthread>

struct MyClass {
    enum op {
        hello,
        world
    };

    void recv(st::message msg) {
        switch(msg.id()) {
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
    st::fiber my_thread = st::fiber::thread<MyClass>();
    my_thread.send(MyClass::op::hello);
    my_thread.send(MyClass::op::world);
}
```

Terminal output might be:
```
$./a.out
hello
world
```

The code that calls `st::fiber::thread()` to create a fiber is responsible for keeping a copy of the resulting `st::fiber` object. Otherwise the launched `st::fiber` may stop executing because it will go out of scope.

[Back To Top](#simple-threading-and-communication)

### Send Operations
Several classes in this library support the ability to send messages:
- `st::channel::send(/* ... *)`
- `st::fiber::send(/* ... *)`

Arguments passed to `send(/* ... */)` are subsequently passed to `st::message st::message::make(/* ... */)` before the resulting `st::message` is passed to its destination thread and object. The summary of the 3 basic overloads of `st::message st::message::make(/* ... */)` are:
- `st::message st::message st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id()`.
- `st::message st::message st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T`. The will be stored in the message as a type erased `st::data`. 
- `st::message st::message::make(st::message)`: Returns its argument immediately with no changes 

`st::message`s have 2 important methods:
`std::size_t st::message::id()`: Return the unsigned integer id value stored in the messsage
`st::data& st::message::data()`: Return a reference to the payload `st::data` stored in the message

`st::data()` can story any type and that data can be copied to an argument of templated type `T` with `st::data::copy_to(T& t)` or rvalue swapped with `st::data::move_to(T& t)`. Said functions will return `true` if their argument `T` matches the type `T` originally stored in the `st::data`, otherwise they will return `false`.

#### Example 2
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::print:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << s << std::endl;
                } else {
                    std::cout << "message data was not a string" << std::endl;
                }
                break;
            }
        }
    }
};

int main() {
    st::fiber my_thread = st::fiber::thread<MyClass>();

    my_thread.send(MyClass::op::print, std::string("hello again"));
    my_thread.send(MyClass::op::print, 14);
}
```

Terminal output might be:
```
$./a.out
hello again
message data was not a string
```

[Back To Top](#simple-threading-and-communication)

### Type Checking
Payload `st::data` types can also be checked with `st::data::is<T>()` (returns `true` if types match, else `false`) which is useful if a message's data might contain several different potential types. 

WARNING: `st::data` can store a payload of any type. However, this behavior can be confusing with regard to c-style `char*` strings. c-style strings are just pointers to memory, and the user is responsible for ensuring that said memory is accessible outside of the scope when the message is sent. Typically, it is safe to send c-style strings with a hardcoded value, as this is normally stored in the program's data. However, local stack arrays of characters, or, even worse, allocated c-strings must be carefully considered when sending over a message. 

A simple workaround to these headaches is to encapsulate c-style strings within a c++ `std::string`.

#### Example 3
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::print:
                if(msg.data().is<std::string>()) {
                    std::string s;
                    msg.data().copy_to(s);
                    std::cout << s;
                } else if(msg.data().is<int>()) {
                    int i = 0;
                    msg.data().copy_to(i);
                    std::cout << i;
                }
                break;
        }
    }
};

int main() {
    st::fiber my_fiber = st::fiber::thread<MyClass>();

    my_fiber.send(MyClass::op::print, std::string("hello"));
    my_fiber.send(MyClass::op::print, 1);
    my_fiber.send(MyClass::op::print, std::string(" more time\n"));
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
```

[Back To Top](#simple-threading-and-communication)

### Fiber Constructor Arguments
`st::fiber`s can be passed `OBJECT` constructor arguments `as...` in `st::fiber::thread<OBJECT>(As&& as...)`. The `OBJECT` class will be created on the new thread and destroyed before said thread ends.

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

    void recv(st::message msg) { }

    std::string m_destructor_string;
};

int main() {
    std::cout << std::this_thread::get_id() << ":" <<  "parent thread started" << std::endl;
    st::fiber wkr = st::fiber::thread<MyClass>("hello", "goodbye");
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

### Sending Messages Between Threads with Channels
The object that `st::fiber`s uses for communication in their `send()` methods is called `st::channel`. `st::channel`s can be created and used outside of `st::fiber` objects if desired. This allows the user to send messages to threads which were not launched with `st::fiber::thread()`.

#### Example 5
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    MyClass(st::channel ch) { 
        ch.send(std::string("forward this string to main"));
    }

    void recv(st::message msg) { }
};

int main() {
    st::channel my_channel = st::channel::make();
    st::fiber my_thread = st::fiber::thread<MyClass>(my_channel);
    
    st::message msg;
    my_channel.recv(msg); // main blocks to listen on the channel

    std::string s;
    if(msg.data().copy_to(s)) {
        std::cout << s << std::endl;
    }
}
```

Terminal output might be:
```
$./a.out 
forward this string to main
```

[Back To Top](#simple-threading-and-communication)

### Sending Messages Between Fibers
`st::fiber`s can send messages to other `st::fiber`s by using `st::fiber::send(...)` methods. Because of this `st::fiber`s may sometimes need to send a copy of themselves to other fibers. A copy of the `st::fiber` a user's `OBJECT` is running inside of can be acquired by calling static function `st::fiber::self()`. 

Warning: It is important that an `st::fiber` does *NOT* store a copy of itself as an `OBJECT` member variable, as this can cause a memory leak.

Due to `st::message::id()` returning an unsigned integer `st::fiber`s can enjoy all the advantages and pain of using enumerations to communicate. 

#### Example 6
```
struct FiberA {
    enum op {
        value_read_req,
        value_read_resp,
        unused // should always be the last enum value in FiberA::op
    };

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::value_read_req:
            {
                st::fiber f;
                if(msg.data().copy_to(f)) {
                    f.send(op::value_read_resp, m_value);
                }
                break;
            }
        }
    }

    int m_value = 13;
};

struct FiberB {
    FiberB(st::channel ch) : m_main_ch(ch) { }

    // can extend enumeration FiberA::op with clever use of final enumeration value
    enum op {
        set_fiber_a = FiberA::op::unused
    };

    void recv(st::message msg) {
        switch(msg.id()) {
            case FiberA::op::value_read_resp:
            {
                int value;
                if(msg.data.copy_to(value)) {
                    std::cout << "received " << value << " from FiberA" << std::endl;
                }

                // unblock main() to allow the program to exit
                m_main_ch.send(0);
                break;
            }
            case FiberB::op::set_fiber_a:
                if(msg.data().copy_to(m_fib_a)) {
                    // send a value read request to FiberA, with a copy of FiberB as the message payload
                    m_fib_a.send(FiberA::op::value_read_req, st::fiber::self());
                }
                break;
        }
    }

    st::channel m_main_ch;
    st::fiber m_fib_a;
};

int main() {
    st::channel ch;
    st::fiber fib_a = st::fiber::thread<FiberA>();
    st::fiber fib_b = st::fiber::thread<FiberB>(ch);

    // using FiberB's extended enumeration API, can set FiberB value after construction
    fib_b.send(FiberB::op::set_fiber_a, fib_a);

    // block until fibers are done processing
    st::message msg;
    ch.recv(msg);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
received 13 from FiberA
```

[Back To Top](#simple-threading-and-communication)

### Dealing with Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid 
calling functions which will block for indeterminate periods within an `st::fiber`. If the user needs to call such a function, a simple solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a dedicated system thread, then `send()` the result back to the `st::fiber` when the call completes.

#### Example 7
```
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sthread>

std::string slow_function() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return std::string("that's all folks!");
};

struct MyClass {
    MyClass(st::channel main_ch) : m_main_ch(main_ch) { }

    enum op {
        print,
        slow_function,
        slow_result
    };

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::print:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << s << std::endl;
                }
                break;
            }
            case op::slow_function:
                // use `std::async` to execute our  code on its own thread
                st::fiber::self().async(op::slow_result, slow_function);
                break;
            case op::slow_result:
                st::fiber::self().send(op::print, msg);
                main_ch.send(0); // unblock main
                break;
        }
    }

    st::channel m_main_ch;
    int cnt = 0;
}

int main() {
    st::channel ch = st::channel::make();
    st::fiber my_fiber = st::fiber::thread<MyClass>(ch);
    my_fiber.send(MyClass::op::slow_function);
    my_fiber.send(MyClass::op::print, std::string("1"));
    my_fiber.send(MyClass::op::print, std::string("2"));
    my_fiber.send(MyClass::op::print, std::string("3"));

    st::message msg;
    ch.recv(msg); // block so program doesn't end before our functions can run
    return 0; // return causing my_fiber to process remaining messages then exit
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

### Object Lifecycles
Many objects in this library are actually shared pointers to some shared context, whose context needs to be allocated with a static call to their respective `make()`, `thread`, `threadpool()` or `coroutine()` functions. Objects in this category include:
- `st::message`
- `st::channel`
- `st::fiber`

The user can check if these objects contain an allocated shared context with their `bool` conversion. This is easiest to do by using the object as the argument to an `if()` statement. Given an `st::fiber` named 'my_fib':
```
if(my_fib) {
    // my_fib is allocated
} else {
    // my_fib is not allocated
}
```

Attempting to use API of these objects when they are *NOT* allocated (other than static allocation functions or the `bool` conversion) will typically raise an exception.

When the last object holding a copy of some shared context goes out of scope, that object will be neatly shutdown and be destroyed. As such, the user is responsible for keeping copies of the above objects when they are created with an allocator function (`make()`, `thread()`, `threadpool()`, or `coroutine()`), otherwise they may unexpectedly shut down.

In some cases, object's shared context can be shutdown early with a call to `st::channel::close()` or `st::fiber::shutdown()` API respectively.

In `st::channel`'s case, blocking `st::channel::recv()` operations can be stopped and made to return `false` by calling `st::channel::shutdown()`. `st::fiber` uses an `st::channel` internally for receiving `st::message`s, and it uses this behavior in order to determine when it should stop processing messages and go out of scope.

The default behavior for `st::channel::shutdown()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. Alternatively, the user can call `st::channel::close(false)`/`st::fiber::shutdown(false)` to immediately end all operations on the `st::channel`/`st::fiber`.

The user can call `bool running()` on each of these objects to check if an object has had `shutdown()` called on it. `running()` will return `true` if `shutdown()` has *NOT* been called, else it will return `false`. 

#### Example 8
```
#include <iostream>
#include <string>
#include <sthread>

void looping_recv(st::channel ch) {
    st::message msg;

    while(ch.recv(msg)) {
        std::string s;
        if(msg.data().copy_to(s)) {
            std::cout << s << std::endl;
        }
    }
}

int main() {
    st::channel my_channel;

    if(!my_channel) {
        std::cout << "my_channel is not yet allocated" << std::endl;
    }

    my_channel = st::channel::make();

    if(my_channel) {
        std::cout << "my_channel is allocated" << std::endl;
    }

    std::thread my_thread(looping_recv, my_channel);

    if(my_channel.running()) {
        std::cout << "channel is running" << std::endl;
    }

    my_channel.send(0, "you say goodbye");
    my_channel.send(0, "and I say hello");
    my_channel.shutdown(); // end thread looping 
    my_thread.join(); // join thread

    if(!my_channel.running()) {
        std::cout << "channel is shutdown" << std::endl;
    }
}
```

Terminal output might be:
```
$./a.out 
my_channel is not yet allocated
my_channel is allocated
channel is running
you say goodbye
and I say hello
channel is shutdown
```

[Back To Top](#simple-threading-and-communication)

#### Advanced Usage:
It should be noted that the following features are no longer "simple" as implied 
by this project's title. Most users will be able to get by with basic usage.

However, the advanced features detailed below help solve a variety of common,
but difficult, concurrency usecases through 

### Running Functions on Fibers 
`st::fiber`s provide the ability to enqueue arbitrary code for asynchronous execution with `st::fiber::schedule(...)` API. Any `st::fiber` can be used for this purpose, though the default `st::fiber::thread<>()` and `st::fiber::coroutine<>()` `OBJECT` template type `st::fiber::processor` is useful for generating worker `st::fiber`s dedicated to scheduling other code.

`st::fiber::schedule()` can accept a function, functor, or lambda function, alongside optional arguments, in a similar fashion to standard library features `std::async()` and `std::thread()`.

#### Advanced Example 1
```
#include <iostream>
#include <string>
#include <sthread>

void print(std::string s) {
    std::cout << s << std::endl;
}

struct PrinterFunctor { 
    void recv(std::string s) {
        std::cout << s << std::endl;
    }
};

int main() {
    // specifying `st::fiber::processor` inside the template `<>` is optional
    st::fiber my_processor = st::fiber::thread<>();
    my_processor.schedule(print, std::string("what a beautiful day"));
    my_processor.schedule(PrintFunctor, std::string("looks like rain"));
    my_processor.schedule([]{ std::cout << "what a beautiful sunset" << std::endl; });
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

### Running Fibers on Fibers 
`st::fiber` is actually an example of a stackless coroutine. According to wikipedia: 
```
Coroutines are computer program components that generalize subroutines for 
non-preemptive multitasking, by allowing execution to be suspended and resumed.
```

Without going into to much detail, here are some advantages of coroutines compared to threads:
 - changing which coroutine is running by suspending its execution is 
   exponentially faster than changing which system thread is running. IE, the 
   more concurrent operations need to occur, the more efficient coroutines 
   become in comparison to system threads. 
 - faster context switching results in faster communication between code
 - coroutines take less memory than threads 
 - the number of coroutines is not limited by the operating system
 - coroutines do not require system level calls to create 

There are three methods of creating allocated `st::fiber`s:
- `static st::fiber st::fiber::thread<OBJECT>(As&& as...)`: launch a root `st::fiber` on a new system thread
- `static st::fiber st::fiber::threadpool<OBJECT>(count, As&& as...)`: launch a root `st::fiber` representing a `count` of worker `st::fiber`s running on their own threads
- `st::fiber st::fiber::coroutine<OBJECT>(As&& as...)`: launch a cooperative `st::fiber` inside another `st::fiber`

The user is responsible for keeping a copy of the `st::fiber`s returned by the above functions to keep them in memory.

`st::fiber`s are designed to run inside of each other in a cooperative fashion. When an `st::fiber` is launched in cooperative mode inside another `st::fiber` the child will be scheduled for processing `st::message`s. after processing their own `st::message`, the child `st::fiber` will suspend itself and allowing other `st::fiber`s to process `st::message`s. 

A simple way to start executing many child `st::fiber`s is to create a new `st::fiber` with `st::fiber::thread` with the default `OBJECT` `st::fiber::processor` and call `st::fiber::coroutine<...>(...)` many times to launch the rest of the `st::fiber`s on top of the first `st::fiber`.

`st::fiber`s can hold copies of other `st::fiber`s and use these copies' `st::fiber::send()` functions to communicate with each other. 

*WARNING*: `OBJECT`s running in an `st::fiber` need to be careful to *NOT* hold a copy of that `st::fiber` as a member variable, as this can create a memory leak. Instead, `st::fiber::self()` should be called from within the running `OBJECT` when accessing the `OBJECT`'s associated `st::fiber` is necessary.

#### Advanced Example 2
```
#include <iostream>
#include <string>
#include <list>
#include <sthread>

struct ChildFiber {
    enum op {
        say_name
    };
    
    ChildFiber(int name, 
               st::channel done_ch,
               st::fiber next=st::fiber()) : 
        m_name(name), m_done_ch(done_ch), m_next(next) 
    { }

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::say_name:
            {
                std::cout << "My name is ChildFiber" << m_name << std::endl;

                if(m_next) {
                    m_next.send(op::say_name);
                } else {
                    m_done_ch.send(0);
                }
                break;
            }
        }
    }

    int m_name;
    st::channel m_done_ch;
    st::fiber m_next; // next fiber in the list
};

int main() {
    // create the root fiber with default template type
    st::fiber root = st::fiber::thread<>();

    // create a channel to block on while fibers are running
    st::channel ch = st::channel::make();

    // Create an implicit singly linked list of fibers.
    st::fiber child2 = root.coroutine<ChildFiber>(2,ch);
    st::fiber child1 = root.coroutine<ChildFiber>(1,ch,child2);
    st::fiber child0 = root.coroutine<ChildFiber>(0,ch,child1);

    child0.send(ChildFiber::op::say_name);

    st::message msg;
    ch.recv(msg); // block until fibers are done processing
    return 0;
}
```

Terminal output might be:
```
$./a.out 
My name is ChildFiber0
My name is ChildFiber1
My name is ChildFiber2
```

[Back To Top](#simple-threading-and-communication) 

### Fiber Trees
`st::fiber`s have a parent and child relationship. That is, they are either running in a blocking fashion at the top level of a system thread or they are running in a non-blocking fashion inside of another `st::fiber`. 

Multiple children can run on a single parent `st::fiber`. In fact, child `st::fiber`s can be parents to their own children, creating a branching family tree.

`st::fiber`s have some awarenes of their position in the current system thread's family tree. Each fiber has the following API:
`st::fiber::parent()`: return a copy of the parent `st::fiber` (or the root `st::fiber` if there is no parent)
`st::fiber::root()`: return a copy of the root `st::fiber` at the top of the family tree 

Additionally, the `OBJECT` executing inside an `st::fiber` can get a copy of it's associated `st::fiber` by calling this static function:
`st::fiber::self()`: return a copy of the `st::fiber` currently running on the calling thread

`st::fiber`s running as children of a parent `st::fiber` will suspend themselves to allow for their sibling `st::fiber`s to run after they have processed an `st::message` or executed some `st::fiber::schedule()`ed code. 

#### Advanced Example 3
```
// in main.h
#include <sthread>

struct fibers {
    enum op {
        say_hello,
        say_hi,
        say_goodbye,
    };

    struct parent {
        parent(st::channel);
        void recv(st::message);

        st::channel m_main_ch;
        st::fiber m_child;
    };

    struct child {
        void recv(st::message);
    };
};

// in main.c
#include <iostream>
#include <string>
#include "main.h"

fibers::parent::parent(st::channel main_ch) : 
    m_main_ch(main_ch),
    m_child(st::fiber::self().coroutine<child>()); // launch the child fiber on the current fiber
{ }

void fibers::parent::operator(st::message msg) {
    switch(msg.id()) {
        case op::say_hello:
            std::cout << "hello" << std::endl;
            // tell the child fiber to say goodbye
            m_child.send(op::say_hi);
            break;
        case op::say_goodbye:
            std::cout << "goodbye" << std::endl;
            // allow the program to exit
            m_main_ch.send(0);
            break;
    }
}

fibers::child::child() {
    // tell the parent fiber to say hello
    st::fiber::self().parent().send(op::say_hello);
}

void fibers::child::operator(st::message msg) {
    switch(msg.id()) {
        case op::say_hi:
            std::cout << "hi" << std::endl;
            st::fiber::self().parent().send(op::say_goodbye);
            break;
    }
}

int main() {
    st::channel chan = st::channel::make();
    st::fiber par = st::fiber::thread<fibers::parent>(chan);
    st::message msg;
    chan.recv(msg); // block until parent sends a message to the main thread
    return 0;
}

```

Terminal output might be:
```
$./a.out 
hello
hi
goodbye
```

An important note is that parent `st::fiber`s are of an implicit higher priority than their children when handling messages and scheduled code. A simple solution to create evenly balanced priorities between all working `st::fiber`s is to have the parent thread launched with `st::fiber::thread()` use the default `OBJECT` (`st::fiber::processor`) which itself will process no messages except to handle scheduling of other code:
``` 
// call `st::fiber::thread` with empty template to use default `st::fiber::processor`
st::fiber proc = st::fiber::thread<>();
st::fiber ch1 = proc.coroutine<ChildFiber1>();
st::fiber ch2 = proc.coroutine<ChildFiber2>();
// etc...
```

[Back To Top](#simple-threading-and-communication) 

### Creating a Pool of Worker Fibers 
A common concurrency usecase is creating a group of worker system threads (a threadpool) that can execute (a potentially large quantity of) arbitrary code. The static function `st::fiber::threadpool<OBJECT>(count, ... optional OBJECT constructor args ...)` is provided for this purpose.

`st::fiber::threadpool<OBJECT>(count, ...args...)` allocates multiple `st::fiber`s running on dedicated system threads by calling `st::fiber::thread(...args...)` `count` times. The returned root `st::fiber` represents a collection of child system thread `st::fiber`s, all of which are listening to the root `st::fiber`'s internal `st::channel` for messages.

This means that any call to `st::fiber::send(...)` or `st::fiber::schedule(...)` on the returned root `st::fiber` will be evenly distributed among the child system threads for execution, potentially distributing work among multiple processor cores.

The default `OBJECT` for `st::fiber::threadpool<OBJECT>()` is `st::fiber::processor`, an `OBJECT` which does not process messages with its `void recv(st::message)` handler, and is instead intended to execute arbitrary code with calls to `st::fiber::schedule(...)`. Any user specified `OBJECT` can be used instead of `st::fiber::processor` if desired.

If no arguments are provided to `st::fiber::threadpool<OBJECT>()`, a default count of fibers is selected which attempts to launch a count of `st::fiber`s equal to the number of concurrently executable system threads. This count is typically equal to the count of processor cores on the running hardware, retrieved with a call to static function `st::fiber::concurrency()`.

Therefore, a simple way to create a multipurpose (and theoretically maximally efficient in terms of CPU throughput) threadpool is to use all default options: `st::fiber::threadpool<>()`.

#### Advanced Example 5
```
#include <iostream>
#include <string>
#include <sthread>
#include <thread>

void foo() {
    std::cout << "system thread: " << std::this_thread::get_id() << std::endl;
}

int main() {
    st::fiber pool = st::fiber::threadpool<>(); // launch a default number of worker threads

    for(int i=0; i<st::fiber::concurrency(); ++i) {
        pool.schedule(foo);
    }

    return 0;
}

```

Terminal output might be:
```
$./a.out 
```

[Back To Top](#simple-threading-and-communication) 
