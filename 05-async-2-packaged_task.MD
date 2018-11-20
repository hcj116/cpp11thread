# C++11多线程-异步运行之std::packaged_task
上一篇介绍的std::promise通过set_value可以使得与之关联的std::future获取数据。本篇介绍的std::packaged_task则更为强大，它允许传入一个函数，并将函数计算的结果传递给std::future，包括函数运行时产生的异常。下面我们就来详细介绍一下它。
## 2. std::package_task
在开始std::packaged_task之前我们先看一段代码，对std::packaged_task有个直观的印象，然后我们再进一步介绍。
```c++
#include <thread>   // std::thread
#include <future>   // std::packaged_task, std::future
#include <iostream> // std::cout

int sum(int a, int b) {
    return a + b;
}

int main() {
    std::packaged_task<int(int,int)> task(sum);
    std::future<int> future = task.get_future();

    // std::promise一样，std::packaged_task支持move，但不支持拷贝
    // std::thread的第一个参数不止是函数，还可以是一个可调用对象，即支持operator()(Args...)操作
    std::thread t(std::move(task), 1, 2);
    // 等待异步计算结果
    std::cout << "1 + 2 => " << future.get() << std::endl;

    t.join();
    return 0;
}
/// 输出: 1 + 2 => 3
```
std::packaged_task位于头文件```#include <future>```中，是一个模板类
```c++
template <class R, class... ArgTypes>
class packaged_task<R(ArgTypes...)>
```
其中R是一个函数或可调用对象，ArgTypes是R的形参。与std::promise一样，std::packaged_task**支持move，但不支持拷贝(copy)**。std::packaged_task封装的函数的计算结果会通过与之联系的std::future::get获取(当然，可以在其它线程中异步获取)。关联的std::future可以通过std::packaged_task::get_future获取到，get_future仅能调用一次，多次调用会触发std::future_error异常。<br/>
std::package_task除了可以通过可调用对象构造外，还支持缺省构造(无参构造)。但此时构造的对象不能直接使用，需通过右值赋值操作设置了可调用对象或函数后才可使用。判断一个std::packaged_task是否可使用，可通过其成员函数valid来判断。
### 2.1 std::packaged_task::valid
该函数用于判断std::packaged_task对象是否是有效状态。当通过缺省构造初始化时，由于其未设置任何可调用对象或函数，valid会返回false。只有当std::packaged_task设置了有效的函数或可调用对象，valid才返回true
```c++
#include <future>   // std::packaged_task, std::future
#include <iostream> // std::cout

int main() {
    std::packaged_task<void()> task; // 缺省构造、默认构造
    std::cout << std::boolalpha << task.valid() << std::endl; // false

    std::packaged_task<void()> task2(std::move(task)); // 右值构造
    std::cout << std::boolalpha << task.valid() << std::endl; // false

    task = std::packaged_task<void()>([](){});  // 右值赋值, 可调用对象
    std::cout << std::boolalpha << task.valid() << std::endl; // true

    return 0;
}
```
上面的示例演示了几种valid为false的情况，程序输出如下
```console
false
false
true
```

### 2.2 std::packaged_task::operator()(ArgTypes...)
该函数会调用std::packaged_task对象所封装可调用对象R，但其函数原型与R稍有不同:
```c++
void operator()(ArgTypes... );
```
operator()的返回值是void，即无返回值。因为std::packaged_task的设计主要是用来进行异步调用，因此R(ArgTypes...)的计算结果是通过std::future::get来获取的。该函数会忠实地将R的计算结果反馈给std::future，即使R抛出异常(此时std::future::get也会抛出同样的异常)
```c++
#include <future>   // std::packaged_task, std::future
#include <iostream> // std::cout

int main() {
    std::packaged_task<void()> convert([](){
        throw std::logic_error("will catch in future");
    });
    std::future<void> future = convert.get_future();

    convert(); // 异常不会在此处抛出

    try {
        future.get();
    } catch(std::logic_error &e) {
        std::cerr << typeid(e).name() << ": " << e.what() << std::endl;
    }

    return 0;
}
/// Clang下输出: St11logic_error: will catch in future
```
为了帮忙大家更好的了解该函数，下面将Clang下精简过的operator()(Args...)的实现贴出，以便于更好理解该函数的边界，明确什么可以做，什么不可以做。
```c++
template<class _Rp, class ..._ArgTypes>
class packaged_task<_Rp(_ArgTypes...)> {
    __packaged_task_function<_Rp_(_ArgTypes...)> __f_;
    promise<_Rp> __p_;  // 内部采用了promise实现

public:
    // 构造、析构以及其它函数...

    void packaged_task<_Rp(_ArgTypes...)>::operator()(_ArgTypes... __args) {
        if (__p_.__state_ == nullptr)
            __throw_future_error(future_errc::no_state);
        if (__p_.__state_->__has_value())  // __f_不可重复调用
            __throw_future_error(future_errc::promise_already_satisfied);

        try {
            __p_.set_value(__f_(std::forward<_ArgTypes>(__args)...));
        } catch (...) {
            __p_.set_exception(current_exception());
        }
    }
};
```
### 2.3 让std::packaged_task在线程退出时再将结果反馈给std::future
std::packaged_task::make_ready_at_thread_exit函数接收的参数与operator()(_ArgTypes...)一样，行为也一样。只有一点差别，那就是不会将计算结果立刻反馈给std::future，而是在其执行时所在的线程结束后，std::future::get才会取得结果。
### 2.4 std::packaged_task::reset
与std::promise不一样， std::promise仅可以执行一次set_value或set_exception函数，但std::packagged_task可以执行多次，其奥秘就是reset函数
```c++
template<class _Rp, class ..._ArgTypes>
void packaged_task<_Rp(_ArgTypes...)>::reset()
{
    if (!valid())
        __throw_future_error(future_errc::no_state);
    __p_ = promise<result_type>();
}
```
通过重新构造一个promise来达到多次调用的目的。显然调用reset后，需要重新get_future，以便获取下次operator()执行的结果。由于是重新构造了promise，因此reset操作并不会影响之前调用的make_ready_at_thread_exit结果，也即之前的定制的行为在线程退出时仍会发生。

std::packaged_task就介绍到这里，下一篇将会完成本次异步运行的整体脉络，将std::async和std::future一起介绍结大家。

## 附: C++11多线程中的样例代码的编译及运行
```console
g++ -std=c++11 <Your Cpp File>
./a.out
```
