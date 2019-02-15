#pragma once

#include <tuple>
#include <mutex>
#include <memory>
#include <functional>

namespace locked {

template <typename T>
class pool {

    struct node {
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
            return reinterpret_cast<T*>(new node { { std::forward<P>(pars)... }, nullptr });
        }
        void* p = cursor_;
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
    bool empty() const {
        auto guard = std::unique_lock { mtx_ };
        return head_ == nullptr;
    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, nullptr);
        auto guard = std::unique_lock { mtx_ };
        if (tail_ == nullptr) {
            head_ = tail_ = n;
        }
        else {
            tail_->next_ = n;
            tail_ = n;
        }
    }

    std::tuple<T, bool> pop() {
        std::unique_ptr<node, std::function<void(node*)>> temp;
        auto guard = std::unique_lock { mtx_ };
        if (head_ == nullptr) {
            return {};
        }
        T val = head_->data_;
        temp = decltype(temp) {
            head_, [this](node* temp) { allocator_.free(temp); }
        };
        head_ = head_->next_;
        if (tail_ == temp.get()) {
            tail_ = nullptr;
        }
        return std::make_tuple(val, true);
    }
};

} // namespace locked
