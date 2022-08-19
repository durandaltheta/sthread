# Simple Threading and Communication
## Quick Links

This library makes extensive use of inherited functionality via interfaces. If 
code lacks documentation, look at the interfaces for more information.
[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/simple_thread_tst.cpp)

[Basic Usage](#basic-usage)

[Advanced Usage](README_ADVANCED.md)

## Purpose 
This header only library seeks to easily setup useful threading & concurrency with a simple API.

Instead of functions `st::thread`s execute c++ functors. A functor is a class 
which has a function call overload allowing you to execute the object like a 
function, IE:
```
struct MyClass {
    inline void operator()(std::shared_ptr<st::message> msg) { /* ... */ }
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
library provides `st::sptr` as a typedef to `std::shared_ptr` and `st::wptr` as 
a typedef to `std::weak_ptr`. Usage of these typedefs is completely optional in 
user code.

## Basic Usage
[Back To Top](#simple-threading-and-communication)

- Install the library and include the header `sthread` or `simple_thread.hpp`
- Create a class or struct with `void operator()(st::sptr<st::message>)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::thread::make<YourClassNameHere>()`
- Trigger user class's `void operator()(st::sptr<st::message>)` via `st::thread::send(/* ... */)` 

#### Example 1:
```
#include <iostream>
#include <sthread>

struct MyClass {
    enum op {
        hello,
        world
    };

    inline void operator()(st::sptr<st::message> msg) {
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
    st::sptr<st::thread> my_thread = st::thread::make<MyClass>();

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

    inline void operator()(st::sptr<st::message> msg) {
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
    st::sptr<st::thread> my_thread = st::thread::make<MyClass>();

    my_thread->send(MyClass::op::print, "hello again");
}
```

Terminal output might be:
```
$./a.out
hello again
```

### Send Operations
Several classes in this library support the ability to send messages:
- `st::channel::send(/* ... *)`
- `st::thread::send(/* ... *)`
- `st::coroutine::send(/* ... *)`
- `st::executor::send(/* ... *)`

Arguments passed to `send(/* ... */)` are subsequently passed to `st::sptr<st::message>  st::message::make(/* ... */)` before the resulting `st::sptr<st::message>` is passed to its destination thread and functor. The summary of the 3 basic overloads of `st::sptr<st::message> st::message::make(/* ... */)` are:
- `st::sptr<st::message> st::message::make(st::sptr<st::message>)`: Returns its argument immediately with no changes
- `template <typename ID> st::sptr<st::message> st::message::make(ID id)`: Returns a constructed message which returns argument unsigned integer `id` as `st::message::id`.
- `template <typename ID, typename T> st::sptr<st::message> st::message::make(ID id, T&& t)`: Same as the previous invocation but additionally accepts and stores a payload `t` of any type (compiler deduced) `T` which can later be copied with `template <typename T> bool st::data::copy_to(T& t)` or moved out of the message with `template <typename T> bool st:data::move_to(T& t)`.

### Type Checking
Payload types can also be easily checked with `st::data::is<T>()` (returns `true` if types match, else `false`) which is useful if a message's data might contain several different potential types. 

NOTE: `st::data` can store a payload of any type. However, it does provide one special exception when passed explicit c-style `char*` strings, where it will automatically convert the argument `char*` into a `std::string` to protect the user against unexpected behavior. However, this means the the user must use `std::string` type when trying to copy or move the data back out. If this behavior is not desired, the user will have to wrap their `char*` in some other object.

Many classes support a similar method also named `is<T>()`:
- `st::thread::is<T>()` // compares against the type of the thread's FUNCTOR 
- `at::coroutine::is<T>()` // compares against the type of the coroutine's FUNCTOR 
- `at::executor::is<T>()` // compares against the type of the executor's worker thread's FUNCTOR

#### Example 3:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        print
    };

    inline void operator()(st::sptr<st::message> msg) {
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
    st::sptr<st::thread> my_thread = st::thread::make<MyClass>();

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

`st::thread`s will automatically shutdown and join when they are destructed. This can be done early with `st::thread::shutdown()`. 
[documentation](https://durandaltheta.github.io/sthread/) for more info.


### Thread Constructor Arguments and Lifecycle
`st::thread`s can be passed constructor arguments in `st::thread::make<FUNCTOR>(As&&...)`. The `FUNCTOR` class will be created on the new thread and destroyed before said thread ends.

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

    inline void operator()(st::sptr<st::message> msg) { }

    std::string m_destructor_string;
};

int main() {
    std::cout << std::this_thread::get_id() << ":" <<  "parent thread started" << std::endl;
    st::sptr<st::thread> wkr = st::thread::make<MyClass>("hello", "goodbye");
}

```

Terminal output might be:
```
$./a.out 
0x800018040:parent thread started
0x800098150:hello
0x800098150:goodbye
```


### Abstracting Message Passing Details
One useful advanced design pattern is abstracting all of the operation details 
for message passing into some API, freeing the user from having to interact 
directly with the thread details.

#### Example 5:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    static inline MyClass make() {
        return MyClass(st::thread::make<thread>());
    }

    inline void set_string(std::string txt) {
        m_wkr->send(op::eset_string, txt);
    }

    inline std::string get_string() {
        auto ret_ch = st::channel::make();
        m_wkr->send(op::eget_string, ret_ch);
        std::string s;
        st::sptr<st::message> msg;
        ret_ch->recv(msg);
        msg->data.copy_to(s);
        return s;
    }

private:
    enum op {
        eset_string,
        eget_string
    };

    struct thread { 
        inline void operator()(st::sptr<st::message> msg) {
            switch(msg->id) {
                case op::eset_string:
                    msg->data.copy_to(m_str);
                    break;
                case op::eget_string:
                {
                    st::sptr<st::channel> ret_ch;
                    if(msg->data.copy_to(ret_ch)) {
                        ret_ch->send(0,m_str);
                    }
                    break;
                }
            }
        }

        std::string m_str;
    };

    MyClass(st::sptr<st::thread> wkr) : m_wkr(wkr) { }

    st::sptr<st::thread> m_wkr;
};

int main() {
    MyClass my_class = MyClass::make();
    my_class.set_string("hello");
    std::cout << my_class.get_string() << std::endl;
    my_class.set_string("hello hello");
    std::cout << my_class.get_string() << std::endl;
}
```

Terminal output might be:
```
$./a.out
hello
hello hello
```


### Channels
The object that `st::thread`s use for communication in their `send()` methods is called `st::channel`. `st::channel`s can be created and used outside of `st::thread` objects if desired. This allows the user, for example, to send messages to threads which were not launched with `st::thread::make<T>()`.

#### Example 6:
```
#include <iostream>
#include <string>
#include <sthread>

struct MyClass {
    enum op {
        forward
    };

    MyClass(st::sptr<st::channel> fwd_ch) : m_fwd_ch(fwd_ch) { }

    inline void operator()(st::sptr<st::message> msg) {
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
    st::sptr<st::thread> my_thread = st::thread::make<MyClass>(my_channel);

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

### Further Object Lifecycles
In looping `st::channel::recv()` operations `st::channel::shutdown()` can be manually called to force all operations to cease on the `st::channel` (operations will return `false`). The default behavior for `st::channel::shutdown()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. 

This is the default behavior of several objects which use `st::channel` internally:
- `st::channel::shutdown(/* default true */)`
- `st::thread::shutdown(/* default true */)`
- `at::cotask::shutdown(/* default true */)`
- `at::executor::shutdown(/* default true */)`

Alternatively, the user can call said functions with explicit `false` to immediately end all operations on the channel:
- `st::channel::shutdown(false)`
- `st::thread::shutdown(false)`
- `at::cotask::shutdown(false)`
- `at::executor::shutdown(false)`

NOTE: When an closable/shutdownable object goes out of scope (no more `st::sptr` for that object instance exists), the object will be shutdown with default behavior (if the object is not already shutdown).

#### Example 7:
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
