# Simple Threading and Communication
## Quick Links

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

[Dealing with Blocking Functions](#dealing-with-blocking-functions)

## Purpose 
This library's purpose is to make setting up useful c++ threading simple.

The main thread-like object provided by the library is `st::thread`, an object which can manage a system thread and process incoming messages.

Instead of functions `st::thread`s execute an object's `recv()` method:
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
- Objects allow for public enumerations and child classes to be defined as part of its namespace, which is useful for organizing what messages and message payload data types the thread will listen for.
- Objects allow for class method definitions, instead of forcing the user to rely on lambdas, local objects or global namespace functions if further function calls are desired.
- Initialization (constructor), runtime execution (`void recv(st::message`), and deinitialization (destructor) are broken in to separate functions, which I think makes them more readable. A thread running only a raw function requires everything be managed within that function or within child objects managed by that function. 

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

[Back To Top](#simple-threading-and-communication)

### Send Operations
Several classes in this library support the ability to send messages:
- `st::channel::send(/* ... *)`
- `st::thread::send(/* ... *)`

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

[Back To Top](#simple-threading-and-communication)

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

The user can check if these objects contain an allocated shared context with their `bool` conversion. This is easiest to do by using the object as the argument to an `if()` statement. Given an `st::thread` named 'my_fib':
```
if(my_fib) {
    // my_fib is allocated
} else {
    // my_fib is not allocated
}
```

Attempting to use API of these objects when they are *NOT* allocated (other than static allocation functions or the `bool` conversion) will typically raise an exception.

When the last object holding a copy of some shared context goes out of scope, that object will be neatly shutdown and be destroyed. As such, the user is responsible for keeping copies of the above objects when they are created with an allocator function (`make()`), otherwise they may unexpectedly shut down.

In some cases, object's shared context can be shutdown early with a call to `st::channel::close()` or `st::thread::shutdown()` API respectively.

In `st::channel`'s case, blocking `st::channel::recv()` operations can be stopped and made to return `false` by calling `st::channel::close()`. `st::thread` uses an `st::channel` internally for receiving `st::message`s, and it uses this behavior in order to determine when it should stop processing messages and go out of scope.

The default behavior for `st::channel::shutdown()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. Alternatively, the user can call `st::channel::close(false)`/`st::thread::shutdown(false)` to immediately end all operations on the `st::channel`/`st::thread`.

The user can call `bool st::channel::closed()`/`bool st::thread::running()` respectively on these objects to check if an object has been closed or shutdown. 

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
    my_channel.shutdown(); // end thread looping 
    my_thread.join(); // join thread
}
```

Terminal output might be:
```
$./a.out 
you say goodbye
and I say hello
```

[Back To Top](#simple-threading-and-communication)

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
                    m_done_ch.send(0);
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

[Back To Top](#simple-threading-and-communication)

### Scheduling Functions on Threads 
`st::thread`s provide the ability to enqueue arbitrary code for asynchronous execution with `st::thread::schedule(...)` API. Any `st::thread` can be used for this purpose, though the default `st::thread::make<>()` `OBJECT` template type `st::thread::processor` is useful for generating worker `st::thread`s dedicated to scheduling other code.

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

[Back To Top](#simple-threading-and-communication)

### Dealing with Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid calling functions which will block for indeterminate periods within an `st::thread`. If the user needs to call such a function, a solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a dedicated system thread, then `send()` the result back to the `st::thread` when the call completes. As a convenience, `st::thread::async(std::size_t resp_id, ...)` and `st::channel::async(std::size_t resp_id, ...)` are provided for exactly this purpose:

#### Example 8
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
                    std::cout << s << std::endl;
                }
                main_ch.send(0); // unblock main
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

[Back To Top](#simple-threading-and-communication)
