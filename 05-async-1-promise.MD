# C++11多线程-异步运行之std::promise
前面介绍了C++11的std::thread、std::mutex以及std::condition_variable，并实现了一个多线程通信的chan类，虽然由于篇幅的限制，该实现有些简陋，甚至有些缺陷，但对于一般情况应该还是够用了。在C++11多线程系列的最后会献上chan的最终版本，敬请期待。<p>
本文将介绍C++11的另一大特性：异步运行(std::async)。async顾名思义是将一个函数A移至另一线程中去运行。A可以是静态函数、全局函数，甚至类成员函数。在异步运行的过程中，如果A需要向调用者输出结果怎么办呢？std::async完美解决了这一问题。在了解async的解决之道前，我们需要一些知识储备，那就是：std::promise、std::packaged_task和std::future。异步运行涉及的内容较多，我们会分几节来讲。
## 1. std::promise
std::promise是一个模板类: ```template<typename R> class promise```。其泛型参数R为std::promise对象保存的值的类型，R可以是void类型。std::promise保存的值可被与之关联的std::future读取，读取操作可以发生在其它线程。std::promise**允许move**语义(右值构造，右值赋值)，但**不允许拷贝**(拷贝构造、赋值)，std::future亦然。std::promise和std::future合作共同实现了多线程间通信。
### 1.1 设置std::promise的值
通过成员函数set_value可以设置std::promise中保存的值，该值最终会被与之关联的std::future::get读取到。**需要注意的是：set_value只能被调用一次，多次调用会抛出std::future_error异常**。事实上std::promise::set_xxx函数会改变std::promise的状态为ready，再次调用时发现状态已要是reday了，则抛出异常。
```c++
#include <iostream> // std::cout, std::endl
#include <thread>   // std::thread
#include <string>   // std::string
#include <future>   // std::promise, std::future
#include <chrono>   // seconds
using namespace std::chrono;

void read(std::future<std::string> *future) {
    // future会一直阻塞，直到有值到来
    std::cout << future->get() << std::endl;
}

int main() {
    // promise 相当于生产者
    std::promise<std::string> promise;
    // future 相当于消费者, 右值构造
    std::future<std::string> future = promise.get_future();
    // 另一线程中通过future来读取promise的值
    std::thread thread(read, &future);
    // 让read等一会儿:)
    std::this_thread::sleep_for(seconds(1));
    // 
    promise.set_value("hello future");
    // 等待线程执行完成
    thread.join();

    return 0;
}
// 控制台输: hello future
```
与std::promise关联的std::future是通过std::promise::get_future获取到的，自己构造出来的无效。**一个std::promise实例只能与一个std::future关联共享状态**，当在同一个std::promise上反复调用get_future会抛出future_error异常。<p>
共享状态。在std::promise构造时，std::promise对象会与共享状态关联起来，这个共享状态可以存储一个R类型的值或者一个由std::exception派生出来的异常值。通过std::promise::get_future调用获得的std::future与std::promise共享相同的共享状态。
### 1.2 当std::promise不设置值时
如果promise直到销毁时，都未设置过任何值，则promise会在析构时自动设置为std::future_error，这会造成std::future.get抛出std::future_error异常。
```c++
#include <iostream> // std::cout, std::endl
#include <thread>   // std::thread
#include <future>   // std::promise, std::future
#include <chrono>   // seconds
using namespace std::chrono;

void read(std::future<int> future) {
    try {
        future.get();
    } catch(std::future_error &e) {
        std::cerr << e.code() << "\n" << e.what() << std::endl;
    }
}

int main() {
    std::thread thread;
    {
        // 如果promise不设置任何值
        // 则在promise析构时会自动设置为future_error
        // 这会造成future.get抛出该异常
        std::promise<int> promise;
        thread = std::thread(read, promise.get_future());
    }
    thread.join();

    return 0;
}
```
上面的程序在Clang下输出：
```console
future:4
The associated promise has been destructed prior to the associated state becoming ready.
```
### 1.3 通过std::promise让std::future抛出异常
通过std::promise.set_exception函数可以设置自定义异常，该异常最终会被传递到std::future，并在其get函数中被抛出。
```c++
#include <iostream>
#include <future>
#include <thread>
#include <exception>  // std::make_exception_ptr
#include <stdexcept>  // std::logic_error

void catch_error(std::future<void> &future) {
    try {
        future.get();
    } catch (std::logic_error &e) {
        std::cerr << "logic_error: " << e.what() << std::endl;
    }
}

int main() {
    std::promise<void> promise;
    std::future<void> future = promise.get_future();

    std::thread thread(catch_error, std::ref(future));
    // 自定义异常需要使用make_exception_ptr转换一下
    promise.set_exception(
        std::make_exception_ptr(std::logic_error("caught")));
    
    thread.join();
    return 0;
}
// 输出：logic_error: caught
```
std::promise虽然支持自定义异常，但它并不直接接受异常对象：
```c++
// std::promise::set_exception函数原型
void set_exception(std::exception_ptr p);
```
自定义异常可以通过位于头文件exception下的std::make_exception_ptr函数转化为std::exception_ptr。
### 1.4 std::promise\<void\>
通过上面的例子，我们看到```std::promise<void>```是合法的。此时std::promise.set_value不接受任何参数，仅用于通知关联的std::future.get()解除阻塞。

### 1.5 std::promise所在线程退出时
std::async(异步运行)时，开发人员有时会对std::promise所在线程退出时间比较关注。std::promise支持定制线程退出时的行为：
 * std::promise.set_value_at_thread_exit 线程退出时，std::future收到通过该函数设置的值
 * std::promise.set_exception_at_thread_exit 线程退出时，std::future则抛出该函数指定的异常。

关于std::promise就是这些，本文从使用角度介绍了std::promise的能力以及边界，读者如果想更深入了解该类，可以直接阅读一下源码。
