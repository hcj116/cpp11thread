/**
 * chan的使用:
 * 1. 阻塞队列:
 *   1.1 chan<T> ch(0): 无缓冲队列，push阻塞直到pop取走数据，或ch关闭。
 *   1.2 chan<T> ch(N): N>0, 具有N个缓冲区的队列，只有当队列满时push才会阻塞。
 * 2. 非阻塞队列
 *   2.1 chan<T> ch(N, discard_old): 具有N个缓冲区的实时队列, 当N==0时队列长度为1。
 *                                   push不阻塞，当队列满时，新push的数据会替换掉最老的数据。
 *   2.2 chan<T> ch(N, discard): 具有N个缓冲区的队列，当N==0时队列长度为1。
 *                               push不阻塞，当队列满时，新push的数据会失败。
 */
#pragma once
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4200)
#endif

#include <condition_variable>  // std::condition_variable
#include <memory>              // unique_ptr
#include <mutex>               // std::mutex
#include <vector>
#ifndef CHAN_MAX_COUNTER
#    include <limits>  // std::numeric_limits
// VC下max问题: https://www.cnblogs.com/cvbnm/articles/1947743.html
#    define CHAN_MAX_COUNTER (std::numeric_limits<size_t>::max)()
#endif

// 队列满时的push策略
enum class push_policy : unsigned char {
    blocking,     // 阻塞, 直到队列腾出空间
    discard_old,  // 丢弃队列中最老数据，非阻塞
    discard,      // 丢弃当前入chan的值，并返回false。非阻塞
};

namespace ns_chan {
    // 避免惊群
    class cv_t {
        std::condition_variable cv_;
        uint32_t thread_count_ = 0;
        uint32_t wait_count_ = 0;

    public:
        template <class Predicate>
        void wait(std::unique_lock<std::mutex> &lock, Predicate pred) {
            if (!pred()) {
                ++thread_count_;
                do {
                    ++wait_count_;
                    cv_.wait(lock);
                } while (!pred());
                --thread_count_;
            }
        }

        void notify_one() {
            if (wait_count_ > 0) {
                wait_count_ = (wait_count_ > thread_count_ ? thread_count_ : wait_count_) - 1;
                cv_.notify_one();
            }
        }
        void notify_all() {
            wait_count_ = 0;
            cv_.notify_all();
        }
    };

    template<typename T>
    class queue_t {
        mutable std::mutex mutex_;
        cv_t cv_push_;
        cv_t cv_pop_;
        std::condition_variable *const cv_overflow_;
        const size_t capacity_;     // _data容量
        const push_policy policy_;  // 队列满时的push策略
        bool closed_ = false;  // 队列是否已关闭
        size_t first_ = 0;     // 队列中的第一条数据
        size_t new_ = 0;       // 新数据的插入位置，first_==new_队列为空
        T data_[0];            // T data_[capacity_]
    private:
        queue_t(size_t capacity, push_policy policy)
            : capacity_(capacity == 0 ? 1 : capacity),
              policy_(policy),
              cv_overflow_(capacity == 0 ? new std::condition_variable() : nullptr) {
        }
    public:
        queue_t(const queue_t &) = delete;
        queue_t(queue_t &&) = delete;
        queue_t &operator=(const queue_t &) = delete;
        queue_t &operator=(queue_t &&) = delete;

        ~queue_t() {
            for (; first_ < new_; first_++) {
                data(first_).~T();
            }
            delete cv_overflow_;
        }

        // close以后的入chan操作会返回false, 而出chan则在队列为空后，才返回false
        void close() {
            std::unique_lock<std::mutex> lock(mutex_);
            closed_ = true;
            if (cv_overflow_ != nullptr && !is_empty()) {
                // 消除溢出
                data(--new_).~T();
                cv_overflow_->notify_all();
            }
            cv_push_.notify_all();
            cv_pop_.notify_all();
        }

        bool is_closed() const {
            std::unique_lock<std::mutex> lock(mutex_);
            return closed_;
        }

        // 入chan，支持move语义
        template <typename TR>
        bool push(TR &&data) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_push_.wait(lock, [&]() { return policy_ != push_policy::blocking || free_count() > 0 || closed_; });
            if (closed_) {
                return false;
            }

            if (!push_thread_unsafe(std::forward<TR>(data))) {
                return false;
            }

            cv_pop_.notify_one();
            if (cv_overflow_ != nullptr) {
                const size_t old = first_;
                cv_overflow_->wait(lock, [&]() { return old != first_ || closed_; });
            }

            return !closed_;
        }

