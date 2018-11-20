# C++11多线程-mutex(1)
mutex又称互斥量，用于提供对共享变量的互斥访问。C++11中mutex相关的类都在\<mutex\>头文件中。共四种互斥类:
<table>
  <tr><td>序号</td><td>名称</td><td>用途</td></tr>
  <tr><td>1</td><td>std::mutex</td><td>最基本也是最常用的互斥类</td></tr>
  <tr><td>2</td><td>std::recursive_mutex</td><td>同一线程内可递归(重入)的互斥类</td></tr>
  <tr><td>3</td><td>std::timed_mutex</td><td>除具备mutex功能外，还提供了带时限请求锁定的能力</td></tr>
  <tr><td>4</td><td>std::recursive_timed_mutex</td><td>同一线程内可递归(重入)的timed_mutex</td></tr>
</table>
与std::thread一样，mutex相关类不支持拷贝构造、不支持赋值。同时mutex类也不支持move语义(move构造、move赋值)。不用担心会误用这些操作，真要这么做了的话，编译器会阻止你的。

## 一、lock, try_lock, unlock
mutex的标准操作，四个mutex类都支持这些操作，但是不同类在行为上有些微的差异。
### lock
锁住互斥量。调用lock时有三种情况:
  1. 如果互斥量没有被锁住，则调用线程将该mutex锁住，直到调用线程调用unlock释放。
  2. 如果mutex已被其它线程lock，则调用线程将被阻塞，直到其它线程unlock该mutex。
  3. 如果当前mutex已经被调用者线程锁住，则std::mutex死锁，而recursive系列则成功返回。
### try_lock
尝试锁住mutex，调用该函数同样也有三种情况：
  1. 如果互斥量没有被锁住，则调用线程将该mutex锁住(返回true)，直到调用线程调用unlock释放。
  2. 如果mutex已被其它线程lock，则调用线程将失败，并返回false。
  3. 如果当前mutex已经被调用者线程锁住，则std::mutex死锁，而recursive系列则成功返回true。
### unlock
解锁mutex，释放对mutex的所有权。值得一提的时，对于recursive系列mutex，unlock次数需要与lock次数相同才可以完全解锁。<br/>
下面给出一个mutex小例子
```c++
#include <iostream>
#include <thread>
#include <mutex>

void inc(std::mutex &mutex, int loop, int &counter) {
    for (int i = 0; i < loop; i++) {
        mutex.lock();
        ++counter;
        mutex.unlock();
    }
}
int main() {
    std::thread threads[5];
    std::mutex mutex;
    int counter = 0;

    for (std::thread &thr: threads) {
        thr = std::thread(inc, std::ref(mutex), 1000, std::ref(counter));
    }
    for (std::thread &thr: threads) {
        thr.join();
    }

    // 输出：5000，如果inc中调用的是try_lock，则此处可能会<5000
    std::cout << counter << std::endl;

    return 0;
}
//: g++ -std=c++11 main.cpp
```

## 二、try_lock_for, try_lock_until
这两个函数仅用于timed系列的mutex(std::timed_mutex, std::recursive_timed_mutex)，函数最多会等待指定的时间，如果仍未获得锁，则返回false。除超时设定外，这两个函数与try_lock行为一致。
```c++
// 等待指定时长
template <class Rep, class Period>
    try_lock_for(const chrono::duration<Rep, Period>& rel_time);
// 等待到指定时间
template <class Clock, class Duration>
    try_lock_until(const chrono::time_point<Clock, Duration>& abs_time);
```
try_lock_for相关代码
```c++
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>

void run500ms(std::timed_mutex &mutex) {
    auto _500ms = std::chrono::milliseconds(500);
    if (mutex.try_lock_for(_500ms)) {
        std::cout << "获得了锁" << std::endl;
    } else {
        std::cout << "未获得锁" << std::endl;
    }
}
int main() {
    std::timed_mutex mutex;

    mutex.lock();
    std::thread thread(run500ms, std::ref(mutex));
    thread.join();
    mutex.unlock();

    return 0;
}
//输出：未获得锁
```
### 其它
mutex文件中还提供了lock_guard, unique_lock，std::call_once, std::try_lock, std::lock(批量上锁)操作，由于篇幅关系，我们下次再讲。
