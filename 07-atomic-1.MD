# C++11多线程-原子操作(1)
前面我们讲了C++11下的多线程及相关操作，这些操作在绝大多数情况下应该够用了。但在某些极端场合，如需要高性能的情况下，我们还需要一些更高效的同步手段。本节介绍的原子操作是一种lock free的操作，不需要同步锁，具有很高的性能。在化学中原子不是可分割的最小单位，引申到编程中，原子操作是不可打断的最低粒度操作，是线程安全的。**C++11中原子类提供的成员函数都是原子的，是线程安全的。**
原子操作中最简单的莫过于atomic_flag，只有两种操作：test and set、clear。我们的原子操作就从这种类型开始。
## 1. std::atomic_flag
C++11中所有的原子类都是**不允许拷贝、不允许Move**的，atomic_flag也不例外。atomic_flag顾名思议，提供了标志的管理，标志有三种状态：clear、set和未初始化状态。
### 1.1 atomic_flag实例化
缺省情况下atomic_flag处于未初始化状态。除非初始化时使用了`ATOMIC_FLAG_INIT`宏，则此时atomic_flag处于clear状态。
### 1.2 std::atomic_flag::clear
调用该函数将会把atomic_flag置为clear状态。clear状态您可以理解为bool类型的false，set状态可理解为true状态。clear函数没有任何返回值:
```c++
void clear(memory_order m = memory_order_seq_cst) volatile noexcept;
void clear(memory_order m = memory_order_seq_cst) noexcept;
```
对于memory_order我们会在后面的章节中详细介绍它，现在先列出其取值及简单释义

|序号|值|意义
|:--:|:--:|:---
|1|memory_order_relaxed|宽松模型，不对执行顺序做保证
|2|memory_order_consume|当前线程中,满足happens-before原则。<br/>当前线程中该原子的所有后续操作,必须在本条操作完成之后执行
|3|memory_order_acquire|当前线程中,**读**操作满足happens-before原则。<br/>所有后续的**读**操作必须在本操作完成后执行
|4|memory_order_release|当前线程中,**写**操作满足happens-before原则。<br/>所有后续的**写**操作必须在本操作完成后执行
|5|memory_order_acq_rel|当前线程中，同时满足memory_order_acquire和memory_order_release
|6|memory_order_seq_cst|最强约束。全部读写都按顺序执行

### 1.3 test_and_set
该函数会检测flag是否处于set状态，如果不是，则将其设置为set状态，并返回false；否则返回true。![](https://upload-images.jianshu.io/upload_images/6687014-40e5b28ef720dad9.png?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)

[test_and_set](https://en.wikipedia.org/wiki/Test-and-set)是典型的*read-modify-write(RMW)*模型，保证多线程环境下只被设置一次。下面代码通过10个线程，模拟了一个计数程序，第一个完成计数的会打印"win"。
```c++
#include <atomic>    // atomic_flag
#include <iostream>  // std::cout, std::endl
#include <list>      // std::list
#include <thread>    // std::thread

void race(std::atomic_flag &af, int id, int n) {
    for (int i = 0; i < n; i++) {
    }
    // 第一个完成计数的打印：Win
    if (!af.test_and_set()) {
        printf("%s[%d] win!!!\n", __FUNCTION__, id);
    }
}

int main() {
    std::atomic_flag af = ATOMIC_FLAG_INIT;

    std::list<std::thread> lstThread;
    for (int i = 0; i < 10; i++) {
        lstThread.emplace_back(race, std::ref(af), i + 1, 5000 * 10000);
    }

    for (std::thread &thr : lstThread) {
        thr.join();
    }

    return 0;
}
```
程序输出如下(每次运行，可能率先完成的thread不同):
```console
race[7] win!!!
```

[](https://www.cnblogs.com/zifeiye/p/8194949.html)
