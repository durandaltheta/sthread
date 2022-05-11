# Simple threading and communication.

[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/simple_thread_tst.cpp)

## Requirements
- C++11

## Installation
- cmake .
- sudo make install

## Usage
- Install the library and include the header `simple_thread.hpp`
- Create a class or struct with `void operator()(std::shared_ptr<st::message>)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::worker::make<YourClass>()`

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

### Singleton Service Threads
Singleton worker threads can be accessed (and launched as necessary) by reference with the `st::service<FUNCTOR>()` function.

#### Example 2
```
#include <iostream>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        say_something
    };

    inline void operator()(std::shared_ptr<st::message> msg) {
        switch(msg->id()) {
            case op::say_something:
                std::cout << "I'm a singleton worker thread!" << std::endl;
                break;
        }
    }
};

int main() {
    st::service<MyClass>().send(MyClass::op::say_something);
}
```

Terminal output might be:
```
$./a.out
I'm a singleton worker thread!
```

### Message Payload Data
Message data payloads can be of any type and can be copied to argument `T t` with `st::message::copy_data_to<T>(T&& t)` or rvalue swapped with `st::message::move_data_to<T>(T&& t)`.

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
`st::message::copy_data_to<T>(T&& t)` and `st::message::move_data_to<T>(T&& t)` will return `true` only if the stored payload type matches type `T`, otherwise it returns `false`. Payload types can also be easily checked with `st::message::is<T>()` (returns `true` if type match, else `false`) which is useful if a message might contain several different potential types.

#### Example 4:
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
`st::worker`s can be passed constructor arguments in `st::worker::make<FUNCTOR>(As&&...)`. The FUNCTOR class will be created on the new thread and destroyed before said thread ends.

#### Example 5
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

#### Example 6:
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
- `st::shutdown_all_services(/* default true */)`
- `st::restart_all_services(/* default true */)`

Alternatively, the user can call said functions with explicit `false` to immediately end all operations on the channel:
- `st::channel::close(false)`
- `st::worker::shutdown(false)`
- `st::worker::restart(false)`
- `st::shutdown_all_services(false)`
- `st::restart_all_services(false)`

#### Example 7:
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
