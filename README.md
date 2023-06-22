# Simple Threading and Communication
## Quick Links

The overall design of code in this library relies heavily on virtual
interfaces to implement inherited behavior. Visit the documentation for 
information on interfaces and various other features not detailed in this README.
[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/)

#### Usage:

[Channel Send Operations](#send-operations) 

[A Closer Look at Channel Send Operations](#a-closer-look-at-channel-send-operations) 

[Message Data Payloads](#message-data-payloads)

[Payload Data Type Checking](#payload-data-type-checking)

[Object Lifecycles](#object-lifecycles)

[Closing Channels](#closing-channels)

[Abstracting Message Replies](#abstracting-message-senders)

[Dealing with Blocking Functions](#dealing-with-blocking-functions)

[Scheduling Functions on Threads](#scheduling-functions-on-threads)

## Purpose 
This library's purpose is to simplify setting up useful c++ threading, and to enable trivial inter-thread message passing of C++ objects. That is, message passing with support for proper C++ construction and destruction instead of blind `memcpy()` calls, allowing C++ objects to be passed around.

The library provides a handful of objects which support this goal:
- `st::channel`
- `st::message`
- `st::reply`

The core of the behavior of the library is tied to the `st::channel` object, which is responsible for asynchronously sending messages between senders and receivers. A receiver thread can use this object to make a simple message receive loop and process incoming `st::message`s.

Each of the above objects is actually a thinly wrapped shared pointer to some internal data. The internal data must be constructed by a call to that object's associated static `make()` method. This means that once these objects are constructed, they can be passed around efficiently like any other variable. In the case of `st::channel` all of its public methods are threadsafe.

## Requirements
- C++11 

## Git Submodules
This project uses Googletest as a submodule to build unit tests. If unit tests are needed try cloning this project with submodules:
- git clone --recurse-submodules https://github.com/durandaltheta/sthread

## Installation
- `cmake .`
- `make install`

If building on linux, may have to `sudo make install`.

## Build Unit Tests 
- `cmake .`
- `make sthread_tst`

`sthread_tst` binary will be placed in tst/ 

## Usage 
### Simple message passing 
All that is required to send/recv messages between threads is that each thread has a copy of a constructed `st::channel` object. Messages can be sent with a call to one of the 4 `st::message::send()` overloads:
- `st::channel::send(

Messages can be received by calling `st::channel::recv()`, but the simplest solution is to make use of `st::channel` iterators in a range-for loop.

#### Example 1
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

enum op {
    say, // print an std::string
    goodbye // print a goodbye message
};

st::channel ch = st::channel::make();

int childThread() {
    for(auto msg : ch) { // msg is an `st::message`
        switch(msg.id()) {
            case op::say: 
                {
                    std::string s;
                    if(msg.copy_to(s)) { 
                        std::cout << "child thread says: " << s << std::endl;
                    }
                }
                break;
            case op::goodbye:
                std::cout << "Thanks for all the fish!" << std::endl;
        }
    }
}

int main() {
    std::thread thd(childThread);
    ch.send(op::say, std::string("hello"));
    ch.send(op::say, std::string("world"));
    ch.send(op::goodbye);
    ch.close();
    thd.join();
    return 0;
}
```

Terminal output might be:
```
$./a.out
child thread says: hello
child thread says: world
Thanks for all the fish!
$
```

### A Closer Look at Channel Send Operations
Arguments passed to `st::channel::send(...)` are internally passed to `st::message st::message::make(...)` function before sending to its destination. 

The summary of the 4 overloads of `st::message st::message::make(...)` are:

- `st::message st::message st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id()`. This message has no payload.
- `st::message st::message st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T`. The will be stored in the message as a type erased `st::data`. 
- `st::message st::message::make()`: Returns a default allocated `st::message`. This message has an id of 0 and no payload.
- `st::message st::message::make(st::message)`: Returns its argument immediately with no changes 

`st::message`s have 2 important methods:
- `std::size_t st::message::id()`: Return the unsigned integer id value stored in the message
- `st::data& st::message::data()`: Return a reference to the payload `st::data` stored in the message

### Message Data Payloads
`st::data()` can store any data type. The stored data can be copied to an argument of templated type `T` with `st::data::copy_to(T& t)` or rvalue swapped with `st::data::move_to(T& t)`. Said functions will return `true` if their argument `T` matches the type `T` originally stored in the `st::data`, otherwise they will return `false`.

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
    st::channel my_channel = st::channel::make();
    st::thread my_thread = st::thread::make<MyClass>(my_channel);

    my_channel.send(MyClass::op::print, std::string("hello again"));
    my_channel.send(MyClass::op::print, 14);
    my_thread.join(true);
    return 0;
}
```

Terminal output might be:
```
$./a.out
hello again
message data was not a string
```

### Payload Data Type Checking
Payload `st::data` types can also be checked with `bool st::data::is<T>()` (returns `true` if types match, else `false`) which is useful if a message's data might contain several different potential types. If `st::data` is unallocated then `st::data::is<T>()` will always return `false` (`st::data::operator bool()` conversion will also be `false`).

Additionally, the type stored in `st::data` can be cast to a reference with a call to `T& st::data::cast_to<T>()`. However, this functionality is only safe when used inside of an `st::data::is<T>()` check.

WARNING: `st::data` can store a payload of any type. However, this behavior can be confusing with regard to c-style `const char*` strings. c-style strings are just pointers to memory, and the user is responsible for ensuring that said memory is accessible outside of the scope when the message is sent. Typically, it is safe to send c-style strings with a hardcoded value, as such strings are stored in the program's global data. However, local stack arrays of characters, or, even worse, allocated c-strings must be carefully considered when sending over a message. 

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
                    std::cout << msg.data().cast_to<std::string>();
                } else if(msg.data().is<int>()) {
                    std::cout << msg.data().cast_to<int>();
                }
                break;
        }
    }
};

int main() {
    auto ch = st::channel::make();
    st::thread my_thread = st::thread::make<MyClass>(ch);

    ch.send(MyClass::op::print, std::string("hello"));
    ch.send(MyClass::op::print, 1);
    ch.send(MyClass::op::print, std::string(" more time\n"));
    my_thread.join(true);
    return 0;
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
```

### Object Lifecycles
The objects in this library are actually shared pointers to some shared context, whose context needs to be allocated with a static call to their respective `make()` functions. Objects in this category include:
- `st::message`
- `st::channel`
- `st::reply`

The user can check if these objects contain an allocated shared context with their `bool` conversion. This is easiest to do by using the object as the argument to an `if()` statement. Given an `st::thread` named `my_thd`:
```
if(my_channel) {
    // my_channel is allocated
} else {
    // my_channel is not allocated
}
```

Attempting to use API of these objects when they are *NOT* allocated (other than static allocation functions or the `bool` conversion) will typically result in a failure return value. This means that users generally don't need to handle exceptions, but they do need to pay attention to return values.

When the last object holding a copy of some shared context goes out of scope, that object will be neatly shutdown and be destroyed. As such, the user is responsible for keeping copies of the above objects when they are created with an allocator function (`make()`), otherwise they may unexpectedly shutdown.

### Closing Channels
Operations on `st::channel`'s shared context can be shutdown early with a call to `close()`, causing operations which use that shared context to fail.

For example, the default behavior for `st::channel::close()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. This behavior is similar in `st::thread`.

Alternatively, the user can call `close(false)`to immediately end all operations on the object. As a convenience, the function overload `st::thread::join(bool)` has been provided to close the `st::channel`, ending future `st::channel::send()` calls, and `join()` the underlying `std::thread`.

The user can call `bool closed()`on these objects to check if an object has been closed or is still running. 

#### Example 5
```
#include <iostream>
#include <string>
#include <sthread>

void looping_recv() {
    // Acquire a copy of the `st::channel` passed to our `st::thread`'s constructor
    st::channel ch = st::this_thread::get_channel();
    st::message msg;

    // Since this is a normal function, we handle the `st::message` receive loop 
    // ourselves. When `recv()` starts returning false the `while()` loop will end
    while(ch.recv(msg)) {
        std::string s;
        if(msg.data().copy_to(s)) {
            std::cout << s << std::endl;
        }
    }
}

int main() {
    st::channel my_channel = st::channel::make();

    // notice the use of a standard `st::thread` constructor instead of `st::thread::make<OBJECT>()`
    st::thread my_thread(my_channel, looping_recv);

    my_channel.send(0, "you say goodbye");
    my_channel.send(0, "and I say hello");
    my_thread.join(true); // close channel and join thread
    return 0;
}
```

Terminal output might be:
```
$./a.out 
you say goodbye
and I say hello
```

### Abstracting Message Replies 
Dealing with enumerations when message passing can be painful when enumerations conflict with each other.

Instead, the user can create an `st::reply` object to abstract sending a response back over an `st::channel` or `st::thread` which abstracts information about the receiver so the sender doesn't have to account for it.

`st::reply::make(...)` will take an `st::channel` and an unsigned integer `st::message` id. When `st::reply::send(T t)` is called, an `st::message` containing the given `st::message` id and an optional payload `t` is sent to the stored `st::channel`.

#### Example 7
```
#include <iostream>
#include <string>
#include <sthread>

enum opA {
    request_value = 0; // Send a value back to a requestor
};

enum opB {
    receive_value = 0; // same enumeration value as opA::request_value, normally a potential bug!
};

void childThreadA(st::channel ch) {
    std::string str = "foofaa";

    for(auto msg : ch)
        switch(msg.id()) {
            case opA::request_value:
            {
                st::reply r;
                if(msg.data().copy_to(r)) {
                    // this thread doesn't know anything about who they are replying to
                    r.send(str);
                }
                break;
            }
        }
    }

};

void childThreadB(st::channel ch, st::channel main_ch) {
    for(auto msg : ch) {
        switch(msg.id()) {
            case opB::receive_value:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << "received " << s << "!" std::endl;
                    main_ch.send();
                }
                break;
            }
        }
    }
};

int main() {
    // channels
    st::channel ch_a = st::channel::make(); // ObjA listens to this channel
    st::channel ch_b = st::channel::make(); // ObjB listens to this channel
    st::channel main_ch = st::channel::make(); // main thread listens to this channel
   
    // launch child threads
    st::thread thd_a = st::thread::make<ObjA>(ch_a);
    st::thread thd_b = st::thread::make<ObjB>(ch_b, main_ch);

    // create an `st::reply` to send a message to `ch_b` and send to `ch_a`
    st::reply rep_b = st::reply(ch_b, opB::receive_value);
    ch_a.send(opA::request_value, rep_b);

    // wait for childThreadB to process the response from childThreadA 
    st::message msg;
    main_ch.recv(msg); 

    // close and join child threads 
    ch_a.close();
    ch_b.close();
    thd_a.join(true);
    thd_b.join(true);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
received foofaa!
```

### Dealing with Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid calling functions which will block for indeclose periods within an `st::thread`. If the user needs to call such a function, a solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a dedicated system thread, then `send()` the result back to the `st::thread` when the call completes. 

As a convenience, `st::channel::async()` is provided for exactly this purpose, sending an `st::message` back to the object with the argument response id stored in `st::message::id()` and the return value of some executed function stored in the `st::message::data()` payload:
- `st::channel::async(std::size_t resp_id, user_function, optional_function_args ...)` 

If the user function returns `void`, the `st::message::data()` will be unallocated (`st::data::operator bool()` will return `false`).

The user can implement a simple timer mechanism using this functionality by calling `std::this_thread::sleep_for(...)` inside of `user_function`.

#### Example 8
```
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sthread>

std::string slow_function() {
    // long operation
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return std::string("that's all folks!");
};

struct MyClass {
    MyClass(st::channel main_ch) { }

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
                // execute our code on its own thread
                st::thread::self().async(op::slow_result, slow_function);
                break;
            case op::slow_result:
            {
                std::string s;
                if(msg.data().copy_to(s) {
                    st::thread::self().send(op::print, s);
                }
                break;
            }
        }
    }

    int cnt = 0;
}

int main() {
    st::channel thread_ch = st::channel::make();
    st::thread my_thread = st::thread::make<MyClass>(thread_ch);
    thread_ch.send(MyClass::op::slow_function);
    thread_ch.send(MyClass::op::print, std::string("1"));
    thread_ch.send(MyClass::op::print, std::string("2"));
    thread_ch.send(MyClass::op::print, std::string("3"));
    my_thread.join(true); // `true` value allows thread to process all sent messages before closing 
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

### Scheduling Functions on Threads 
`st::channel`s provide the ability to enqueue functions (actually any Callable) for asynchronous execution with `st::channel::schedule(...)` API. When a message containing a scheduled function is received by a thread it will automatically be executed on that thread before resuming waiting for incoming messages. Any `st::channel` can be used for this purpose.

`st::channel::schedule()` can accept any Callable function, functor, or lambda function, alongside optional arguments, in a similar fashion to standard library features `std::async()` and `std::thread()`.

#### Example 9
```
#include <iostream>
#include <string>
#include <sthread>

void print(const char* s) {
    std::cout << s << std::endl;
}

struct PrinterFunctor { 
    void operator()(const char* s) {
        std::cout << s << std::endl;
    }
};

int main() {
    // specifying `st::thread::callable` inside the template `<>` is optional
    st::thread thd = st::thread::make<>(st::channel::make());
    thd.channel().schedule(print, "what a beautiful day");
    thd.channel().schedule(PrintFunctor, "looks like rain");
    thd.channel().schedule([]{ std::cout << "what a beautiful sunset" << std::endl; });
    thd.join(true);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
what a beautiful day 
looks like rain 
what a beautiful sunset
```
