# Simple C++ Threading and Communication
## Quick Links

The overall design of code in this library relies heavily on virtual interfaces to implement inherited behavior. Visit the documentation for information and features not detailed in this README.

[Documentation](https://durandaltheta.github.io/sthread/annotated.html)

[Unit Test and Example Code](tst/)

## Quick Links:
[Simple Message Passing](#simple-message-passing) 

[A Closer Look at Channel Send Operations](#a-closer-look-at-channel-send-operations) 

[Message Data Payloads](#message-data-payloads)

[Payload Data Type Checking](#payload-data-type-checking)

[Object Lifecycles](#object-lifecycles)

[Closing Channels](#closing-channels)

[Abstracting Message Replies](#abstracting-message-replies)

[Dealing with Blocking Functions](#dealing-with-blocking-functions)

[Scheduling Functions on User Threads](#scheduling-functions-on-user-threads)

[Interprocess Considerations](#interprocess-considerations)

## Purpose 
This library's purpose is to simplify setting up useful c++ threading, and to enable trivial inter-thread message passing of C++ objects. That is, message passing with support for proper C++ construction and destruction instead of blind `memcpy()` calls, allowing C++ objects to be passed around.

The library provides a handful of objects which support this goal:
- `st::channel`
- `st::message`
- `st::reply`
- `st::task`

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

`sthread_tst` binary will be placed in directory `tst/`

## Usage 
### Simple message passing 
All that is required to send/receive messages between threads is that each thread has a copy of a constructed `st::channel` object. Messages can be sent with a call to `bool st::message::send(...)` and messages can be received by calling `bool st::channel::recv(st::message& msg)`. However, the simplest solution is to make use of `st::channel` iterators in a range-for loop. `st::channel` iterators can also be returned with calls to `st::channel::begin()` and `st::channel::end()`.

#### Example 
[example source](tst/message_passing_ex.cpp)
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

void childThread() {
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

It should be noted that the `auto` keyword can be used for objects contructed by `make()` calls.

### Message Data Payloads
`st::data()` can store any data type. Usually the user will not need to manually construct `st::data()`, as `st::message::make(...)` methods do this internally. In the cases where manual construction is necessary `st::data` objects can be constructed with a call to `st::data::make<T>(optional_constructor_args...)`, where type `T` is the type to be stored in the result `st::data`.

`st::data` objects are thin wrappers to `std::unique_ptr<T>` objects, and therefore cannot be lvalue copied (ownership between `st::data` objects must be assigned with `destination_data = std::move(source_data)`).

The stored data can be copied to an argument of templated type `T` with `st::data::copy_to(T& t)` or rvalue swapped with `st::data::move_to(T& t)`. Said functions will return `true` if their argument `T` matches the type `T` originally stored in the `st::data`, otherwise they will return `false`.

#### Example
[example source](tst/message_payloads_ex.cpp)
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

enum op {
    print
};

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
    auto my_channel = st::channel::make();
    std::thread my_thread(my_function, my_channel);

    my_channel.send(op::print, std::string("hello again"));
    my_channel.send(op::print, 14);
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
$
```

### Payload Data Type Checking
Payload `st::data` types can also be checked with `bool st::data::is<T>()` (returns `true` if types match, else `false`) which is useful if a message's data might contain several different potential types. If `st::data` is unallocated then `st::data::is<T>()` will always return `false` (`st::data::operator bool()` conversion will also be `false`).

Additionally, the type stored in `st::data` can be cast to a reference with a call to `T& st::data::cast_to<T>()`. However, this functionality is only safe when used inside of an `st::data::is<T>()` check.

WARNING: 

`st::data` can store a payload of any type. However, this behavior can be confusing with regard to c-style `const char*` strings. c-style strings are just pointers to memory, and the user is responsible for ensuring that said memory is accessible outside of the scope when the message is sent. 

Typically, it is safe to send c-style strings with a hardcoded value, as such strings are stored in the program's global data. However, local stack arrays of characters, or, even worse, allocated c-strings must be carefully considered when sending over a message. 

Furthermore, compiler rules around array and pointer conversions may sometimes cause unexpected types to be stored in `st::data` if the type is not explicit. Therefore when using raw c-strings it may be a necessary to explicitly cast it to the expected type whenever assigning a payload to an `st::message`: `st::message::make(my_id, (const char*)"my string")`.

A simple workaround to these headaches is to encapsulate c-style strings within a c++ `std::string`, which will make typing consistent and explicit.

#### Example
[example source](tst/payload_data_type_checking_ex.cpp)
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

enum op {
    print
};

void my_function(st::channel ch) {
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

    ch.send(op::print, std::string("hello "));
    ch.send(op::print, 1);
    ch.send(op::print, std::string(" more time\n"));
    ch.close();
    thd.join();
    return 0;
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
$
```

### Object Lifecycles
The objects in this library are actually shared pointers to some shared context, whose context needs to be allocated with a static call to their respective `make()` functions. Objects in this category include:
- `st::message`
- `st::channel`
- `st::reply`
- `st::task`

The user can check if these objects contain an allocated shared context with their `bool` conversion. This is easiest to do by using the object as the argument to an `if()` statement. Given an `st::channel` named `my_channel`:
```
if(my_channel) {
    // my_channel is allocated
} else {
    // my_channel is not allocated
}
```

Attempting to use API of these objects when they are *NOT* allocated (other than static allocation functions or the `bool` conversion) will typically result in a failure return value of `false`. Alternatively, a successful call will return `true`. This means that users generally don't need to handle exceptions, but they do need to pay attention to return values of methods.

When the last object holding a copy of some shared context goes out of scope, that object will be neatly shutdown and be destroyed. As such, the user is responsible for keeping copies of the above objects when they are created with an allocator function (`make()`), otherwise they may unexpectedly shutdown.

### Closing Channels
Operations on `st::channel`'s shared context can be shutdown early with a call to `close()`, causing operations which use that shared context to fail.

For example, the default behavior for `st::channel::close()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. Alternatively, the user can specify a "hard" close by calling `st::channel::close(false)` to immediately end all send and recieve operations on the object, though this is rarely the preferred behavior. 

The user can call `bool closed()`on these objects to check if an object has been closed or is still running. 

#### Example
[example source](tst/closing_channels_ex.cpp)
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
    auto my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);

    my_channel.send(0, std::string("you say goodbye"));
    my_channel.send(0, std::string("and I say hello"));
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
$
```

### Abstracting Message Replies 
Dealing with enumerations when message passing can be complicated when API grows large and painful when enumerations conflict with each other. This typically requires abstraction between multiple APIs such that multiple points in the program do not know anything about the other.

As a convenience, the user can create an `st::reply` object to abstract sending a response back over an `st::channel` which hides information about the receiver, simplifying request/response API.

`st::reply::make(...)` will take an `st::channel` and an unsigned integer `st::message` id. When `st::reply::send(T t)` is called, an `st::message` containing the given `st::message` id and an optional payload `t` is sent to the stored `st::channel`.

#### Example
[example source](tst/abstracting_message_replies_ex.cpp)
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

enum opA {
    request_value = 0 // send a value back to a requestor
};

enum opB {
    // same enumeration value as opA::request_value, normally a potential bug
    receive_value = 0 
};

void childThreadA(st::channel ch) {
    std::string value = "foofaa";

    for(auto msg : ch) {
        switch(msg.id()) {
            case opA::request_value:
            {
                // this thread doesn't know anything about who they are replying to
                if(msg.data().is<st::reply>()) {
                    msg.data().cast_to<st::reply>().send(value);
                }
                break;
            }
        }
    }

}

void childThreadB(st::channel ch, st::channel value_received_conf_ch) {
    for(auto msg : ch) {
        switch(msg.id()) {
            // this thread doesn't know who they are receiving from 
            case opB::receive_value:
            {
                std::string s;
                if(msg.data().copy_to(s)) {
                    std::cout << "received " << s << "!" << std::endl;
                    value_received_conf_ch.send();
                }
                break;
            }
        }
    }
};

int main() {
    // launch child threads
    auto ch_a = st::channel::make(); 
    std::thread thd_a(childThreadA, ch_a);

    auto ch_b = st::channel::make(); 
    auto value_received_conf_ch = st::channel::make(); 
    std::thread thd_b(childThreadB, ch_b, value_received_conf_ch);

    // create an `st::reply` to forward a value to `ch_b`
    auto rep_b = st::reply::make(ch_b, opB::receive_value);

    // send the request for a value over `ch_a`
    ch_a.send(opA::request_value, rep_b);

    // wait for childThreadB to process the response from childThreadA 
    st::message msg;
    value_received_conf_ch.recv(msg); 

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
$
```

### Dealing with Blocking Functions 
To ensure messages are processed in a timely manner, and to avoid deadlock in general, it is important to avoid calling functions which will block for indeterminate periods within a message receiving thread. If the user needs to call such a function a solution is to make use of the standard library's `std::async()` feature to execute arbitrary code on a separate, dedicated system thread, then `send()` the result back to through the caller's `st::channel` when the call completes. 

`st::channel::async()` is provided for exactly this purpose, sending an `st::message` back through the channel to a receiver when the scheduled function (actually any `Callable`) completes. The resulting `st::message` will have the same id passed to `st::channel::async()` and the payload will be the return value of the executed function:

`st::channel::async(std::size_t resp_id, user_function, optional_function_args...)` 

Alternatively, if the user function returns `void`, the `st::message::data()` will be unallocated (`st::data::operator bool()` will return `false`).

The user can implement a simple timer mechanism using this functionality by calling `std::this_thread::sleep_for(...)` inside of `user_function`. Another convenience, `st::channel::timer(duration, id, optional_payload)` does exactly this, where `duration` is a `std::chrono::duration` sending `id` (and potentially a payload) in a `st::message` over the `st::channel` after timeout.

#### Example
[example source](tst/dealing_with_blocking_functions_ex.cpp)
```
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sthread>

enum op {
    timeout
};

// variant of user_timer returning a value
std::string user_timer(std::chrono::milliseconds ms, std::string s) {
    std::this_thread::sleep_for(ms);
    std::cout << "sleep ended on temporary thread" << std::endl;
    return s;
}

// variant of user_timer returning void
void user_timer(std::chrono::milliseconds ms) {
    std::this_thread::sleep_for(ms);
    std::cout << "sleep ended on temporary thread with no return" << std::endl;
}

void process_timeouts(st::channel ch, st::channel timeout_conf_ch) {
    for(auto msg: ch) { 
        switch(msg.id()) {
            case op::timeout:
                std::cout << "timeout detected" << std::endl;

                if(msg.data().is<std::string>()) {
                    std::cout << msg.data().cast_to<std::string>() << std::endl;
                }

                // let main thread know we processed the timeout
                timeout_conf_ch.send(0); 
                break;
        }
    }
}

int main() {
    auto ch = st::channel::make();
    auto timeout_conf_ch = st::channel::make();
    std::thread thd(process_timeouts, ch, timeout_conf_ch);

    ch.async(op::timeout, 
             user_timer, 
             std::chrono::milliseconds(100), 
             std::string("that's all folks!"));

    ch.async(op::timeout, user_timer, std::chrono::milliseconds(200));

    ch.timer(op::timeout, std::chrono::milliseconds(300), std::string("timer with payload"));
    ch.timer(op::timeout, std::chrono::milliseconds(400));

    // wait for child thread to indicate it received the timeout confirmation
    st::message msg;
    timeout_conf_ch.recv(msg);
    timeout_conf_ch.recv(msg);
    timeout_conf_ch.recv(msg);
    timeout_conf_ch.recv(msg);

    ch.close();
    thd.join(); 
    return 0; 
}
```

Terminal output might be:
```
$./a.out
sleep ended on temporary thread
timeout detected
that's all folks!
sleep ended on temporary thread with no return
timeout detected
timeout detected
timer with payload
timeout detected
$
```

### Scheduling Functions on User Threads 
The utility type `st::task` provides the ability to wrap any `Callable` (and optionally any arguments) for execution. 

A `Callable` is any data or object which can be executed like a function including:
- functions 
- function pointers 
- functors (ex: std::function)
- lambdas 

`st::task::make(Callable, optional_arguments...)` can be invoked to make a `st::task` which will wrap it's arguments into a task for the user. `st::task` objects can be invoked with the `()` operator.

`st::task` objects when invoked will return a reference to an `st::data` value when invoked containing the returned value of the wrapped `Callable`. If wrapped `Callable` returns void, the resulting `st::data&` will be empty and `== false` when used in an `if` statement.

`st::task` objects are 'lazy', in that once they have been evaluated once, further evaluations will immediately return the previously returned value with no further work.

#### Example
[example source](tst/scheduling_functions_on_user_threads_ex.cpp)
```
#include <iostream>
#include <sthread>

int foo(int a) {
    std::cout << "foo: " << a << std::endl;
    return a + 1;
}

int main() {
    auto foo_task = st::task::make(foo, 3);
    if(foo_task().is<int>()) {
        // can safely invoke task again because it will immediately return its
        // previous result
        std::cout << "result: " << foo_task().cast_to<int>() << std::endl;
    }
    return 0;
}
```
Terminal output might be:
```
$./a.out 
foo: 3
result: 4
$
```

`st::task`s can be sent over `st::channel`s to implement arbitrary code execution worker threads:

#### Example
[example source](tst/scheduling_functions_on_user_threads_ex.cpp)
```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>

void print(const char* s) {
    std::cout << s << std::endl;
}

struct PrintFunctor { 
    void operator()(const char* s) {
        std::cout << s << std::endl;
    }
};

void executor(st::channel ch) {
    for(auto msg : ch) { 
        // execute any received tasks
        if(msg.data().is<st::task>()) {
            msg.data().cast_to<st::task>()();
        }
    } 
}

int main() {
    auto printer_lambda = []{ std::cout << "what a beautiful sunset" << std::endl; };
    auto ch = st::channel::make();
    std::thread thd(executor, ch); 

    // in this example, message id's are arbitrary
    ch.send(0, st::task::make(print, "what a beautiful day"));
    ch.send(0, st::task::make(PrintFunctor(), "looks like rain"));
    ch.send(0, st::task::make(printer_lambda));

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
$
``` 

### Interprocess Considerations
While `st::channel`s are useful for communicating between a single process's threads, they cannot be used for communicating between processes. A simple method for unifying interprocess and interthread communication is to use one thread as a translator, receiving interprocess messages and forwarding them to a main thread over an `st::channel`.

#### Example 
Given this theoretical `interprocess_messaging.h` API header:
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
    SUCCESS = 0,
    // ...
};

// Type HANDLE is some value used by the interprocess mechanism for identifying
// and differentiating queues and endpoints. 
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
    INTERPROCESS_OPERATION_1 = 0,
    INTERPROCESS_OPERATION_2,
    // etc...
    INTERPROCESS_SHUTDOWN,
    MY_PUBLIC_API_RESERVED // do not put values in this enum after this
};
#endif
```

And an internal `my_api.h` API header for interthread communication which
extends the message types defined in `my_public_api.h`:
```
#ifndef MY_API
#define MY_API
#include  "my_public_api.h"

enum my_api {
    // internal operations start on reserved value
    interprocess_receive_error = MY_PUBLIC_API_RESERVED,  
    internal_operation_2,
    // etc...
    internal_operation_n
};
#endif
```

```
#include <iostream>
#include <string>
#include <thread>
#include <sthread>
#include "interprocess_messaging.h"
#include "my_api.h"

void interprocess_receive_loop(st::channel ch, HANDLE hdl) {
    int error = 0;
    interprocess_message ipmsg;
    memset(&ipmsg, 0, sizeof(interprocess_message));

    // when main thread closes interprocess queue this loop will end
    while(0 == error = interprocess_recv_message(hdl, &ipmsg)) { 
        // received interprocess_message will be in the `st::message` payload
        ch.send(msg.id, ipmsg);
    }

    if(0 != error) {
        // this will only be handled by the main thread in the case of an 
        // unexpected error, not when the main thread caused the error by 
        // closing the interprocess queue
        ch.send(my_api::interprocess_receive_error, error);
    }
}

int main() {
    int ret = 0;
    int error = 0;
    HANDLE hdl;
    auto ch = st::channel::make();

    if(0 == error = interprocess_open_queue(INTERPROCESS_QUEUE_NAME, &hdl)) {
        std::thread interprocess_receive_thread(interprocess_receive_loop, ch, hdl);

        // launch any other child threads...

        // handle incoming messages
        for(auto msg : ch) {
            switch(msg.id()) {
                case INTERPROCESS_OPERATION_1:
                    // ...
                    break;
                case INTERPROCESS_OPERATION_2:
                    // ...
                    break;
                // handle other public API operations...
                case INTERPROCESS_SHUTDOWN:
                    // end message processing and cleanup
                    ch.close(); 
                    break;
                // handle private API operations...
                case my_api::interprocess_receive_error:
                    if(msg.data().copy_to(error)) {
                        std::cerr << "interprocess queue receive failed with error[" << error << "]" << std::endl;
                    } else {
                        std::cerr << "interprocess queue receive failed; message type unknown" << std::endl;
                    }
                    ret = 1;
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
        
        // shutdown interprocess queue to end interprocess_receive_thread
        if(0 != error = interprocess_close_queue(hdl)) {
            std::cerr << "interprocess queue close failed with error[" << error << "]" << std::endl;
            ret = 1;
        }

        interprocess_receive_thread.join();

        // shutdown and join any other child threads...
    } else {
        std::cerr << "failed to open interprocess queue[" << INTERPROCESS_QUEUE_NAME << "] with error[" << error << "]" << std::endl;
        ret = 1;
    }

    return ret;
}
```
