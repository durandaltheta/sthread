# Simple Threading and Communication
## Quick Links

The overall design of code in this library relies heavily on virtual
interfaces to implement inherited behavior. Visit the documentation for 
information on interfaces and various other features not detailed in this README.
[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/)

#### Usage:
[Creating Threads from Objects](#creating-threads-from-objects)

[Creating Traditional Threads](#creating-traditional-threads)

[Channel Send Operations](#send-operations) 

[Type Checking](#type-checking)

[Thread Constructor Arguments](#thread-constructor-arguments)

[Object Lifecycles](#object-lifecycles)

[Sending Messages Between Threads](#sending-messages-between-threads-with-channels)

[Abstracting Message Replies](#abstracting-message-senders)

[Dealing with Blocking Functions](#dealing-with-blocking-functions)

[Scheduling Functions on Threads](#scheduling-functions-on-threads)

[Callable Considerations](#callable-considerations)

## Purpose 
This library's purpose is to simplify setting up useful c++ threading, and to enable trivial inter-thread message passing of C++ objects.

The library provides a thread-like object named `st::thread` (which inherits the standard library `std::thread`), an object which can manage a system thread and process incoming messages.

`st::thread` can be constructed similarly to an `std::thread`, except that `st::thread` requires its first argument to be an `st::channel`. More on that later.

However, the recommened usage is to execute an object's `recv()` method, instead of executing a global/static function:
```
struct MyClass {
    void recv(st::message msg) { /* ... */ }
};
```

`st::thread`s running user objects have several advantages over system threads running raw functions. 
- The system thread's message receive loop is managed by the library 
- Objects allow for inheritance
- Objects allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
- Objects allow for members to be used like local variables and functions within the `st::thread`'s receive loop
- Objects enable RAII (Resource Acquisition is Initialization) semantics

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
### Creating Threads from Objects
- Install the library and include the header `sthread`
- Create a class or struct and implement method `void recv(st::message)` to handle received messages 
- Define some enum to distinguish different messages 
- Construct a message passing `st::channel`
- Launch your `st::thread` with `st::thread::make<YourClassNameHere>(your_channel, optional_constructor_args...)`
- Trigger user class's `void recv(st::message)` via `st::channel::send(enum_id, optional_payload)` 
- User object can distinguish `st::message`s by their unsigned integer id (possibly representing an enumeration) with a call to `st::message::id()`. 
- When finished call `st::thread::join(bool)` to close the `st::channel`, ending future `st::channel::send()` calls, and `join()` the underlying `std::thread`

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
    st::channel my_channel = st::channel::make();
    st::thread my_thread = st::thread::make<MyClass>(my_channel);
    my_channel.send(MyClass::op::hello);
    my_channel.send(MyClass::op::world);
    my_thread.join(true); // close my_channel and join my_thread
    return 0;
}
```

Terminal output might be:
```
$./a.out
hello
world
```

The code that calls `st::thread::make()` to create a thread is responsible for keeping a copy of the resulting `st::thread` object. Otherwise the launched `st::thread` may stop executing because it will go out of scope.

### Creating Traditional Threads 
This library also provides a mechanism to create `st::thread`s constructed in a similar fashion to standard library `std::threads`. The constructor is the same except a `st::channel` is required as the first constructor argument. This `st::channel` will be returned in the running thread when the `thread_local` function `st::this_thread::get_channel()` is called.

See [Object Lifecycles](#object-lifecycles) for an example.

### Channel Send Operations
Arguments passed to `st::channel::send(...)` are subsequently passed to `st::message st::message::make(...)` before the resulting `st::message` is passed to its destination thread and object. The summary of the 4 basic overloads of `st::message st::message::make(...)` are:

- `st::message st::message st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id()`.
- `st::message st::message st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T`. The will be stored in the message as a type erased `st::data`. 
- `st::message st::message::make()`: Returns a default allocated `st::message`
- `st::message st::message::make(st::message)`: Returns its argument immediately with no changes 

`st::message`s have 2 important methods:
- `std::size_t st::message::id()`: Return the unsigned integer id value stored in the message
- `st::data& st::message::data()`: Return a reference to the payload `st::data` stored in the message

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

### Type Checking
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

### Thread Constructor Arguments
`st::thread`s can be passed `OBJECT` constructor arguments `as...` in `st::thread::make<OBJECT>(ch, As&& as...)`. The `OBJECT` class will be created on a new system thread and destroyed before said thread ends.

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
    st::thread my_thread = st::thread::make<MyClass>("hello", "goodbye");
    my_thread.join(true);
    return 0;
}

```

Terminal output might be:
```
$./a.out 
0x800018040:parent thread started
0x800098150:hello
0x800098150:goodbye
```

### Object Lifecycles
Many objects in this library are actually shared pointers to some shared context, whose context needs to be allocated with a static call to their respective `make()` functions. Objects in this category include:
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

Attempting to use API of these objects when they are *NOT* allocated (other than static allocation functions or the `bool` conversion) will typically result in a failure return value.

When the last object holding a copy of some shared context goes out of scope, that object will be neatly shutdown and be destroyed. As such, the user is responsible for keeping copies of the above objects when they are created with an allocator function (`make()`), otherwise they may unexpectedly shutdown.

`st::channel`'s shared context can be shutdown early with a call to `close()`, causing operations which use that shared context to fail.

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

### Sending Messages Between Threads
`st::thread`s can hold copies of other `st::channel`s to communicate with each other. Alternatively the user can store all `st::channels`s in globally accessible manner so each can access the others as necessary. The design is entirely up to the user.

#### Example 6
```
#include <iostream>
#include <string>
#include <list>
#include <sthread>

struct MyThread {
    enum op {
        say_name
    };
    
    MyThread(int name, st::channel next) : 
        m_name(name), m_next(next) 
    { }

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::say_name:
            {
                std::cout << "My name is MyThread" << m_name << std::endl;
                m_next.send(op::say_name);
                break;
            }
        }
    }

    int m_name;
    st::channel m_next; // next thread in the list
};

int main() {
    // create a channel to block on while threads are running
    st::channel ch0 = st::channel::make();
    st::channel ch1 = st::channel::make();
    st::channel ch2 = st::channel::make();
    st::channel ch3 = st::channel::make();

    // Create an implicit singly linked list of channels
    st::thread thread0 = st::thread::make<MyThread>(ch0, 0, ch1);
    st::thread thread1 = st::thread::make<MyThread>(ch1, 1, ch2);
    st::thread thread2 = st::thread::make<MyThread>(ch2, 2, ch3);

    ch.send(MyThread::op::say_name);

    st::message msg;
    ch3.recv(msg); // block until threads are done processing
    thread0.join(true);
    thread1.join(true);
    thread2.join(true);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
My name is MyThread0
My name is MyThread1
My name is MyThread2
```

### Abstracting Message Replies 
Dealing with enumerations when message passing can be painful when enumerations conflict with each other.

Instead, the user can create an `st::reply` object to abstract sending a response back over an `st::channel` or `st::thread`.

`st::reply::make(...)` will take an `st::channel` and an unsigned integer `st::message` id. When `st::reply::send(T t)` is called, an `st::message` containing the previously given `st::message` id and an optional payload `t` is sent to the stored `st::channel`.

#### Example 7
```
#include <iostream>
#include <string>
#include <sthread>

struct ObjA {
    ObjA() : m_str("foofaa") { }

    enum op {
        request_value = 0;
    };

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::request_value:
            {
                st::reply r;
                if(msg.data().copy_to(r)) {
                    r.send(m_str);
                }
                break;
            }
        }
    }

    std::string m_str
};

struct ObjB {
    ObjB(st::channel main_ch) : m_main_ch(main_ch) { }

    enum op {
        receive_value = 0; // same value as ObjA::op::request_value
    };

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::receive_value:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << "received " << s << "!" std::endl;
                    m_main_ch.send();
                }
                break;
            }
        }
    }

    st::channel m_main_ch;
};

int main() {
    st::channel ch_a = st::channel::make();
    st::channel ch_b = st::channel::make();
    st::channel ch_c = st::channel::make();
    st::thread thd_a = st::thread::make<ObjA>(ch_a);
    st::thread thd_b = st::thread::make<ObjB>(ch_b, ch_c);

    // create an `st::reply` to send a message to `ch_b`
    st::reply rep_b = st::reply(ch_b, ObjB::op::receive_value);

    // send the `st::reply` `ch_a` for `thd_a` to use
    ch_a.send(ObjA::op::request_value, rep_b);

    st::message msg;
    ch_c.recv(msg);
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

As a convenience, member functions are provided for exactly this purpose, sending an `st::message` back to the object with the argument response id stored in `st::message::id()` and the return value of some executed function stored in the `st::message::data()` payload:
- `st::channel::async(std::size_t resp_id, user_function, optional_function_args ...)` 
- `st::thread::async(std::size_t resp_id, user_function, optional_function_args ...)`

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
`st::thread`s provide the ability to enqueue arbitrary code for asynchronous execution with `st::channel::schedule(...)` API. Any `st::thread` can be used for this purpose, though the default `st::thread::make<>()` `OBJECT` template type `st::thread::callable` is useful for generating worker `st::thread`s dedicated to scheduling other code.

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

### Callable Considerations
The reasoning behind the default approach of using user objects implementing `recv(st::message)` within `st::thread`s, as opposed to a c++ Callable (any object convertable to `std::function<void(st::message)>`), is that c++ objects are well understood by virtually all c++ programmers and provide the most control over behavior compared to alternatives.

In comparision, Callables are a rather advanced topic that fewer intermediate c++ programmers may understand well. Additionally, operator overloads are a more advanced topic than I wanted to require of users.

However, if a user wants to use Callables with `st::thread` like a standard library `std::thread` then default `st::thread` `OBJECT` `st::thread::callable` is provided as a convenience, as it can represent any Callable convertable to `std::function<void(st::message)>`.

#### Example 10
```
#include <iostream>
#include <sthread>

void MyFunction(st::message msg) { 
    if(msg.data().is<const char*>()) {
        std::cout << msg.data().cast_to<const char*>() << std::endl;
    }
}

struct MyFunctor {
    void operator()(st::message msg) { 
        if(msg.data().is<const char*>()) {
            std::cout << msg.data().cast_to<const char*>() << std::endl;
        }
    }
};

int main() {
    // specifying `st::thread::callable` inside the template `<>` is optional
    auto thd1 = st::thread::make<>(MyFunction);
    thd1.send(0,"Tis but a scratch!");

    auto thd2 = st::thread::make<>(MyFunctor());
    thd2.send(0,"No it isn't.");

    auto MyLambda = [](st::message msg){ 
        if(msg.data().is<const char*>()) {
            std::cout << msg.data().cast_to<const char*>() << std::endl;
        }
    };
    auto thd3 = st::thread::make<>(MyLambda);
    thd3.send(0,"Then what's that?");
    
    thd1.join(true);
    thd2.join(true);
    thd3.join(true);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
Tis but a scratch!
No it isn't.
Then what's that?
```
