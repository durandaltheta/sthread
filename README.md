# Simple Threading and Communication
## Quick Links

The overall design of code in this library relies heavily on virtual
interfaces to implement inherited behavior. Visit the documentation for 
information on interfaces and various other features not detailed in this README.
[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/)

#### Usage:
[Creating Threads from Objects](#creating-threads-from-objects)

[Send Operations](#send-operations) 

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

The library provides a thread-like object named `st::thread`, an object which can manage a system thread and process incoming messages.

Instead of executing a global/static function `st::thread`s execute an object's `recv()` method:
```
struct MyClass {
    void recv(st::message msg) { /* ... */ }
};
```

`st::thread`s running user objects have several advantages over system threads running raw functions. 
- Sending messages to the thread is provided by the library 
- The system thread's message receive loop is managed by the library 
- The system thread's lifecycle is managed by the library 
- Objects allow for inheritance
- Objects allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
- Objects allow for member data to be used like local variables within the `st::thread`'s receive loop
- Objects allow for use of local namespace methods, instead of forcing the user to rely on lambdas, local objects or global namespace functions if further function calls are desired.
- Objects enable RAII (Resource Acquisition is Initialization) semantics
- Object's constructors, runtime execution (`void recv(st::message`), and destructor are broken into separate functions, which I think makes them more readable than alternatives when dealing with system threads.

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
- Launch your `st::thread` with `st::thread::make<YourClassNameHere>()`
- Trigger user class's `void recv(st::message)` via `st::thread::send(enum_id, optional_payload)` 
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
    st::thread my_thread = st::thread::make<MyClass>();
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

The code that calls `st::thread::make()` to create a thread is responsible for keeping a copy of the resulting `st::thread` object. Otherwise the launched `st::thread` may stop executing because it will go out of scope.

### Send Operations
Several classes in this library support the ability to send messages:
- `st::channel::send(...)`
- `st::thread::send(...)`

Arguments passed to `send(...)` are subsequently passed to `st::message st::message::make(...)` before the resulting `st::message` is passed to its destination thread and object. The summary of the 4 basic overloads of `st::message st::message::make(...)` are:

- `st::message st::message st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id()`.
- `st::message st::message st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T`. The will be stored in the message as a type erased `st::data`. 
- `st::message st::message::make()`: Returns a default allocated `st::message`
- `st::message st::message::make(st::message)`: Returns its argument immediately with no changes 

`st::message`s have 2 important methods:
- `std::size_t st::message::id()`: Return the unsigned integer id value stored in the message
- `st::data& st::message::data()`: Return a reference to the payload `st::data` stored in the message

`st::data()` can store any data type. The stored data can be copied to an argument of templated type `T` with `st::data::copy_to(T& t)` or rvalue swapped with `st::data::move_to(T& t)`. Said functions will return `true` if their argument `T` matches the type originally stored in the `st::data`, otherwise they will return `false`.

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
    st::thread my_thread = st::thread::make<MyClass>();

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
    st::thread my_thread = st::thread::make<MyClass>();

    my_thread.send(MyClass::op::print, std::string("hello"));
    my_thread.send(MyClass::op::print, 1);
    my_thread.send(MyClass::op::print, std::string(" more time\n"));
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
```

### Thread Constructor Arguments
`st::thread`s can be passed `OBJECT` constructor arguments `as...` in `st::thread::make<OBJECT>(As&& as...)`. The `OBJECT` class will be created on a new system thread and destroyed before said thread ends.

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
    st::thread wkr = st::thread::make<MyClass>("hello", "goodbye");
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
- `st::thread`
- `st::reply`

The user can check if these objects contain an allocated shared context with their `bool` conversion. This is easiest to do by using the object as the argument to an `if()` statement. Given an `st::thread` named `my_thd`:
```
if(my_thd) {
    // my_thd is allocated
} else {
    // my_thd is not allocated
}
```

Attempting to use API of these objects when they are *NOT* allocated (other than static allocation functions or the `bool` conversion) will typically raise an exception.

When the last object holding a copy of some shared context goes out of scope, that object will be neatly shutdown and be destroyed. As such, the user is responsible for keeping copies of the above objects when they are created with an allocator function (`make()`), otherwise they may unexpectedly shutdown.

In some cases, object's shared context can be shutdown early with a call to `terminate()`, causing operations on that object's which use that shared context to fail. Several objects support this API:
- `st::channel`
- `st::thread`

For example, the default behavior for `st::channel::terminate()` is to cause all future `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. This behavior is similar in `st::thread`.

Alternatively, the user can call `terminate(false)`to immediately end all operations on the object.

The user can call `bool alive()`on these objects to check if an object has been terminated or is still running. 

#### Example 5
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
    st::channel my_channel = st::channel::make();

    // notice the use of an standard library std::thread instead of st::thread
    std::thread my_thread(looping_recv, my_channel);

    my_channel.send(0, "you say goodbye");
    my_channel.send(0, "and I say hello");
    my_channel.terminate(); // end thread looping 
    my_thread.join(); // join thread
}
```

Terminal output might be:
```
$./a.out 
you say goodbye
and I say hello
```

### Sending Messages Between Threads
`st::thread`s can hold copies of other `st::thread`s or `st::channel`s and use these copies' `send()` functions to communicate with each other. Alternatively the user can store all `st::thread`s in globally accessible manner so each can access the others as necessary. The design is entirely up to the user.

*WARNING*: `OBJECT`s running in an `st::thread` need to be careful to *NOT* hold a copy of that `st::thread` as a member variable, as this can create a memory leak. Instead, static function `st::thread st::thread::self()` should be called from within the running `OBJECT` when accessing the associated `st::thread` is necessary.

#### Example 6
```
#include <iostream>
#include <string>
#include <list>
#include <sthread>

struct Childthread {
    enum op {
        say_name
    };
    
    MyThread(int name, 
             st::channel done_ch,
             st::thread next=st::thread()) : 
        m_name(name), m_done_ch(done_ch), m_next(next) 
    { }

    void recv(st::message msg) {
        switch(msg.id()) {
            case op::say_name:
            {
                std::cout << "My name is MyThread" << m_name << std::endl;

                if(m_next) {
                    m_next.send(op::say_name);
                } else {
                    m_done_ch.send();
                }
                break;
            }
        }
    }

    int m_name;
    st::channel m_done_ch;
    st::thread m_next; // next thread in the list
};

int main() {
    // create a channel to block on while threads are running
    st::channel ch = st::channel::make();

    // Create an implicit singly linked list of threads.
    st::thread thread2 = st::thread::make<MyThread>(2,ch);
    st::thread thread1 = st::thread::make<MyThread>(1,ch,thread2);
    st::thread thread0 = st::thread::make<MyThread>(0,ch,thread1);

    thread0.send(MyThread::op::say_name);

    st::message msg;
    ch.recv(msg); // block until threads are done processing
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

`st::reply::make(...)` will take an object which implements `st::shared_sender_context`  and an unsigned integer `st::message` id. The following objects all implement `st::shared_sender_context`:
- `st::channel`
- `st::thread`

When `st::reply::send(T t)` is called, an `st::message` containing the previously given `st::message` id and the argument `t` is sent to the stored `st::shared_sender_context`. 

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
    st::channel main_ch = st::channel::make();
    st::thread thd_a = st::thread::make<ObjA>();
    st::thread thd_b = st::thread::make<ObjB>(main_ch);

    st::reply rep_b = st::reply(thd_b, ObjB::op::receive_value);
    thd_a.send(ObjA::op::request_value, rep_b);

    st::message msg;
    main_ch.recv(msg);
    return 0;
}
```

Terminal output might be:
```
$./a.out 
received foofaa!
```

### Dealing with Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid calling functions which will block for indeterminate periods within an `st::thread`. If the user needs to call such a function, a solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a dedicated system thread, then `send()` the result back to the `st::thread` when the call completes. 

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
                // execute our code on its own thread
                st::thread::self().async(op::slow_result, slow_function);
                break;
            case op::slow_result:
            {
                std::string s;
                if(msg.data().copy_to(s) {
                    st::thread::self().send(op::print, s);
                }
                main_ch.send(); // unblock main
                break;
            }
        }
    }

    st::channel m_main_ch;
    int cnt = 0;
}

int main() {
    st::channel ch = st::channel::make();
    st::thread my_thread = st::thread::make<MyClass>(ch);
    my_thread.send(MyClass::op::slow_function);
    my_thread.send(MyClass::op::print, std::string("1"));
    my_thread.send(MyClass::op::print, std::string("2"));
    my_thread.send(MyClass::op::print, std::string("3"));

    st::message msg;
    ch.recv(msg); // block so program doesn't end before our functions can run
    return 0; // return causing my_thread to process remaining messages then exit
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
`st::thread`s provide the ability to enqueue arbitrary code for asynchronous execution with `st::thread::schedule(...)` API. Any `st::thread` can be used for this purpose, though the default `st::thread::make<>()` `OBJECT` template type `st::thread::callable` is useful for generating worker `st::thread`s dedicated to scheduling other code.

`st::thread::schedule()` can accept any Callable function, functor, or lambda function, alongside optional arguments, in a similar fashion to standard library features `std::async()` and `std::thread()`.

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
    st::thread thd = st::thread::make<>();
    thd.schedule(print, "what a beautiful day");
    thd.schedule(PrintFunctor, "looks like rain");
    thd.schedule([]{ std::cout << "what a beautiful sunset" << std::endl; });
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
The reasoning behind the default approach of using user objects implementing `recv(st::message)` within `st::thread`s, as opposed to a c++ Callable (any function or object convertable to `std::function<void(st::message)>` in this case), is that c++ objects are well understood by virtually all c++ programmers and provide the most fine grained control over behavior compared to alternatives.

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
