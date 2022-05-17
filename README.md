# Simple Threading and Communication

[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/simple_thread_tst.cpp)

## Requirements
- C++11

## Installation
- cmake .
- sudo make install 

## Purpose 
This library seeks to easily setup useful worker threads using a simple API.

Instead of functions worker threads execute c++ functors. A functor is a class 
which has a function call overload allowing you to execute the functor like a 
function, IE:
```
struct MyClass {
    inline void operator()(std::shared_ptr<st::message> msg) { /* ... */ }
};
```

Functors (as used by this library) have several advantages over raw functions. 
- The worker thread message receive loop is managed by the library 
- The worker lifecycle is managed by the library 
- Sending messages to the worker is provided by the library 
- Functors allow for inheritance
- Functors allow for public enumerations to be defined as part of its
  namespace, which is useful for organizing what messages the thread will
  listen for.
- Functors allow for class method definitions, instead of forcing the user to rely on lambdas or global namespace functions if further function calls are desired.
- Initialization (constructor), runtime execution (`void operator()(std::shared_ptr<st::message>`), and deinitialization (destructor) are broken in to separate functions, which I think makes them more readable. A thread running only a raw function requires everything be managed within that function.

## Usage
- Install the library and include the header `simple_thread.hpp`
- Create a class or struct with `void operator()(std::shared_ptr<st::message>)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::worker::make<YourClassNameHere>()`

### Basic Usage
#### Example 1:
```
#include <iostream>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        hello,
        world
    };

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
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
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    my_worker->send(MyClass::op::hello);
    my_worker->send(MyClass::op::world);
}
```

Terminal output might be:
```
$./a.out
hello world
```

### Message Payload Data
Message data payloads can be of any type and can be copied to argument `T t` with `st::message::copy_data_to<T>(T&& t)` or rvalue swapped with `st::message::move_data_to<T>(T&& t)`. 

`st::message::copy_data_to<T>(T&& t)` and `st::message::move_data_to<T>(T&& t)` will return `true` only if the stored payload type matches type `T`, otherwise it returns `false`. 
#### Example 2:
```
#include <iostream>
#include <string>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        print
    };

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::print:
            {
                std::string s;
                if(msg->copy_data_to(s)) {
                    std::cout << s << std::endl;
                }
                break;
            }
        }
    }
};

int main() {
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    std::string s("hello again");
    my_worker->send(MyClass::op::print, s);
}
```

Terminal output might be:
```
$./a.out
hello again
```

### Payload Type Checking
Payload types can also be easily checked with `st::message::is<T>()` (returns `true` if types match, else `false`) which is useful if a message might contain several different potential types.

#### Example 3:
```
#include <iostream>
#include <string>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        print
    };

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::print:
                if(msg->is<std::string>()) {
                    std::string s;
                    msg->copy_data_to(s);
                    std::cout << s;
                } else if(msg->is<int>()) {
                    int i = 0;
                    msg->copy_data_to(i);
                    std::cout << i;
                }
                break;
        }
    }
};

int main() {
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>();

    std::string s("hello ");
    my_worker->send(MyClass::op::print, s);
    int i = 1;
    my_worker->send(MyClass::op::print, i);
    s = " more time\n";
    my_worker->send(MyClass::op::print, s);
}
```

Terminal output might be:
```
$./a.out
hello 1 more time
```

`st::worker`s will automatically shutdown and join when they are destructed. This can be done early with `st::worker::shutdown()`. `st::worker`s can also be restarted at any time with `st::worker::restart()`. See 
[documentation](https://durandaltheta.github.io/sthread/) for more info.


### Worker Constructor Arguments and Lifecycle
`st::worker`s can be passed constructor arguments in `st::worker::make<FUNCTOR>(As&&...)`. The `FUNCTOR` class will be created on the new thread and destroyed before said thread ends.

An `st::worker`'s `std::thread` will be shutdown and joined when any of the following happens:
- The `st::worker`s last `std::shared_ptr` goes out of scope
- `st::worker::shutdown()` is called on a worker
- `st::worker::restart()` is called on a worker (and a new `std::thread` and `FUNCTOR` will be created before `restart()` returns)

#### Example 4
```
#include <iostream>
#include <string>
#include <simple_thread.hpp>

struct MyClass {
    MyClass(std::string constructor_string, std::string destructor_string) :
        m_destructor_string(destructor_string)
    {
        std::cout << std::this_thread::get_id() << ":" << constructor_string << std::endl;
    }

    ~MyClass() {
        std::cout << std::this_thread::get_id() << ":" <<  m_destructor_string << std::endl;
    }

    inline void operator()(std::shared_ptr<st::message> msg) { }

    std::string m_destructor_string;
};

int main() {
    std::cout << std::this_thread::get_id() << ":" <<  "parent thread started" << std::endl;
    std::shared_ptr<st::worker> wkr = st::worker::make<MyClass>("hello", "goodbye");
}

```

Terminal output might be:
```
$./a.out 
0x800018040:parent thread started
0x800098150:hello
0x800098150:goodbye
```


### Channels
The object that `st::worker`s use for communication in their `send()` methods is called `st::channel`. `st::channel`s can be created and used outside of `st::worker` objects if desired. 

#### Example 5:
```
#include <iostream>
#include <thread>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        forward
    };

    MyClass(std::shared_ptr<st::channel> fwd_ch) : m_fwd_ch(fwd_ch) { }

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::forward:
                m_fwd_ch->send(msg);
                break;
        }
    }

    std::shared_ptr<st::channel> m_fwd_ch;
};

int main() {
    std::shared_ptr<st::channel> my_channel = st::channel::make();
    std::shared_ptr<st::worker> my_worker = st::worker::make<MyClass>(my_channel);

    my_worker->send(MyClass::op::forward, std::string("forward this string"));
    
    std::shared_ptr<st::message> msg;
    my_channel->recv(msg);

    std::string s;
    if(msg->copy_data_to(s)) {
        std::cout << s << std::endl;
    }
}
```

Terminal output might be:
```
$./a.out 
forward this string
```

### Close, Shutdown, and Restart
In looping `st::channel::recv()` operations `st::channel::close()` can be manually called to force all operations to cease on the `st::channel` (operations will return `false`). The default behavior for `st::channel::close()` is to cause all current and future all `st::channel::send()` operations to fail early but to allow `st::channel::recv()` to continue succeeding until the internal message queue is empty. 

This is the default behavior of several functions:
- `st::channel::close(/* default true */)`
- `st::worker::shutdown(/* default true */)`
- `st::worker::restart(/* default true */)`

Alternatively, the user can call said functions with explicit `false` to immediately end all operations on the channel:
- `st::channel::close(false)`
- `st::worker::shutdown(false)`
- `st::worker::restart(false)`

#### Example 6:
```
#include <iostream>
#include <thread>
#include <simple_thread.hpp>

void looping_recv(std::shared_ptr<st::channel> ch) {
    std::shared_ptr<st::message> msg;

    while(ch->recv(msg)) {
        std::string s;
        if(msg->copy_data_to(s)) {
            std::cout << s << std::endl;
        }
    }

    std::cout << "thread done" << std::endl;
}

int main() {
    std::shared_ptr<st::channel> my_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel);
    std::shared_ptr<st::message> msg;

    my_channel->send(0, std::string("You say goodbye"));
    my_channel->send(0, std::string("And I say hello"));

    my_channel->close(); // end thread looping 
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
