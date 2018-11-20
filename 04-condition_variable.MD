# C++11多线程-条件变量(std::condition_variable)
前面我们介绍了线程(std::thread)和互斥量(std::mutex)，互斥量是多线程间同时访问某一共享变量时，保证变量可被安全访问的手段。在多线程编程中，还有另一种十分常见的行为：线程同步。线程同步是指线程间需要按照预定的先后次序顺序进行的行为。C++11对这种行为也提供了有力的支持，这就是条件变量。条件变量位于头文件condition_variable下。本章我们将简要介绍一下该类，在文章的最后我们会综合运用std::mutex和std::condition_variable，实现一个chan类，该类可在多线程间安全的通信，具有广泛的应用场景。
## 1. std::condition_variable
条件变量提供了两类操作：wait和notify。这两类操作构成了多线程同步的基础。
### 1.1 wait
wait是线程的等待动作，直到其它线程将其唤醒后，才会继续往下执行。下面通过伪代码来说明其用法：
```c++
std::mutex mutex;
std::condition_variable cv;

// 条件变量与临界区有关，用来获取和释放一个锁，因此通常会和mutex联用。
std::unique_lock lock(mutex);
// 此处会释放lock，然后在cv上等待，直到其它线程通过cv.notify_xxx来唤醒当前线程，cv被唤醒后会再次对lock进行上锁，然后wait函数才会返回。
// wait返回后可以安全的使用mutex保护的临界区内的数据。此时mutex仍为上锁状态
cv.wait(lock)
```
需要注意的一点是, wait有时会在没有任何线程调用notify的情况下返回，这种情况就是有名的[**spurious wakeup**](https://docs.microsoft.com/zh-cn/windows/desktop/api/synchapi/nf-synchapi-sleepconditionvariablecs)。因此当wait返回时，你需要再次检查wait的前置条件是否满足，如果不满足则需要再次wait。wait提供了重载的版本，用于提供前置检查。
```c++
template <typename Predicate>
void wait(unique_lock<mutex> &lock, Predicate pred) {
    while(!pred()) {
        wait(lock);
    }
}
```
除wait外, 条件变量还提供了wait_for和wait_until，这两个名称是不是看着有点儿眼熟，std::mutex也提供了_for和_until操作。在C++11多线程编程中，需要等待一段时间的操作，一般情况下都会有xxx_for和xxx_until版本。前者用于等待指定时长，后者用于等待到指定的时间。
### 1.2 notify
了解了wait，notify就简单多了：唤醒wait在该条件变量上的线程。notify有两个版本：notify_one和notify_all。
 * notify_one 唤醒等待的一个线程，注意只唤醒一个。
 * notify_all 唤醒所有等待的线程。使用该函数时应避免出现[惊群效应](https://blog.csdn.net/lyztyycode/article/details/78648798?locationNum=6&fps=1)。

其使用方式见下例：
```c++
std::mutex mutex;
std::condition_variable cv;

std::unique_lock lock(mutex);
// 所有等待在cv变量上的线程都会被唤醒。但直到lock释放了mutex，被唤醒的线程才会从wait返回。
cv.notify_all(lock)
```
## 2. 线程间通信 - chan的实现
有了上面的基础我们就可以设计我们的线程间通讯工具"chan"了。我们的设计目标：
 * 在线程间安全的传递数据。golang社区有一句经典的话：不要通过共享内存来通信，要通过通信来共享内存。
 * 消除线程线程同步带来的复杂性。

我们先来看一下chan的实际使用效果, 生产者-消费者（一个生产者，多个消费者）
```c++
#include <stdio.h>
#include <thread>
#include "chan.h"  // chan的头文件

using namespace std::chrono;

// 消费数据 
void consume(chan<int> ch, int thread_id) {
    int n;
    while(ch >> n) {
        printf("[%d] %d\n", thread_id, n);
        std::this_thread::sleep_for(milliseconds(100));
    }
}

int main() {
    chan<int> chInt(3);
    
    // 消费者
    std::thread consumers[5];
    for (int i = 0; i < 5; i++) {
        consumers[i] = std::thread(consume, chInt, i+1);
    }

    // 生产数据 
    for (int i = 0; i < 16; i++) {
        chInt << i;
    }
    chInt.close();  // 数据生产完毕

    for (std::thread &thr: consumers) {
        thr.join();
    }

    return 0;
}
```
## 附: 源码
下面附上chan的实现，该代码在g++和vc 2015下均编译通过，其它平台未验证。chan很强大，我们在后面的文章中会多次使用到该类，有兴趣的读者可以看一下，欢迎大家一起交流。
```c++
// chan.h
#pragma once
#include <memory>  // shared_ptr
#include <mutex>
#include <condition_variable>

template <typename T>
class chan {
    struct _queue_t {
        T *_data; // 循环使用
        bool _closed = false;   // 队列是否已关闭
        const size_t _capacity; // _data容量
        size_t _old = 0;        // 最旧的数据，优先取走之
        size_t _new = 0;        // 新数据的插入位置，当_old==_new表明队列为空
        std::mutex _mutex;
        std::condition_variable _cv;

        _queue_t(size_t capacity) : _capacity(capacity) {
            _data = new T[capacity];
        }

        ~_queue_t() {
            delete[] _data;
            _data = nullptr;
        }

        bool is_full() const {
            return _old + _capacity <= _new;
        }

        bool is_empty() const {
            return _old >= _new;
        }
    };
    std::shared_ptr<_queue_t> _queue;

  public:
    // 当capacity为0时，入chan操作会阻塞，直到有出chan操作为止
    chan(size_t capacity = 0) {
        _queue = std::make_shared<_queue_t>(capacity + 1);
    }

    // 支持拷贝
    chan(const chan &) = default;
    chan(chan &&) = default;
    // 支持move
    chan &operator=(const chan &) = default;
    chan &operator=(chan &&) = default;

    // 入chan，调用时如果chan未close，则在执行过程中即使close了，入chan也会成功
    bool operator<<(const T &data) {
        std::unique_lock<std::mutex> lock(_queue->_mutex);
        if (_queue->_closed) {
            return false;
        }
        _queue->_cv.wait(lock, [&]() { return !_queue->is_full(); });

        const bool empty = _queue->is_empty();
        _queue->_data[_queue->_new++ % _queue->_capacity] = data;
        if (empty) {
            _queue->_cv.notify_all();
        }
        /**
         由于_queue的容量比用户定制的尺寸大1, 当queue满时，实际上已“溢出“,需等待queue回复正常, 入chan操作也会成功
         */
        _queue->_cv.wait(lock, [&]() { return !_queue->is_full(); });

        return true;
    }

    // 出chan
    bool operator>>(T &data) {
        std::unique_lock<std::mutex> lock(_queue->_mutex);
        _queue->_cv.wait(lock, [&]() {
            return !_queue->is_empty() || _queue->_closed;
        });

        if (_queue->is_empty()) {
            // 已关闭
            return false;
        }

        const bool full = _queue->is_full();
        data = _queue->_data[_queue->_old++ % _queue->_capacity];
        if (_queue->_old >= _queue->_capacity) {
            _queue->_old -= _queue->_capacity;
            _queue->_new -= _queue->_capacity;
        }

        if (full) {
            _queue->_cv.notify_all();
        }

        return true;
    }

    // close以后的入chan操作会抛出closed_error异常
    void close() {
        std::unique_lock<std::mutex> lock(_queue->_mutex);
        _queue->_closed = true;
    }
};
```
