# C++11多线程-异步运行之最终篇(future+async)
前面两章多次使用到std::future，本章我们就来揭开std::future庐山真面目。最后我们会引出std::async，该函数使得我们的并发调用变得简单，优雅。
## 3. std::future
前面我们多次使用std::future的get方法来获取其它线程的结果，那么除这个方法外，std::future还有哪些方法呢
```c++
enum class future_status
{
    ready,
    timeout,
    deferred
};
template <class R>
class future
{
public:
    // retrieving the value
    R get();
    // functions to check state
    bool valid() const noexcept;

    void wait() const;
    template <class Rep, class Period>
    future_status wait_for(const chrono::duration<Rep, Period>& rel_time) const;
    template <class Clock, class Duration>
    future_status wait_until(const chrono::time_point<Clock, Duration>& abs_time) const;

    shared_future<R> share() noexcept;
};
```
以上代码去掉了std::future构造、析构、赋值相关的代码，这些约束我们之前都讲过了。下面我们来逐一了解上面这些函数。
### 3.1 get
这个函数我们之前一直使用，该函数会一直阻塞，直到获取到结果或异步任务抛出异常。
### 3.2 share
std::future允许move，但是不允许拷贝。如果用户确有这种需求，需要同时持有多个实例，怎么办呢? 这就是share发挥作用的时候了。std::shared_future通过引用计数的方式实现了多实例[共享同一状态](#assoc_state)，但有计数就伴随着同步开销(无锁的原子操作也是有开销的)，性能会稍有下降。因此C++11要求程序员显式调用该函数，以表明用户对由此带来的开销负责。std::shared_future允许move，允许拷贝，并且具有和std::future同样的成员函数，此处就不一一介绍了。当调用share后，std::future对象就不再和任何[共享状态](#assoc_state)关联，其[valid](#valid)函数也会变为false。
### 3.3 wait
等待，直到数据就绪。数据就绪时，通过get函数，无等待即可获得数据。
### 3.4 wait_for和wait_until
wait_for、wait_until主要是用来进行超时等待的。wait_for等待指定时长，wait_until则等待到指定的时间点。返回值有3种状态：
  1. ready - 数据已就绪，可以通过get获取了
  2. timeout - 超时，数据还未准备好
  3. deferred - 这个和std::async相关，表明无需wait，异步函数将在get时执行

### 3.5 <a id="valid">valid</a>
判断当前std::future实例是否有效。std::future主要是用来获取异步任务结果的，作为消费方出现，单独构建出来的实例没意义，因此其valid为false。当与其它生产方(Provider)关联后，valid就会变得有效，std::future才会发挥实际的作用。C++11中有下面几种Provider，可这些Provider获得有效的std::future实例：
 1. [std::async](#async)
 2. std::promise::get_future
 3. std::packaged_task::get_future
Provider与std::future通过共享状态进行关联，从而实现与std::future的通信。既然std::future的各种行为都依赖共享状态，那么什么是共享状态呢?
## 4. <a id="assoc_state">共享状态</a>
共享状态其本质就是单生产者-单消费者的多线程并发模型。无论是std::promise还是std::packaged_task都是通过共享状态，实现与std::future通信的。还记得我们在std::condition_variable一节给出的chan类么。共享状态与其类似，通过std::mutex、std::condition_variable实现了多线程间通信。共享状态并非C++11的标准，只是对std::promise、std::future的实现手段。回想我们之前的使用场景，共享状态可能具有如下形式(c++11伪代码):
```c++
template<typename T>
class assoc_state {
protected:
    mutable mutex mut_;
    mutable condition_variable cv_;
    unsigned state_ = 0;
    // std::shared_future中拷贝动作会发生引用计数的变化
    // 当引用计数降到0时，实例被delete
    int share_count_ = 0;
    exception_ptr exception_; // 执行异常
    T value_;  // 执行结果

public:
    enum {
        ready = 4,  // 异步动作执行完，数据就绪
        // 异步动作将延迟到future.get()时调用
        // (实际上非异步，只不过是延迟执行)
        deferred = 8,
    };

    assoc_state() {}
    // 禁止拷贝
    assoc_state(const assoc_state &) = delete;
    assoc_state &operator=(const assoc_state &) = delete;
    // 禁止move
    assoc_state(assoc_state &&) = delete;
    assoc_state &operator=(assoc_state &&) = delete;

    void set_value(const T &);
    void set_exception(exception_ptr p);
    // 需要用到线程局变存储
    void set_value_at_thread_exit(const T &);
    void set_exception_at_thread_exit(exception_ptr p);

    void wait();
    future_status wait_for(const duration &) const;
    future_status wait_until(const time_point &) const;

    T &get() {
        unique_lock<mutex> lock(this->mut_);
        // 当_state为deferred时，std::async中
        // 的函数将在sub_wait中调用
        this->sub_wait(lock);
        if (this->_exception != nullptr)
            rethrow_exception(this->_exception);
        return _value;
    }
private:
    void sub_wait(unique_lock<mutex> &lk) {
        if (state_ != ready) {
            if (state_ & static_cast<unsigned>(deferred)) {
                state_ &= ~static_cast<unsigned>(deferred);
                lk.unlock();
                __execute();  // 此处执行实际的函数调用
            } else {
                cv_.wait(lk, [this](){return state == ready;})
            }
        }
    }
};
```
以上给出了get的实现(伪代码)，其它部分虽然没实现，但assoc_state应该具有的功能，以及对std::promise、std::packaged_task、std::future、std::shared_future的支撑应该能够表达清楚了。未实现部分还请读者自行补充一下，权当是练手了。<br/>
有兴趣的读者可以阅读[llvm-libxx](https://github.com/llvm-mirror/libcxx)(https://github.com/llvm-mirror/libcxx) 的源码，以了解更多细节，对共享状态有更深掌握。
## 5. <a id="async">std::async</a>
std::async可以看作是对std::packaged_task的封装(虽然实际并一定如此，取决于编译器的实现，但共享状态的思想是不变的)，有两种重载形式:
```c++
#define FR typename result_of<typename decay<F>::type(typename decay<Args>::type...)>::type

// 不含执行策略
template <class F, class... Args>
future<FR> async(F&& f, Args&&... args);
// 含执行策略
template <class F, class... Args>
future<FR> async(launch policy, F&& f, Args&&... args);
```
define部分是用来推断函数F的返回值类型，我们先忽略它，以后有机再讲。两个重载形式的差别是一个含执行策略，而另一个不含。那么什么是执行策略呢？执行策略定义了async执行F(函数或可调用求对象)的方式，是一个枚举值：
```c++
enum class launch {
    // 保证异步行为，F将在单独的线程中执行
    async = 1,
    // 当其它线程调用std::future::get时，
    // 将调用非异步形式, 即F在get函数内执行
    deferred = 2,
    // F的执行时机由std::async来决定
    any = async | deferred
};
```
不含加载策略的版本，使用的是std::launch::any，也即由std::async函数自行决定F的执行策略。那么C++11如何确定std::any下的具体执行策略呢，一种可能的办法是：优先使用async策略，如果创建线程失败，则使用deferred策略。实际上这也是Clang的any实现方式。std::async的出现大大减轻了异步的工作量。使得一个异步调用可以像执行普通函数一样简单。
```c++
#include <iostream> // std::cout, std::endl
#include <future>   // std::async, std::future
#include <chrono>   // seconds
using namespace std::chrono;

int main() {
    auto print = [](char c) {
        for (int i = 0; i < 10; i++) {
            std::cout << c;
            std::cout.flush();
            std::this_thread::sleep_for(milliseconds(1));
        }
    };
    // 不同launch策略的效果
    std::launch policies[] = {std::launch::async, std::launch::deferred};
    const char *names[] = {"async   ", "deferred"};
    for (int i = 0; i < sizeof(policies)/sizeof(policies[0]); i++) {
        std::cout << names[i] << ": ";
        std::cout.flush();
        auto f1 = std::async(policies[i], print, '+');
        auto f2 = std::async(policies[i], print, '-');
        f1.get();
        f2.get();
        std::cout << std::endl;
    }

    return 0;
}
```
以上代码输出如下：
```commandline
async   : +-+-+-+--+-++-+--+-+
deferred: ++++++++++----------
```
进行到现在，C++11的async算是结束了，尽管还留了一些疑问，比如共享状态如何实现set_value_at_thread_exit效果。我们将会在下一章节介绍C++11的线程局部存储，顺便也解答下该疑问。
