# C++11多线程-mutex(2)
C++11在提供了常规mutex的基础上，还提供了一些易用性的类，本节我们将一起看一下这些类。
## 1. lock_guard
lock_guard利用了C++ RAII的特性，在构造函数中上锁，析构函数中解锁。lock_guard是一个模板类，其原型为
```c++
template <class Mutex> class lock_guard
```
模板参数Mutex代表互斥量，可以是上一篇介绍的std::mutex, std::timed_mutex, std::recursive_mutex, std::recursive_timed_mutex中的任何一个，也可以是std::unique_lock(下面即将介绍)，这些都提供了lock和unlock的能力。<br />
lock_guard仅用于上锁、解锁，不对mutex承担供任何生周期的管理，因此在使用的时候，请确保lock_guard管理的mutex一直有效。
同其它mutex类一样，locak_guard不允许拷贝，即拷贝构造和赋值函数被声明为delete。
```c++
lock_guard(lock_guard const&) = delete;
lock_guard& operator=(lock_guard const&) = delete;
```
lock_guard的设计保证了即使程序在锁定期间发生了异常，也会安全的释放锁，不会发生死锁。
```c++
#include <iostream>
#include <mutex>

std::mutex mutex;

void safe_thread() {
    try {
        std::lock_guard<std::mutex> _guard(mutex);
        throw std::logic_error("logic error");
    } catch (std::exception &ex) {
        std::cerr << "[caught] " << ex.what() << std::endl;
    }
}
int main() {
    safe_thread();
    // 此处仍能上锁
    mutex.lock();
    std::cout << "OK, still locked" << std::endl;
    mutex.unlock();

    return 0;
}
```
程序输出
```console
[caught] logic error
OK, still locked
```
## 2. unique_lock
lock_guard提供了简单上锁、解锁操作，但当我们需要更灵活的操作时便无能为力了。这些就需要unique_lock上场了。unique_lock拥有对Mutex的**所有权**，一但初始化了unique_lock，其就接管了该mutex, 在unique_lock结束生命周期前(析构前)，其它地方就不要再直接使用该mutex了。unique_lock提供的功能较多，此处不一一列举，下面列出unique_lock的类声明，及部分注释，供大家参考
```c++
template <class Mutex>
class unique_lock
{
public:
    typedef Mutex mutex_type;
    // 空unique_lock对象
    unique_lock() noexcept;
    // 管理m, 并调用m.lock进行上锁，如果m已被其它线程锁定，由该构造了函数会阻塞。
    explicit unique_lock(mutex_type& m);
    // 仅管理m，构造函数中不对m上锁。可以在初始化后调用lock, try_lock, try_lock_xxx系列进行上锁。
    unique_lock(mutex_type& m, defer_lock_t) noexcept;
    // 管理m, 并调用m.try_lock，上锁不成功不会阻塞当前线程
    unique_lock(mutex_type& m, try_to_lock_t);
    // 管理m, 该函数假设m已经被当前线程锁定，不再尝试上锁。
    unique_lock(mutex_type& m, adopt_lock_t);
    // 管理m, 并调用m.try_lock_unitil函数进行加锁
    template <class Clock, class Duration>
        unique_lock(mutex_type& m, const chrono::time_point<Clock, Duration>& abs_time);
    // 管理m，并调用m.try_lock_for函数进行加锁
    template <class Rep, class Period>
        unique_lock(mutex_type& m, const chrono::duration<Rep, Period>& rel_time);
    // 析构，如果此前成功加锁(或通过adopt_lock_t进行构造)，并且对mutex拥有所有权，则解锁mutex
    ~unique_lock();

    // 禁止拷贝操作
    unique_lock(unique_lock const&) = delete;
    unique_lock& operator=(unique_lock const&) = delete;

    // 禁止move语义
    unique_lock(unique_lock&& u) noexcept;
    unique_lock& operator=(unique_lock&& u) noexcept;

    void lock();
    bool try_lock();

    template <class Rep, class Period>
        bool try_lock_for(const chrono::duration<Rep, Period>& rel_time);
    template <class Clock, class Duration>
        bool try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);

    // 显示式解锁，该函数调用后，除非再次调用lock系列函数进行上锁，否则析构中不再进行解锁
    void unlock();

    // 与另一个unique_lock交换所有权
    void swap(unique_lock& u) noexcept;
    // 返回当前管理的mutex对象的指针，并释放所有权
    mutex_type* release() noexcept;

    // 当前实例是否获得了锁
    bool owns_lock() const noexcept;
    // 同owns_lock
    explicit operator bool () const noexcept;
    // 返回mutex指针，便于开发人员进行更灵活的操作
    // 注意：此时mutex的所有权仍归unique_lock所有，因此不要对mutex进行加锁、解锁操作
    mutex_type* mutex() const noexcept;
};
```
## 3. std::call_once
该函数的作用顾名思义：保证call_once调用的函数只被执行一次。该函数需要与std::once_flag配合使用。std::once_flag被设计为对外封闭的，即外部没有任何渠道可以改变once_flag的值，仅可以通过std::call_once函数修改。一般情况下我们在自己实现call_once效果时，往往使用一个全局变量，以及双重检查锁(DCL)来实现，即便这样该实现仍然会有很多坑(多核环境下)。有兴趣的读者可以搜索一下DCL来看，此处不再赘述。<br/>
C++11为我们提供了简便的解决方案，所需做的仅仅像下面这样使用即可。
```c++
#include <iostream>
#include <thread>
#include <mutex>

void initialize() {
    std::cout << __FUNCTION__ << std::endl;
}

std::once_flag of;
void my_thread() {
    std::call_once(of, initialize);
}

int main() {
    std::thread threads[10];
    for (std::thread &thr: threads) {
        thr = std::thread(my_thread);
    }
    for (std::thread &thr: threads) {
        thr.join();
    }
    return 0;
}
// 仅输出一次：initialize
```
## 4. std::try_lock
当有多个mutex需要执行try_lock时，该函数提供了简便的操作。try_lock会按参数从左到右的顺序，对mutex**顺次执行**try_lock操作。当其中某个mutex.try_lock失败(返回false或抛出异常)时，已成功锁定的mutex都将被解锁。<br/>
需要注意的是，该函数成功时返回-1， 否则返回失败mutex的索引，索引从0开始计数。
```c++
template <class L1, class L2, class... L3>
  int try_lock(L1&, L2&, L3&...);
```
## 5. std::lock
std::lock是较智能的上批量上锁方式，采用死锁算法来锁定给定的mutex列表，避免死锁。该函数对mutex列表的上锁顺序是不确定的。该函数保证: 如果成功，则所有mutex全部上锁，如果失败，则全部解锁。
```c++
template <class L1, class L2, class... L3>
  void lock(L1&, L2&, L3&...);
```