        bool pop(std::function<void(T &&data)> consume) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_pop_.wait(lock, [&]() { return !is_empty() || closed_; });
            if (is_empty()) {
                return false;  // 已关闭
            }

            T &target = data(first_++);
            consume(std::move(target));
            target.~T();

            if (cv_overflow_ != nullptr) {
                cv_overflow_->notify_one();
            }
            cv_push_.notify_one();

            return true;
        }

        static std::shared_ptr<queue_t> make_queue(size_t capacity, push_policy policy) {
            if (policy != push_policy::blocking && capacity == 0) {
                capacity = 1;
            }
            const size_t size = sizeof(queue_t) + sizeof(T) * (capacity == 0 ? 1 : capacity);
            // 只有阻塞模式下才存在“溢出”等待区
            return std::shared_ptr<queue_t>(new (new char[size]) queue_t(capacity, policy), [](queue_t *q) {
                q->~queue_t();
                delete[](char *) q;
            });
        }

    private:
        template <typename TR>
        bool push_thread_unsafe(TR &&d) {
            if (free_count() > 0) {
                new (&data(new_++)) T(std::forward<TR>(d));
            } else if (policy_ == push_policy::discard_old) {
                first_++;  // 替换掉最老的
                data(new_++) = std::forward<TR>(d);
            } else {
                assert(policy_ == push_policy::discard);
                return false;  // 取消此次操作, 需结合is_closed来判断是否已关闭
            }
            // 防_first和_new溢出
            if (new_ >= CHAN_MAX_COUNTER) {
                reset_pos();
            }

            return true;
        }
        size_t free_count() const {
            return first_ + capacity_ - new_;
        }
        bool is_empty() const {
            return first_ >= new_;
        }
        T &data(size_t pos) {
            return data_[pos % capacity_];
        }

        void reset_pos() {
            const size_t new_first = (this->first_ % this->capacity_);
            this->new_ -= (this->first_ - new_first);
            this->first_ = new_first;
        }

    };
}

template <typename T>
class chan {
    struct data_t{
        std::vector<std::shared_ptr<ns_chan::queue_t<T> > > queue_;
        std::atomic<unsigned int> push_{0}, pop_{0};
    };

    std::shared_ptr<data_t> data_;

public:
    //sizeof(queue__t) = 216, sizeof(cv) = 48/56, sizeof(mutex) = 64
    // 选取适当的concurrent_shift和capacity，chan的吞吐理可达千万/秒
    explicit chan(size_t concurrent_shift, size_t capacity, push_policy policy = push_policy::blocking) {
        data_ = std::make_shared<data_t>();
        data_->queue_.resize(1 << concurrent_shift);
        for (auto &r: data_->queue_) {
            r = ns_chan::queue_t<T>::make_queue(capacity, policy);
        }
    }

    explicit chan(size_t capacity = 0, push_policy policy = push_policy::blocking)
        : chan(0, capacity, policy) {
    }

    // 支持拷贝
    chan(const chan &) = default;
    chan &operator=(const chan &) = default;
    // 支持move
    chan(chan &&) = default;
    chan &operator=(chan &&) = default;

    // 入chan，支持move语义
    template <typename TR>
    bool operator<<(TR &&data) {
        unsigned int index = data_->push_.fetch_add(1, std::memory_order_acq_rel);
        return data_->queue_[index % length()]->push(std::forward<TR>(data));
    }
    template <typename TR>
    bool push(TR &&data) {
        unsigned int index = data_->push_.fetch_add(1, std::memory_order_acq_rel);
        return data_->queue_[index % length()]->push(std::forward<TR>(data));
    }

    void close() {
        for (size_t i = 0; i < length(); i++) {
            data_->queue_[i]->close();
        }
    }

    bool is_closed() const {
        return data_->queue_[0]->is_closed();
    }

    // 出chan
    template <typename TR>
    bool operator>>(TR &d) {
        unsigned int index = data_->pop_.fetch_add(1, std::memory_order_acq_rel);
        return data_->queue_[index % length()]->pop([&d](T &&target) { d = std::forward<T>(target); });
    }

    // 性能较operator>>稍差，但外部用起来更方便
    // 当返回false时表明chan已关闭: while(d = ch.pop()){}
    std::unique_ptr<T> pop() {
        unsigned int index = data_->pop_.fetch_add(1, std::memory_order_acq_rel);
        std::unique_ptr<T> d;
        data_->queue_[index % length()]->pop([&d](T &&target) { d.reset(new T(std::forward<T>(target))); });
        return d;
    }
private:
    size_t length() const {
        return data_->queue_.size();
    }
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
