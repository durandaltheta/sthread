# Simple Threading and Communication
## Quick Links

The overall design of code in this library relies heavily on virtual
interfaces to implement behavior. Visit the documentation for information on 
interfaces and various other features not detailed in this README.
[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/sthread_tst.cpp)

#### Usage:
[Creating Threads from Objects](#creating-threads-from-objects)

[Send Operations](#send-operations) 

[Type Checking](#type-checking)

[Thread Constructor Arguments](#thread-constructor-arguments)

[Object Lifecycles](#object-lifecycles)

[Sending Messages Between Threads](#sending-messages-between-threads-with-channels)

[Scheduling Functions on Threads](#scheduling-functions-on-threads)

[Scheduling Fibers on Threads](#Scheduling-fibers-on-threads)

[Dealing with Blocking Functions](#dealing-with-blocking-functions)

[Abstracting Message Senders and Code Schedulers](#abstracting-message-senders)

[Abstracting Message Replies](#abstracting-message-senders)

## Purpose 
This library's purpose is to make setting up useful c++ threading simple.

The library provides a thread-like object named `st::thread`, an object which can manage a system thread and process incoming messages.

Instead of functions `st::thread`s execute an object's `recv()` method:
```
struct MyClass {
    void recv(st::message msg) { /* ... */ }
};
```

`st::thread`s running user objects have several advantages over system threads running raw functions. 
- Sending messages to the thread is provided by the library 
- The system thread's message receive loop is managed by the library 
- The System thread's lifecycle is managed by the library 
- Objects allow for inheritance
- Objects allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
- Objects allow for member data to be used like local variables within the `st::thread`'s receive loop
- Objects allow for use of local namespace methods, instead of forcing the user to rely on lambdas, local objects or global namespace functions if further function calls are desired.
- Objects enable RAII (Resource Acquisition is Initialization) semantics
- Object's constructors, runtime execution (`void recv(st::message`), and destructor are broken into separate functions, which I think makes them more readable than most alternatives when dealing with system threads.

## Requirements
- C++11 

## Git Submodules
This project uses Googletest as a submodule to build unit tests. If unit tests 
are needed try cloning this project with submodules:
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
- Launch your thread with `st::thread::make<YourClassNameHere>()`
- Trigger user class's `void recv(st::message)` via `st::thread::send(/* ... */)` 
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
- `st::fiber::send(...)`
- `st::sender::send(...)`

Arguments passed to `send(...)` are subsequently passed to `st::message st::message::make(...)` before the resulting `st::message` is passed to its destination thread and object. The summary of the 4 basic overloads of `st::message st::message::make(...)` are:

- `st::message st::message::make()`: Returns a default allocated `st::message`
- `st::message st::message st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id()`.
- `st::message st::message st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T`. The will be stored in the message as a type erased `st::data`. 
- `st::message st::message::make(st::message)`: Returns its argument immediately with no changes 

`st::message`s have 2 important methods:
- `std::size_t st::message::id()`: Return the unsigned integer id value stored in the messsage
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
Payload `st::data` types can also be checked with `bool st::data::is<T>()` (returns `true` if types match, else `false`) which is useful if a message's data might contain several different potential types. 

Additionally, the type stored in `st::data` can be cast to a reference with a call to `T& st::data::cast_to<T>()`. However, this functionality is only safe when used inside of an `st::data::is<T>()` check.

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
- `st::fiber`
- `st::sender`
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

In some cases, object's shared context can be shutdown early with a call to `terminate()`, causing operations on that object's which use that shared context to fail.

For example, the default behavior for `st::channel::terminate()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. This behavior is similar in `st::thread`, `st::fiber` and `st::sender`.

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
`st::thread`s can hold copies of other `st::thread`s or `st::channel`s and use these copies' `send()` functions to communicate with each other. Alternatively the user can store all `st::thread`s in a globally accessible singleton object so `st::thread`s can access each other as necessary. The design is entirely up to the user.

*WARNING*: `OBJECT`s running in an `st::thread` need to be careful to *NOT* hold a copy of that `st::thread` as a member variable, as this can create a memory leak. Instead, static function `st::thread st::thread::self()` should be called from within the running `OBJECT` when accessing the `OBJECT`'s associated `st::thread` is necessary.

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

### Scheduling Functions on Threads 
`st::thread`s provide the ability to enqueue arbitrary code for asynchronous execution with `st::thread::schedule(...)` API. Any `st::thread` can be used for this purpose, though the default `st::thread::make<>()` `OBJECT` template type `st::thread::processor` is a useful default for generating worker `st::thread`s dedicated to scheduling other code.

`st::thread::schedule()` can accept a function, functor, or lambda function, alongside optional arguments, in a similar fashion to standard library features `std::async()` and `std::thread()`.

#### Example 7
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
    // specifying `st::thread::processor` inside the template `<>` is optional
    st::thread my_processor = st::thread::make<>();
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

### Scheduling Fibers on Threads
`st::fiber` is an object very similar to `st::thread` in that it can process `st::message`s sent to it via `st::fiber::send(...)` and that it is uses an `OBJECT` template to handle the received message.

The main difference between `st::fiber` and `st::thread` is that an `st::fiber` scheduled on an `st::thread` and cannot run on their own. `st::fiber`s running on an `st::thread` take turns executing, allowing for very fast concurrency/message passing between multiple `st::fiber`s. `st::fiber`s are a type of stackless coroutine.

Static function `st::fiber st::fiber::self()` can be called within an `OBJECT` running in an `st::fiber` to retrieve c copy of that `st::fiber`. As usual, this copy should not be saved as an `OBJECT` member to prevent a memory leak.

`st::fiber`s support the ability to schedule code for execution like an `st::thread` with `st::fiber::schedule(...)`. Calls to this function will internally call `st::thread::schedule(...).` on the `st::fiber`'s parent `st::thread`.

Example 8
```
#include <iostream>
struct MyFiber {
    MyFiber(st::channel ch = st::channel()) : m_ch(ch) { }

    ~MyFiber() {
        if(ch) {
            ch.send();
        }
    }

    void recv(st::message msg) {
        std::string s;
        if(msg.data().copy_to(s)) {
            std::cout << s << std::endl;
        }
    }

    st::channel m_ch;
};

int main() {
    st::chanel ch = st::channel::make();

    // create `st::thread` with default `OBJECT` `st::thread::processor`
    st::thread thd = st::thread::make<>(); 
    st::fiber fib1 = st::fiber::make<MyFiber>(thd);
    st::fiber fib2 = st::fiber::make<MyFiber>(thd);
    st::fiber fib3 = st::fiber::make<MyFiber>(thd);
    st::fiber fib4 = st::fiber::make<MyFiber>(thd, ch);

    fib1.send(0,std::string("hello"));
    fib2.send(0,std::string(" my"));
    fib3.send(0,std::string(" name"));
    fib4.send(0,std::string(" is"));
    fib5.send(0,std::string(" foo\n"));

    st::message msg;
    ch.recv(msg); // wait for last fiber to process 
    return 0;
}
```

Terminal output might be:
```
$./a.out 
hello my name is foo
$
```

### Dealing with Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid calling functions which will block for indeterminate periods within an `st::thread`. If the user needs to call such a function, a solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a dedicated system thread, then `send()` the result back to the `st::thread` when the call completes. 

As a convenience these member functions are provided for exactly this purpose:
- `st::channel::async(std::size_t resp_id, ...)` 
- `st::thread::async(std::size_t resp_id, ...)`
- `st::fiber::async(std::size_t resp_id, ...)`
- `st::sender::async(std::size_t resp_id, ...)`

#### Example 9
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

### Abstracting Message Senders and Code Schedulers
`st::sender` is an object which can represent any implementor of the interface `st::shared_send_context`. Implementors of said interface include:
- `st::channel`
- `st::thread`
- `st::fiber` 
- `st::sender`

This means that an `st::sender` can be created to represent either an `st::channel`, `st::thread`, or `st::fiber`.

These objects are trivially convertable to `st::sender` with a simple `=` operation:

#### Example 10
```
#include <iostream>
#include <sthread>

struct MyObject {
    MyObject(st::channel main_ch) : m_main_ch(main_ch) { }

    void recv(st::message msg) {
        std::cout << "Hello this is MyObject" << std::endl;
        m_main_ch.send();
    }

    st::channel m_main_ch;
};

struct MyFiber {
    MyObject(st::channel main_ch) : m_main_ch(main_ch) { }

    void recv(st::message msg) {
        std::cout << "Hello this is MyFiber" << std::endl;
        m_main_ch.send();
    }

    st::channel m_main_ch;
};

void MyThread(st::channel ch, st::channel main_ch) {
    st::message msg;

    while(ch.recv(msg)) {
        std::cout << "Hello this is MyThread" << std::endl;
        main_ch.send();
    }
}

int main() {
    st::message msg;
    st::channel main_ch = st::channel::make();

    // construct an abstracted message sending object `st::sender`
    st::sender snd(st::thread::make<MyObject>(main_ch));
    snd.send();
    main_ch.recv(msg); // block waiting for response message
   
    snd = st::fiber::make<MyFiber>(thd, main_ch);
    snd.send();
    main_ch.recv(msg); // block waiting for response message

    st::channel thd_ch = st::channel::make();
    std::thread thd(MyThread, thd_ch, main_ch);
    snd = thd_ch;
    snd.send();
    main_ch.recv(msg); // block waiting for response message

    thd_ch.close();
    thd.join();

    return 0;
}
```

Terminal output might be:
```
$./a.out
Hello this is MyObject
Hello this is MyFiber
Hello this is MyThread
```

`st::scheduler` is another abstraction object (similar to `st::sender`) that can represent an implementor of `st::shared_scheduler_context`, including:
- `st::thread`
- `st::fiber`
- `st::scheduler`

`st::scheduler` shares all the API of `st::sender` but additionally can schedule arbitrary code with `st::scheduler::schedule(...)`. 

These objects are trivially convertable to `st::scheduler` with a simple `=` operation:

#### Example 11
```
#include <iostream>
#include <sthread>

void print(const char* s) {
    std::cout << "printing: " << s << std::endl;
}

struct MyFiber {
    void recv(st::message msg) { 
        if(msg.data().is<const char*>()) {
            std::cout << "MyFiber printing: " << msg.data.cast_to<const char*>() << std::endl;
        }
    };
};

int main() {
    // default st::thread `OBJECT` is st::thread::processor
    st::thread thd = st::thread::make<>();
    st::scheduler sch = thd;

    sch.schedule(print, "one");

    // default st::fiber `OBJECT` is also st::thread::processor
    sch = st::fiber::make<>(thd);
    sch.schedule(print, "two");

    sch = st::fiber::make<MyFiber>(thd);
    sch.schedule(print, "three");
    sch.send(0, "four");

    return 0;
}
```

Terminal output might be:
```
$./a.out 
printing: one
printing: two
printing: three
MyFiber printing: four
```

### Abstracting Message Replies 
Dealing with enumerations when message passing can be painful when enumerations conflict with each other.

Instead, the user can create an `st::reply` object to abstract sending a response back over an `st::channel` or `st::thread`.

`st::reply::make(...)` will take an `st::sender` (or an argument convertable to an `st::sender` like `st::channel` or `st::thread`) and an unsigned integer `st::message` id. 

When `st::reply::send(T t)` is called, an `st::message` containing the previously given `st::message` id and the argument `t` is sent to the stored `st::sender`. 

#### Example 12
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

    thd_a.send(ObjA::op::request_value, st::reply(thd_b, ObjB::op::receive_value));

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
