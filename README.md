Simple threading and communication.

[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test and Example Code](tst/simple_thread_tst.cpp)

Requirements:
- C++11

Install:
- cmake .
- sudo make install

Usage Steps:
- Install the library and include the header `simple_thread.hpp`
- Create a class or struct with `void operator()(std::shared_ptr<st::message>)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::worker::make<YourClass>()`

Example 1:
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

Singleton worker threads can be accessed by reference with the `st::service<FUNCTOR>()` function.

Example 2:
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

Message data payloads can be of any type

Example 3:
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

`st::message::copy_data_to<T>(T&& t)` and `st::message::move_data_to<T>(T&& t)` will return `true` only if the stored payload type matches type `T`, otherwise it returns `false`. Payload types can also be easily checked with `st::message::is<T>()` (returns `true` if type match, else `false`) which is useful if a message might contain several different potential types.

Example 4:
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


`st::worker`s can be passed constructor arguments in `st::worker::make<FUNCTOR>(As&&...)`. The FUNCTOR class will be created on the new thread and destroyed before said thread ends.

```
#include <iostream>
#include <string>
#include <simple_thread.hpp>

struct MyClass {
    MyClass(std::string constructor_string, std::string destructor_string) :
        m_destructor_string(destructor_string)
    {
        std::cout << constructor_string << std::endl;
    }

    ~MyClass() {
        std::cout << m_destructor_string << std::endl;
    }

    inline void operator()(std::shared_ptr<st::message> msg) { }

    std::string m_destructor_string;
};

int main() {
    std::shared_ptr<st::worker> wkr = st::worker::make<MyClass>("hello", "goodbye");
}

```

Terminal output might be:
```
$./a.out
hello
goodbye
```


The object that `st::worker`s use for communication in their `send()` methods is called `st::channel`. `st::channel`s can be created and used outside of `st::worker` objects desired. 

Example 6:
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


In looping `st::channel::recv()` operations `st::channel::close()` can be manually called to force all operations to cease on the `st::channel` (operations will return `false`). 

Example 7:
```
#include <iostream>
#include <thread>
#include <simple_thread.hpp>

void looping_recv(std::shared_ptr<st::channel> ch, std::shared_ptr<st::channel> conf_ch) {
    std::shared_ptr<st::message> msg;

    while(ch->recv(msg)) {
        std::string s;
        if(msg->copy_data_to(s)) {
            std::cout << s << std::endl;
        }
        conf_ch->send(0,0);
    }

    std::cout << "thread done" << std::endl;
}

int main() {
    std::shared_ptr<st::channel> my_channel = st::channel::make();
    std::shared_ptr<st::channel> my_confirmation_channel = st::channel::make();
    std::thread my_thread(looping_recv, my_channel, my_confirmation_channel);
    std::shared_ptr<st::message> msg;

    my_channel->send(0, std::string("You say goodbye"));
    my_confirmation_channel->recv(msg); // confirm thread received message

    my_channel->send(0, std::string("And I say hello"));
    my_confirmation_channel->recv(msg);

    my_channel->close(); // end thread looping 
    my_thread.join(); // join thread
}
```
