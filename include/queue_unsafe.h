#pragma once

#include <tuple>
#include <condition_variable>
#include <mutex>

namespace unsafe {

template <typename T>
class pool {

    union node {
        T     data_;
        node* next_;
    } * cursor_ = nullptr;

public:
    ~pool() {
        while (cursor_ != nullptr) {
            auto temp = cursor_->next_;
            delete cursor_;
            cursor_ = temp;
        }
    }

    bool empty() const {
        return cursor_ == nullptr;
    }

    template <typename... P>
    T* alloc(P&&... pars) {
        if (cursor_ == nullptr) {
            return &((new node { std::forward<P>(pars)... })->data_);
        }
        void* p = &(cursor_->data_);
        cursor_ = cursor_->next_;
        return ::new (p) T { std::forward<P>(pars)... };
    }

    void free(void* p) {
        if (p == nullptr) return;
        auto temp = reinterpret_cast<node*>(p);
        temp->next_ = cursor_;
        cursor_ = temp;
    }
};

template <typename T>
class queue {

    struct node {
        T      data_;
        node * next_;
    } * head_ = nullptr,
      * tail_ = nullptr;

    pool<node> allocator_;

public:
    bool empty() const {
        return head_ == nullptr;
    }

    void push(T const & val) {
        auto p = allocator_.alloc(val, nullptr);
        if (tail_ == nullptr) {
            head_ = tail_ = p;
        }
        else {
            tail_->next_ = p;
            tail_ = p;
        }
    }

    std::tuple<T, bool> pop() {
        if (head_ == nullptr) {
            return {};
        }
        T val = head_->data_;
        auto temp = head_;
        head_ = head_->next_;
        if (tail_ == temp) {
            tail_ = nullptr;
        }
        allocator_.free(temp);
        return std::make_tuple(val, true);
    }
};

} // namespace unsafe

namespace cond {

template <typename T>
class queue : unsafe::queue<T> {

    using base_t = unsafe::queue<T>;

    std::mutex              lock_;
    std::condition_variable cond_;

    bool quit_ = false;

public:
    ~queue() {
        quit();
    }

    void quit() {
        {
            auto guard = std::unique_lock { lock_ };
            quit_ = true;
        }
        cond_.notify_all();
    }

    bool empty() const {
        auto guard = std::unique_lock { lock_ };
        return base_t::empty();
    }

    void push(T const & val) {
        {
            auto guard = std::unique_lock { lock_ };
            base_t::push(val);
        }
        cond_.notify_one();
    }

    std::tuple<T, bool> pop() {
        auto guard = std::unique_lock { lock_ };
        while (!quit_) {
            auto ret = base_t::pop();
            if (std::get<1>(ret)) {
                return ret;
            }
            cond_.wait(guard);
        }
        return {};
    }
};

} // namespace cond
