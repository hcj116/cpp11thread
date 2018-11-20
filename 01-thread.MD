# C++11多线程(简约但不简单)
## 一、简单使用
C++11提供了一套精练的线程库，小巧且易用。运行一个线程，可以直接创建一个std::thread的实例，线程在实例成功构造成时启动。若有底层平台支持，成员函数std::thread::native_handle()将可提供对原生线程对象运行平台特定的操作。
```c++
#include <thread>
#include <iostream>

void foo() {
    std::cout << "Hello C++11" << std::endl;
}

int main() {
    std::thread thread(foo);  // 启动线程foo
    thread.join();  // 等待线程执行完成

    return 0;
}
```
编译并运行，程序输出：
> Hello C++11

### 1、线程参数
当需要向线程传递参数时，可以直接通过std::thread的构造函数参数进行，构造函数通过完美转发将参数传递给线程函数。
```c++
#include <thread>
#include <iostream>

void hello(const char *name) {
    std::cout << "Hello " << name << std::endl;
}

int main() {
    std::thread thread(hello, "C++11");
    thread.join();

    return 0;
}
```

### 2. 类成员函数做为线程入口
类成员函数做为线程入口时，仍然十分简单: **把this做为第一个参数传递进去即可。**
```c++
#include <thread>
#include <iostream>

class Greet
{
    const char *owner = "Greet";
public:
    void SayHello(const char *name) {
        std::cout << "Hello " << name << " from " << this->owner << std::endl;
    }
};
int main() {
    Greet greet;

    std::thread thread(&Greet::SayHello, &greet, "C++11");
    thread.join();

    return 0;
}
//输出：Hello C++11 from Greet
```

### 3. join: 等待线程执行完成
线程如果像二哈似的撒手没，则程序铁定悲剧。因此std::thread提供了几个线程管理的工具，其中join就是很重要的一个：等待线程执行完成。即使当线程函数已经执行完成后，调用join仍然是有效的。
### 4. 线程暂停
从外部让线程暂停，会引发很多并发问题。大家可以百度一下，此处不做引申。这大概也是std::thread并没有直接提供pause函数的原因。但有时线程在运行时，确实需要“停顿”一段时间怎么办呢？可以使用std::this_thread::sleep_for或std::this_thread::sleep_until
```c++
#include <thread>
#include <iostream>
#include <chrono>

using namespace std::chrono;

void pausable() {
    // sleep 500毫秒
    std::this_thread::sleep_for(milliseconds(500));
    // sleep 到指定时间点
    std::this_thread::sleep_until(system_clock::now() + milliseconds(500));
}

int main() {
    std::thread thread(pausable);
    thread.join();

    return 0;
}
```
### 5. 线程停止
一般情况下当线程函数执行完成后，线程“自然”停止。但在std::thread中有一种情况会造成线程**异常终止**，那就是：**析构**。当std::thread实例析构时，如果线程还在运行，则线程会被强行终止掉，这可能会造成资源的泄漏，因此尽量在析构前join一下，以确保线程成功结束。
如果确实想提前让线程结束怎么办呢？一个简单的方法是使用“共享变量”，线程定期地去检测该量，如果需要退出，则停止执行，退出线程函数。使用“共享变量”需要注意，在多核、多CPU的情况下需要使用“原子”操作，关于原子操作后面会有专题讲述。

## 二、进阶(更多你可能需要知道的)
### 1. 拷贝
```c++
std::thread a(foo);
std::thread b;
b = a;
```
当执行以上代码时，会发生什么？最终foo线程是由a管理，还是b来管理？答案是由b来管理。std::thread被设计为只能由一个实例来维护线程状态，以及对线程进行操作。因此当发生赋值操作时，会发生线程所有权转移。在macos下std::thread的赋值函数原型为:
```c++
thread& operator=(thread&& a);
```
赋完值后，原来由a管理的线程改为由b管理，b不再指向任何线程(相当于执行了detach操作)。如果b原本指向了一个线程，那么这个线程会被终止掉。
### 2. detach/joinable
detach是std::thread的成员函数，函数原型为：
```c++
void detach();
bool joinable() const;
```
detach以后就失去了对线程的所有权，不能再调用join了，因为线程已经分离出去了，不再归该实例管了。判断线程是否还有对线程的所有权的一个简单方式是调用joinable函数，返回true则有，否则为无。
### 3. 线程内部调用自身的join
自己等待自己执行结束？如果程序员真这么干，那这个程序员一定是脑子短路了。对于这种行为C++11只能抛异常了。

## 三、其它
### 1. get_id
每个线程都有一个id，但此处的get_id与系统分配给线程的ID并不一是同一个东东。如果想取得系统分配的线程ID，可以调用native_handle函数。
### 2. 逻辑运算?
有些平台下std::thread还支持若干逻辑运算，比如Visual C++, 但这并不是标准库的行为，不要在跨平台的场景中使用。

