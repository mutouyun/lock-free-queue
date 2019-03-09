#pragma once

#include <tuple>
#include <mutex>
#include <memory>
#include <functional>

namespace lock {

template <typename T>
class pool {

    union node {
        T     data_;
        node* next_;
    } * cursor_ = nullptr;

    mutable std::mutex mtx_;

public:
    ~pool() {
        while (cursor_ != nullptr) {
            auto temp = cursor_->next_;
            delete cursor_;
            cursor_ = temp;
        }
    }

    bool empty() const {
        auto guard = std::unique_lock { mtx_ };
        return cursor_ == nullptr;
    }

    template <typename... P>
    T* alloc(P&&... pars) {
        auto guard = std::unique_lock { mtx_ };
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
        auto guard = std::unique_lock { mtx_ };
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
    mutable std::mutex mtx_;

public:
    void quit() {}

    bool empty() const {
        auto guard = std::unique_lock { mtx_ };
        return head_ == nullptr;
    }

    bool push(T const & val) {
        auto p = allocator_.alloc(val, nullptr);
        auto guard = std::unique_lock { mtx_ };
        if (tail_ == nullptr) {
            head_ = tail_ = p;
        }
        else {
            tail_->next_ = p;
            tail_ = p;
        }
        return true;
    }

    std::tuple<T, bool> pop() {
        std::unique_ptr<node, std::function<void(node*)>> temp;
        auto guard = std::unique_lock { mtx_ };
        if (head_ == nullptr) {
            return {};
        }
        auto ret  = std::make_tuple(head_->data_, true);
        temp = decltype(temp) {
            head_, [this](node* temp) { allocator_.free(temp); }
        };
        head_ = head_->next_;
        if (tail_ == temp.get()) {
            tail_ = nullptr;
        }
        return ret;
    }
};

} // namespace lock
