// chan.simple.h
#pragma once
#include <condition_variable>  // std::condition_variable
#include <list>                // std::list
#include <mutex>               // std::mutex

template <typename T>
class chan {
    class queue_t {
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::list<T> data_;
        const size_t capacity_;  // data_容量
        const bool enable_overflow_;
        bool closed_ = false;   // 队列是否已关闭
        size_t pop_count_ = 0;  // 计数，累计pop的数量
    public:
        queue_t(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity), enable_overflow_(capacity == 0) {
        }

        bool is_empty() const {
            return data_.empty();
        }
        size_t free_count() const {
            // capacity_为0时，允许放入一个，但_queue会处于overflow状态
            return capacity_ - data_.size();
        }
        bool is_overflow() const {
            return enable_overflow_ && data_.size() >= capacity_;
        }

        bool is_closed() const {
            std::unique_lock<std::mutex> lock(this->mutex_);
            return this->closed_;
        }

        // close以后的入chan操作会返回false, 而出chan则在队列为空后才返回false
        void close() {
            std::unique_lock<std::mutex> lock(this->mutex_);
            this->closed_ = true;
            if (this->is_overflow()) {
                // 消除溢出
                this->data_.pop_back();
            }
            this->cv_.notify_all();
        }

        template <typename TR>
        bool pop(TR &data) {
            std::unique_lock<std::mutex> lock(this->mutex_);
            this->cv_.wait(lock, [&]() { return !is_empty() || closed_; });
            if (this->is_empty()) {
                return false;  // 已关闭
            }

            data = this->data_.front();
            this->data_.pop_front();
            this->pop_count_++;

            if (this->free_count() == 1) {
                // 说明以前是full或溢出状态
                this->cv_.notify_all();
            }

            return true;
        }

        template <typename TR>
        bool push(TR &&data) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return free_count() > 0 || closed_; });
            if (closed_) {
                return false;
            }

            data_.push_back(std::forward<TR>(data));
            if (data_.size() == 1) {
                cv_.notify_all();
            }

            // 当queue溢出,需等待queue回复正常
            if (is_overflow()) {
                const size_t old = this->pop_count_;
                cv_.wait(lock, [&]() { return old != pop_count_ || closed_; });
            }

            return !this->closed_;
        }
    };
    std::shared_ptr<queue_t> queue_;

public:
    explicit chan(size_t capacity = 0) {
        queue_ = std::make_shared<queue_t>(capacity);
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
        return queue_->push(std::forward<TR>(data));
    }

    // 出chan(支持兼容类型的出chan)
    template <typename TR>
    bool operator>>(TR &data) {
        return queue_->pop(data);
    }

    // close以后的入chan操作返回false, 而出chan则在队列为空后才返回false
    void close() {
        queue_->close();
    }

    bool is_closed() const {
        return queue_->is_closed();
    }
};
