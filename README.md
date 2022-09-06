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

[Object Lifecycles](#object-lifecycles)

#### Extended Usage:
[Running Functions On Fibers](#running-functions-on-fibers)

[Running Fibers On Fibers](#running-fibers-on-fibers)

[Fiber Trees](#fiber-trees)

[Managing Groups Of Fibers](#managing-groups-of-fibers)

[Creating A Pool Of Worker Fibers](#creating-a-pool-of-worker-fibers)

## Purpose 
This header only library seeks to easily setup useful concurrency with a simple API.

The main thread-like object provided by the library is `st::fiber`, an object which can manage a system thread and process incoming messages.

Instead of functions `st::fiber`s execute c++ functors. A functor is a class which has a function call overload allowing you to execute the object like a function, IE:
```
struct MyClass {
    void operator()(st::message msg) { /* ... */ }
};
```

Functors (as used by this library) have several advantages over raw functions. 
- The thread's message receive loop is managed by the library 
- The thread's lifecycle is managed by the library 
- Sending messages to the thread is provided by the library 
- Functors allow for inheritance
- Functors allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
- Functors allow for class method definitions, instead of forcing the user to rely on lambdas, local objects or global namespace functions if further function calls are desired.
- Initialization (constructor), runtime execution (`void operator()(st::message`), and deinitialization (destructor) are broken in to separate functions, which I think makes them more readable. A thread running only a raw function requires everything be managed within that function. 

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
- Install the library and include the header `sthread` or `simple_thread.hpp`
- Create a class or struct with `void operator()(st::message)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::fiber::thread<YourClassNameHere>()`
- Trigger user class's `void operator()(st::message)` via `st::fiber::send(/* ... */)` 

#### Example 1
```
#include <iostream>
#include <sthread>

struct MyClass {
    enum op {
        hello,
        world
    };

    void operator()(st::message msg) {
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

[Back To Top](#simple-threading-and-communication)

### Message Payload Data 
Messages store their (optional) payload data in an object called `st::data`.
The message's data object is a member also called `data` (`st::message::data`).

Message data can be of any type and can be copied to argument `T t` with `st::data::copy_to<T>(T&& t)` or rvalue swapped with `st::message::move_data_to<T>(T&& t)`. 

`st::data::copy_to<T>(T&& t)` and `st::data::move_to<T>(T&& t)` will return `true` only if the stored payload type matches type `T`, otherwise it returns `false`. 

#### Example 2
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    void operator()(st::message msg) {
        switch(msg.id()) {
            case op::print:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << s << std::endl;
                }
                break;
            }
        }
    }
};

int main() {
    st::fiber my_thread = st::fiber::thread<MyClass>();

    my_thread.send(MyClass::op::print, "hello again");
}
```

Terminal output might be:
```
$./a.out
hello again
```

The code that `st::fiber::thread()`es launches a fiber is responsible for keeping a copy of the resulting `st::fiber`, otherwise the launched `st::fiber` may randomly stop executing because it will go out of scope.

[Back To Top](#simple-threading-and-communication)

### Send Operations
Several classes in this library support the ability to send messages:
- `st::channel::send(/* ... *)`
- `st::fiber::send(/* ... *)`
- `st::weave::send(/* ... *)`

Arguments passed to `send(/* ... */)` are subsequently passed to `st::message st::message::make(/* ... */)` before the resulting `st::message` is passed to its destination thread and functor. The summary of the 3 basic overloads of `st::message st::message::make(/* ... */)` are:
- `st::message st::message st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id()`.
- `st::message st::message st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T`. The will be stored in the message as a type erased `st::data`, accessible via `st::data& st::message::data()`. The stored value `T` can be copied out of the `st::data` object a with a call to `bool st::data::copy_to(T& t)` or moved out of the message with `bool st:data::move_to(T& t)`. Said functions will return false if their argument `T` does not match the type `T` originally stored in the `st::data`.
- `st::message st::message::make(st::message)`: Returns its argument immediately with no changes

[Back To Top](#simple-threading-and-communication)

### Type Checking
Payload `st::data` types can also be checked with `st::data::is<T>()` (returns `true` if types match, else `false`) which is useful if a message's data might contain several different potential types. 

NOTE: `st::data` can store a payload of any type. However, it does provide one special exception when passed explicit c-style `char*` strings, where it will automatically convert the argument `char*` into a `std::string` to protect the user against unexpected behavior. However, this means the the user must use `std::string` type when trying to copy or move the data back out. If this behavior is not desired, the user will have to wrap their `char*` in some other object.

Some classes support a similar method also named `is<T>()`:
- `st::fiber::is<T>()` // compares against the type of the fiber's `FUNCTOR`

#### Example 3
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    void operator()(st::message msg) {
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

    std::string s("hello ");
    my_fiber.send(MyClass::op::print, s);
    int i = 1;
    my_fiber.send(MyClass::op::print, i);
    s = " more time\n";
    my_fiber.send(MyClass::op::print, s);
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
```

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

    void operator()(st::message msg) { }

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

### Sending Messages To Standard Threads With Channels
The object that `st::fiber`s uses for communication in their `send()` methods is called `st::channel`. `st::channel`s can be created and used outside of `st::fiber` objects if desired. This allows the user, for example, to send messages to threads which were not launched with `st::fiber::thread()`.

#### Example 5
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    MyClass(st::channel ch) { 
        ch.send("forward this string to main");
    }

    void operator()(st::message msg) { }
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

### Dealing With Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid 
calling functions which will block for indeterminate periods within an `st::fiber`. If the user needs to call such a function, a simple solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a new thread, then `send()` the result back to the `st::fiber` when the call completes.

#### Example 6
```
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sthread>

std::string blocking_function(st::fiber origin) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    origin.send(op::print, "that's all folks!");
    origin.send(op::unblock_main);
};

struct MyFunctor {
    MyFunctor(st::channel main_ch) : m_main_ch(main_ch) { }

    enum op {
        call_blocker,
        print,
        unblock_main
    };

    void operator()(st::message msg) {
        switch(msg.id()) {
            case op::call_blocker:
            {
                // acquire a copy of the running `st::fiber` shared pointer
                st::fiber self = st::fiber::local_self();
               
                // use `std::async` to execute our lambda code on a new thread
                std::async(blocking_function, self);
                break;
            }
            case op::print:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << s << std::endl;
                }
                break;
            }
            case op::unblock_main:
                m_main_ch.send(0); // unblock main
                break;
        }
    }

    st::channel m_main_ch;
}

int main() {
    st::channel ch = st::channel::make();
    st::message msg;
    st::fiber my_fiber = st::fiber::thread<MyFunctor>(ch);
    my_fiber.send(MyFunctor::op::call_blocker);
    my_fiber.send(MyFunctor::op::print, "1");
    my_fiber.send(MyFunctor::op::print, "2");
    my_fiber.send(MyFunctor::op::print, "3");
    ch.recv(msg); // block so program doesn't end before our functions can run
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

### Object Lifecycles
Many objects in this library are actually shared pointers to some shared context, whose context needs to be allocated with a static call to their respective `make()`, `thread()` or `launch()` functions. Objects in this category include:
`st::message`
`st::channel`
`st::fiber`
`st::weave`

The user can check if these objects contain an allocated shared context with their `bool` conversion. This is easiest to do by using the object as the argument to an `if()` statement. Given an `st::fiber` named 'my_fib':
```
if(my_fib) {
    // my_fib is allocated
} else {
    // my_fib is not allocated
}
```

Attempting to use API of these objects when they are *NOT* allocated (other than static allocation functions or the `bool` conversion) will typically raise an exception.

When the last object holding a copy of some shared context goes out of scope, that object will be neatly shutdown and be destroyed. As such, the user is responsible for keeping copies of the above objects when they are created with an allocator function (`make()`, `thread()` or `launch()`), otherwise they may unexpectedly shut down.

In some cases, objects shared context can be shutdown early with a call to their `shutdown()` API. Objects with this API:
`st::channel`
`st::fiber`

In `st::channel`'s case, blocking `st::channel::recv()` operations can be stopped and made to return `false` by calling `st::channel::shutdown()`. `st::fiber` uses a `st::channel` internally for receiving `st::message`s, and it uses this behavior in order to determine when it should stop processing messages and go out of scope.

The default behavior for `st::channel::shutdown()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. Alternatively, the user can call `st::channel::shutdown(false)`/`st::fiber::shutdown(false)` to immediately end all operations on the `st::channel`/`st::fiber`.

#### Example 7
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

    std::cout << "thread done" << std::endl;
}

int main() {
    st::channel my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);

    my_channel.send(0, "You say goodbye");
    my_channel.send(0, "And I say hello");
    my_channel.shutdown(); // end thread looping 
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

### Running Functions on Fibers 
`st::fiber`s provide the ability to enqueue arbitrary code for asynchronous execution with `st::fiber::schedule(...)` API. Any `st::fiber` can be used for this purpose, though the default `st::fiber::thread<>()` and `st::fiber::launch<>()` `FUNCTOR` template type `st::fiber::processor` is often useful for generating worker threads dedicated to scheduling other code.

`st::fiber::schedule()` can accept a function, functor, or lambda function, alongside optional arguments, in a similar fashion to standard library features `std::async()` and `std::thread()`.

#### Extended Example 1
```
#include <iostream>
#include <string>
#include <sthread>

void print(std::string s) {
    std::cout << s << std::endl;
}

struct PrinterFunctor { 
    void operator()(std::string s) {
        std::cout << s << std::endl;
    }
};

int main() {
    // specifying `st::fiber::processor` inside the template `<>` is optional
    st::fiber my_processor = st::fiber::thread<>();
    my_processor.schedule(print, std::string("what a beautiful day"));
    my_processor.schedule(PrinterFunctor, std::string("looks like rain"));
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

`st::fiber`s can be created in two ways:
`static st::fiber st::fiber::thread<FUNCTOR>(As&& as...)`: launch a blocking root `st::fiber` on a new system thread
`st::fiber st::fiber::launch<FUNCTOR>(As&& as...)`: launch a cooperative `st::fiber` inside another `st::fiber`

`st::fiber`s are designed to run inside of each other in a cooperative fashion. That is, after processing their own message, the `st::fiber` will suspend itself and allowing other `st::fiber`s to process messages. 

The creator of an `st::fiber` is responsible for holding a copy of any `st::fiber` it creates to make sure that they stay in memory. 

*WARNING*: `st::fiber`s need to be careful to *NOT* hold a copy of themselves as a member variable, as this can create a memory leak. 

However, `st::fiber`s can hold copies of one another and use each other's `st::fiber::send()` functions to communicate. 

The simplest way to start executing many child `st::fiber`s is to create a new `st::fiber` with `st::fiber::thread` with the default `FUNCTOR` and `st::fiber::launch()` the rest of the `st::fiber`s on top of the first `st::fiber`. 

#### Extended Example 2
```
#include <iostream>
#include <string>
#include <list>
#include <sthread>

struct ChildFiber {
    enum op {
        say_name
    };
    
    struct Data {
        int name_id;
        std::list<st::fiber> fibs;
        st::channel done_ch;
    };

    void operator()(st::message msg) {
        switch(msg.id()) {
            case op::say_name:
            {
                Data d;
                if(msg.data().copy_to(d)) {
                    d.fibs.pop_front();
                    std::cout << "My name is ChildFiber_" << d.name_id << std::endl;

                    if(d.fibs.size()) {
                        ++(d.id);
                        d.fibs.front().send(op::say_name, d);
                    } else {
                        d.done_ch.send(0);
                    }
                }
                break;
            }
        }
    }
};

int main() {
    // create the root fiber with default template type
    st::fiber root = st::fiber::thread<>();

    // create a channel to block on while fibers are running
    st::channel ch = st::channel::make();

    // launch child fibers within the root fiber
    std::list<st::fiber> fibs{ 
        root.launch<ChildFiber>(),
        root.launch<ChildFiber>(),
        root.launch<ChildFiber>(),
        root.launch<ChildFiber>(),
        root.launch<ChildFiber>()
    };

    fibs.front().send(ChildFiber::op::say_name, ChildFiber::Data{ 0, fibs, ch })

    st::message msg;
    ch.recv(msg); // block until fibers are done processing
    return 0;
}
```

Terminal output might be:
```
$./a.out 
My name is MyFiber_0
My name is MyFiber_1
My name is MyFiber_2
My name is MyFiber_3
My name is MyFiber_4
```

[Back To Top](#simple-threading-and-communication) 

### Fiber Trees
`st::fiber`s have a parent and child relationship. That is, they are either running in a blocking fashion at the top level of a system thread or they are running in a non-blocking fashion inside of another `st::fiber`. `st::fiber`s running as children of a parent `st::fiber` will suspend themselves to allow for their sibling `st::fiber`s to run after they have processed an `st::message` or executed some `st::fiber::schedule()`ed code. 

Multiple children can run on a single parent `st::fiber`. In fact, child `st::fiber`s can be parents to their own children, creating a multi-tiered family tree.

`st::fiber`s have some awarenes of their position in the current system thread's family tree. Each fiber has the following API:
`st::fiber::parent()`: return a copy of the parent `st::fiber` (or the root `st::fiber` if there is no parent)
`st::fiber::root()`: return a copy of the root `st::fiber` at the top of the family tree 

Additionally, the `FUNCTOR` executing inside an `st::fiber` can get the same information by calling these static functions:
`st::fiber::local::self()`: return a copy of the `st::fiber` running on the calling thread
`st::fiber::local::parent()`: return a copy of the parent `st::fiber` (or the root `st::fiber` if there is no parent) of the `st::fiber` running on the calling thread
`st::fiber::local::root()`: return a copy of the root `st::fiber` at the top of the family tree on the current thread

#### Extended Example 3
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
        void operator()(st::message);

        st::channel m_main_ch;
        st::fiber m_child;
    };

    struct child {
        void operator()(st::message);
    };
};

// in main.c
#include <iostream>
#include <string>
#include "main.h"

fibers::parent::parent(st::channel main_ch) : 
    m_main_ch(main_ch),
    m_child(st::fiber::local::self().launch<child>()); // launch the child fiber on the current fiber
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
    st::fiber::local::parent().send(op::say_hello);
}

void fibers::child::operator(st::message msg) {
    switch(msg.id()) {
        case op::say_hi:
            std::cout << "hi" << std::endl;
            st::fiber::local::parent().send(op::say_goodbye);
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

An important note for scheduling is that parent `st::fiber`s are of an implicit higher priority than their children when handling messages and scheduled code. A simple solution to create evenly balanced priorities between all working `st::fiber`s is to have the parent thread launched with `st::fiber::thread()` using the default `FUNCTOR` (`st::fiber::processor`) which itself will process no messages except to handle scheduling of other code:
``` 
// call `st::fiber::thread` with empty template to use default `st::fiber::processor`
st::fiber processor = st::fiber::thread<>();
st::fiber ch1 = processor.launch<ChildFiber1>();
st::fiber ch2 = processor.launch<ChildFiber2>();
// etc...
```

[Back To Top](#simple-threading-and-communication) 

### Managing Groups of Fibers
The object `st::weave` exists to manage the lifecycle of multiple fibers. An `st::weave` represents a private shared context in a similar way to an `st::fiber`. When the last `st::weave` representing a shared context goes out of scope all the `st::fiber`s managed by that `st::weave` will be `st::fiber::shutdown()` OR if the `st::weave::shutdown()` variant function is called.

`st::weave` implements lifecycle API:
`bool st::weave::running()`: returns `true` if managing any running fibers, else `false`
`void st::weave::shutdown()`: shutdown any managed fibers with default behavior
`void st::weave::shutdown(bool process_remaining_messages)`: shutdown any managed fibers with specified behavior

`st::weave` implements this unique API:
`st::weave st::weave::make(... fibers ...)`: allocate a `st::weave` managing argument fibers 
`st::weave st::weave::threadpool<FUNCTOR>(optional_count, args...)`: allocate a `st::weave` managing `optional_count` of `st::fiber`s implementing `FUNCTOR` constructed with `args...`. Default usage `st::weave:;threadpool<>()` can be used to allocate a maximum processing throughput weave.
`void st::weave::append(... fibers ...)`: append additional `st::fiber`s to an existing `st::weave`
`std::vector<st::fiber> st::weave::fibers()`: return the `std::vector<st::fiber>` of `st::fiber`s stored at the given index
`std::size_t st::weave::count()`: return a count of managed `st::fibers`
`st::fiber st::weave::operator[](index)`: return the `st::fiber` stored at the given index
`st::fiber st::weave::select()`: return a `st::fiber` with a relatively light workload

`st::weave::select()` will return a managed `st::fiber` with a relatively light workload. This is useful for `st::fiber::schedule()`ing arbitrary code over a number of worker `st::fiber`s intended for generic code processing. IE, creating an `st::weave` managing a group of `st::fiber`s launched with `st::fiber::thread<>()` is an easy way to set up a threadpool.

#### Extended Example 4
```
#include <iostream>
#include <string>
#include <sthread>

struct PrintIdFiber {
    PrintIdFiber() {
        std::cout << "PrintIdFiber[" << this << "]:" << std::this_thread::get_id() << std::endl;
    }

    void operator()(st::message msg) { }
};

void UnblockMain(st::channel ch) {
    ch.send(0);
}

int main() {
    st::channel ch = st::channel::make();
    st::fiber root = st::fiber::thread<>();

    // hold a copy of all launched fibers
    st::weave fibers = st::weave::make(
        root,
        root.launch<PrintIdFiber>()
    );

    // append more fibers to the weave 
    fibers.append(
        root.launch<PrintIdFiber>(),
        root.launch<PrintIdFiber>()
    );
        
    root.schedule(UnblockMain, ch);

    st::message msg;
    ch.recv(msg); // block until UnblockMain is called
    return 0; // weave shuts down fibers when it goes out of scope
}
```

Terminal output might be:
```
$./a.out 
```

[Back To Top](#simple-threading-and-communication) 

### Creating A Pool Of Worker Fibers 
A common concurrency usecase is creating a group of system threads where arbitrary code code can be executed in an asynchronous fashion. The static function `st::weave::threadpool<FUNCTOR>(count, ... optional FUNCTOR constructor args ...)` is provided for this purpose.

`st::weave::threadpool<FUNCTOR>(count, ...args...)` calls `st::fiber::thread<FUNCTOR>(...args...)` `count` times and returns the collection of launched fibers as an `st::weave`. 

The default `FUNCTOR` for `st::weave::threadpool()` is `st::fiber::processor`, which is a FUNCTOR which does not process messages with its `void operator(st::message)` overload, and is intended to be used *only* for `st::fiber::schedule(...)`ing code to be asynchronously executed.

If no arguments are provided to the function, a default count of fibers is selected which attempts to launch a count of `st::fiber`s equal to the number of concurrently executable system threads. This count is typically equal to the count of processor cores on some hardware.

Therefore, a simple way to create a multipurpose, (theoretically) maximally efficient (in terms of CPU throughput) threadpool is to use all default options: `st::weave::threadpool<>()`.

#### Extended Example 5
```
#include <iostream>
#include <string>
#include <sthread>
#include <thread>

void foo() {
    std::cout << "system thread: " << std::this_thread::get_id() << std::endl;
}

int main() {
    st::weave pool = st::weave::threadpool<>(); // launch a default number of worker threads
    std::cout << "worker thread count: " << pool.size() << std::endl;

    for(int i=0; i<pool.size(); ++i) {
        pool.select().schedule(foo);
    }

    return 0;
}

```

Terminal output might be:
```
$./a.out 
```

[Back To Top](#simple-threading-and-communication) 
