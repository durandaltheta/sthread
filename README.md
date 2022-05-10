Simple threading and communication.

[Documentation](https://durandaltheta.github.io/sthread/)

[Unit Test Examples](tst/simple_thread_tst.cpp)

Install:
- cmake .
- sudo make install

Usage Steps:
- Install the library or include the header `simple_thread.hpp` somehow
- Include `<simple_thread.hpp>`
- Create a class or struct with `void operator()(std::shared_ptr<st:message>)` to 
handle received messages (also called a 'functor')
- Define some enum to distinguish different messages 
- Launch your thread with `st::worker::make<YourClass>()`

Example:
```
#include <iostream>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        hello,
        world
    };

    inline void operator()(std::shared_ptr<st:message> m) {
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

Singleton worker threads can be accessed by reference with the `st::service<FUNCTOR>()` function:
```
#include <iostream>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        say_something
    };

    inline void operator()(std::shared_ptr<st:message> m) {
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

Message data payloads can be of any type:
```
#include <iostream>
#include <string>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        print
    };

    inline void operator()(std::shared_ptr<st:message> m) {
        switch(msg->id()) {
            case op::print:
            {
                std::string s;
                if(msg->copy_data_to<std::string>(s)) {
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

`st::message::copy_data_to<T>(T&& t)` and `st::message::move_data_to<T>(T&& t)` will return true only if the stored payload type matches type `T`, otherwise it returns false. Payload types can also be easily checked with `st::message::is<T>()` (returns true if type match, else false) which is useful if a message might contain several different potential types:
```
#include <iostream>
#include <string>
#include <simple_thread.hpp>

struct MyClass {
    enum op {
        print
    };

    inline void operator()(std::shared_ptr<st:message> m) {
        switch(msg->id()) {
            case op::print:
                if(msg->is<std::string>()) {
                    std::string s;
                    if(msg->copy_data_to<std::string>(s)) {
                        std::cout << s;
                    }
                } else if(msg->is<int>()) {
                    int i;
                    if(msg->copy_data_to<int>(i)) {
                        std::cout << i;
                    }
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

