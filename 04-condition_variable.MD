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
需要注意的一点是, wait有时会在没有任何线程调用notify的情况下返回，这种情况就是有名的[**spurious wakeup**](https://docs.microsoft.com/zh-cn/windows/desktop/api/synchapi/nf-synchapi-sleepconditionvariablecs)。因此当wait返回时，你需要再次检查wait的前置条件是否满足，如果不满足则需要再次wait。wait提供了重载的版本，用于提供前置检查。
```c++
template <typename Predicate>
void wait(unique_lock<mutex> &lock, Predicate pred) {
    while(!pred()) {
        wait(lock);
    }
}
```
除wait外, 条件变量还提供了wait_for和wait_until，这两个名称是不是看着有点儿眼熟，std::mutex也提供了_for和_until操作。在C++11多线程编程中，需要等待一段时间的操作，一般情况下都会有xxx_for和xxx_until版本。前者用于等待指定时长，后者用于等待到指定的时间。
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
#include "chan.h"  // chan的头文件

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
## 源码:
[chan.h](./chan.h) <br/>
chan.h的简化版，方便理解std::mutex, std::condition_variable: [chan.simple.h](./chan.simple.h)
