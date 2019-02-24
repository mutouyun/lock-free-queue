#pragma once

#include <atomic>
#include <new>
#include <utility>

namespace s2s {

template <typename T>
class pool {

    union node {
        T     data_;
        node* next_;
    };

    std::atomic<node*> cursor_ { nullptr };

public:
    ~pool() {
        auto curr = cursor_.load(std::memory_order_acquire);
        while (curr != nullptr) {
            auto temp = curr->next_;
            delete curr;
            curr = temp;
        }
    }

    bool empty() const {
        return cursor_ == nullptr;
    }

    template <typename... P>
    T* alloc(P&&... pars) {
        node* curr = cursor_.load(std::memory_order_acquire);
        if (curr == nullptr) {
            return &((new node { std::forward<P>(pars)... })->data_);
        }
        while (1) {
            if (cursor_.compare_exchange_weak(curr, curr->next_, std::memory_order_acq_rel)) {
                break;
            }
        }
        return ::new (&(curr->data_)) T { std::forward<P>(pars)... };
    }

    void free(void* p) {
        if (p == nullptr) return;
        auto temp = reinterpret_cast<node*>(p);
        node* curr = cursor_.load(std::memory_order_acquire);
        while (1) {
            temp->next_ = curr;
            if (cursor_.compare_exchange_weak(curr, temp, std::memory_order_acq_rel)) {
                break;
            }
        }
    }
};

template <typename T>
class queue {

    struct node {
        T data_;
        std::atomic<node*> next_;
    } dummy_ { {}, nullptr };

    std::atomic<node*> head_ { &dummy_ };
    std::atomic<node*> tail_ { nullptr };

    pool<node> allocator_;

public:
    bool empty() const {
        return head_.load(std::memory_order_acquire)->next_ == nullptr;
    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, nullptr);
        auto curr = tail_.exchange(n, std::memory_order_acq_rel);
        if (curr == nullptr) {
            head_.load()->next_.store(n, std::memory_order_release);
            return;
        }
        curr->next_.store(n, std::memory_order_release);
    }

    std::tuple<T, bool> pop() {
        auto curr = head_.load(std::memory_order_acquire);
        auto next = curr->next_.load(std::memory_order_acquire);
        if (next == nullptr) {
            return {};
        }
        head_.store(next, std::memory_order_release);
        if (curr != &dummy_) {
            allocator_.free(curr);
        }
        return std::make_tuple(next->data_, true);
    }
};

} // namespace s2s
