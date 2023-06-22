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

[Scheduling Functions on User Threads](#scheduling-functions-on-user-threads)

[Interprocess Considerations](#interprocess-considerations)

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
All that is required to send/recv messages between threads is that each thread has a copy of a constructed `st::channel` object. Messages can be sent with a call to `bool st::message::send(...)`.

Messages can be received by calling `bool st::channel::recv(st::message& msg)`, but the simplest solution is to make use of `st::channel` iterators in a range-for loop. `st::channel` iterators can also be returned with calls to `st::channel::begin()` and `st::channel::end()`.

#### Example
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
                    if(msg.data().copy_to(s)) { 
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
Arguments passed to `st::channel::send(...)` are internally passed to `st::message st::message::make(...)` function before sending to its destination. `st::message::make(...)` will generate an allocated `st::message` object which contains message identification and an optional data payload.

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

#### Example
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

void my_function(st::channel ch) {
    for(auto msg : ch) {
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
    std::thread my_thread(my_function, my_channel);

    my_channel.send(MyClass::op::print, std::string("hello again"));
    my_channel.send(MyClass::op::print, 14);
    my_channel.close();
    my_thread.join();
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

#### Example
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

enum op {
    print
};

void my_function(st::channel ch)
    for(auto msg : ch) {
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
    std::thread thd(my_function, ch);

    ch.send(MyClass::op::print, std::string("hello"));
    ch.send(MyClass::op::print, 1);
    ch.send(MyClass::op::print, std::string(" more time\n"));
    ch.close();
    thd.join();
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

The user can check if these objects contain an allocated shared context with their `bool` conversion. This is easiest to do by using the object as the argument to an `if()` statement. Given an `st::channel` named `my_channel`:
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

For example, the default behavior for `st::channel::close()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. Alternatively, the user can specify a "hard" close by calling `st::channel::close(false)` to immediately end all send and recieve operations on the object, though this is rarely the preferred behavior. 

The user can call `bool closed()`on these objects to check if an object has been closed or is still running. 

#### Example
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

void looping_recv(st::channel ch) {
    st::message msg;

    // it is possible to manually receive values instead of through iterators
    while(ch.recv(msg)) {
        std::string s;
        if(msg.data().copy_to(s)) {
            std::cout << s << std::endl;
        }
    }
}

int main() {
    st::channel my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);

    my_channel.send(0, "you say goodbye");
    my_channel.send(0, "and I say hello");
    // close channel and join thread
    my_channel.close();
    my_thread.join(); 
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

Instead, the user can create an `st::reply` object to abstract sending a response back over an `st::channel` which abstracts information about the receiver so the sender doesn't have to know about it.

`st::reply::make(...)` will take an `st::channel` and an unsigned integer `st::message` id. When `st::reply::send(T t)` is called, an `st::message` containing the given `st::message` id and an optional payload `t` is sent to the stored `st::channel`.

#### Example
```
#include <iostream>
#include <string>
#include <thread>
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
            // this thread doesn't know who they are receiving from 
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
    std::thread thd_a(childThreadA, ch_a);
    std::thread thd_b(childThreadB, ch_b, main_ch);

    // create an `st::reply` to send a message to `ch_b` and send to `ch_a`
    st::reply rep_b = st::reply(ch_b, opB::receive_value);
    ch_a.send(opA::request_value, rep_b);

    // wait for childThreadB to process the response from childThreadA 
    st::message msg;
    main_ch.recv(msg); 

    // close and join child threads 
    ch_a.close();
    ch_b.close();
    thd_a.join();
    thd_b.join();
    return 0;
}
```

Terminal output might be:
```
$./a.out 
received foofaa!
```

### Dealing with Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid calling functions which will block for indeterminate periods within an thread. If the user needs to call such a function, a solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a separate, dedicated system thread, then `send()` the result back to through the caller's `st::channel` when the call completes. 

As a convenience, `st::channel::async()` is provided for exactly this purpose, sending an `st::message` back through the channel to a receiver when the scheduled function (actually any Callable) completes. The resulting `st::message` will have the same id passed to `st::channel::async()` and the payload will be the return value of the executed function:
- `st::channel::async(std::size_t resp_id, user_function, optional_function_args ...)` 

If the user function returns `void`, the `st::message::data()` will be unallocated (`st::data::operator bool()` will return `false`).

The user can implement a simple timer mechanism using this functionality by calling `std::this_thread::sleep_for(...)` inside of `user_function`.

#### Example
```
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sthread>

enum op {
    print,
    slow_function,
    slow_result
};

std::string slow_function() {
    // long operation
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return std::string("that's all folks!");
};

void child_thread(st::channel ch, st::channel main_ch) {
    int cnt = 0;

    for(auto msg: ch) { 
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
                ch.async(op::slow_result, slow_function);
                break;
            case op::slow_result:
            {
                std::string s;
                if(msg.data().copy_to(s) {
                    ch.send(op::print, s);
                    // let main thread know we processed the slow result
                    main_ch.send(0); 
                }
                break;
            }
        }
    }
}

int main() {
    st::channel ch = st::channel::make();
    st::channel main_ch = st::channel::make();
    std::thread thd(child_thread, ch, main_ch);
    ch.send(MyClass::op::slow_function);
    ch.send(MyClass::op::print, std::string("1"));
    ch.send(MyClass::op::print, std::string("2"));
    ch.send(MyClass::op::print, std::string("3"));

    // wait for child thread to inidicate it received the slow result
    st::message msg;
    main_ch.recv(msg);

    // default `st::channel::close()` behavior allows all previously sent messages 
    // to be received before calls to `st::channel::recv()` start failing
    ch.close();
    thd.join(); 
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

### Scheduling Functions on User Threads 
`st::channel`s provide the ability to enqueue functions (actually any Callable) for asynchronous execution with `st::channel::schedule(...)` API. When a message containing a scheduled function is received by a thread it will automatically be executed on that thread before resuming waiting for incoming messages. 

In simple terms, `st::channel::async()` executes code on another background thread generated for that purpose, while `st::channel::schedule()` executes code on a thread the user manages (the message receiver).

`st::channel::schedule()` can accept any Callable function, functor, or lambda function, alongside optional arguments, in a similar fashion to standard library features `std::async()` and `std::thread()`.

#### Example
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
    st::channel ch = st::channel::make();
    // blindly receive messages to process incoming scheduled messages
    std::thread thd([ch]{ for(auto msg : ch) { } }); 
    ch.schedule(print, "what a beautiful day");
    ch.schedule(PrintFunctor, "looks like rain");
    ch.schedule([]{ std::cout << "what a beautiful sunset" << std::endl; });
    ch.close();
    thd.join();
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

### Interprocess Considerations
While `st::channel`s are useful for communicating between a single process's threads, they cannot be used for communicating between processes. A simple method for unifying interprocess and interthread communication is to use one thread as a translator, receiving interprocess messages and forwarding them to a main thread over an `st::channel`.

#### Example 
Given this theoretical `interprocess_messaging.h` API header
```
#ifndef INTERPROCESS_MESSAGING
#define INTERPROCESS_MESSAGING

#define INTERPROCESS_MESSAGE_BUFFER_SIZE 2048

struct interprocess_message {
    ssize_t id; // message id
    char buffer[INTERPROCESS_MESSAGE_BUFFER_SIZE]; // buffer filled by receive function  
    ssize_t size; // payload data size
};

enum INTERPROCESS_ERRORS {
    // ...
};

typedef int HANDLE;

extern int interprocess_open_queue(const char* queue_name, HANDLE* hdl);
extern int interprocess_send_message(HANDLE queue, message* msg);
extern int interprocess_recv_message(HANDLE queue, message* msg);
extern int interprocess_close_queue(HANDLE);
#endif
```

With the user's `my_public_api.h` API header for interprocess communication:
```
#ifndef MY_PUBLIC_API
#define MY_PUBLIC_API
const char* INTERPROCESS_QUEUE_NAME = "my_interprocess_queue_name"

enum my_public_api {
    OPERATION_1 = 0,
    OPERATION_2 = 1,
    // etc...
    SHUTDOWN,
    MY_PUBLIC_API_RESERVED // do not put values after this 
};
#endif
```

And an internal `my_api.h` APID header for interthread communication:
```
#ifndef MY_API
#define MY_API
#include  "my_public_api.h"

enum my_api {
    // internal operations start on reserved value
    internal_operation_1 = MY_PUBLIC_API_RESERVED,  
    internal_operation_2,
    // etc...
    internal_operation_n
}
```

```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>
#include "interprocess_messaging.h"
#include "my_api.h"

int interprocess_receive_loop(st::channel ch, HANDLE hdl) {
    int error = 0;
    interprocess_message msg;
    memset(&msg, 0, sizeof(interprocess_message));
    bool cont = true;

    while(cont && 0 == error = interprocess_recv_message(hdl, &msg)) { 
        switch(msg.id) {
            case SHUTDOWN:
                cont = false;
                ch.send(msg.id, msg);
                break;
            default:
                ch.send(msg.id, msg);
                break;
        }
    }

    if(0 != error) {
        std::cout << "interprocess queue receive failed with error[" << error << "]" << std::endl;
    }

    if(0 != error = interprocess_close_queue(hdl)) {
        std::cout << "failed to close interprocess queue with error[" << error << "]" << std::endl;
    }
}

int main() {
    int ret = 0;
    st::channel ch = st::channel::make();
    
    HANDLE hdl = interprocess_open_queue(INTERPROCESS_QUEUE_NAME);

    if(hdl) {
        std::thread interprocess_recv_thread(interprocess_receive_loop, ch, hdl);

        // launch any other child threads...

        // handle incoming messages
        for(auto msg : ch) {
            switch(msg.id()) {
                case OPERATION_1:
                    // ...
                    break;
                case OPERATION_2:
                    // ...
                    break;
                // handle other cases
                case SHUTDOWN:
                    ret = 0;
                    ch.close();
                    break;
                case my_api::internal_operation_1:
                    // ...
                    break;
                case my_api::internal_operation_2:
                    // ...
                    break;
                // ...
                case my_api::internal_operation_n:
                    // ...
                    break;
            }
        }
    } else {
        std::cout << "failed to open interprocess queue[" << INTERPROCESS_QUEUE_NAME << "]" << std::endl;
        ret = 1;
    }

    return ret;
}
```
