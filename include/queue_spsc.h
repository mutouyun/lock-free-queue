#pragma once

#include <atomic>
#include <new>
#include <utility>

namespace spsc {

template <typename T>
class pool {

    union node {
        T data_;
        std::atomic<node*> next_;
    };

    std::atomic<node*> cursor_ { nullptr };
    std::atomic<node*> el_     { nullptr };

public:
    ~pool() {
        auto curr = cursor_.load(std::memory_order_relaxed);
        while (curr != nullptr) {
            auto temp = curr->next_.load(std::memory_order_relaxed);
            delete curr;
            curr = temp;
        }
    }

    bool empty() const {
        return cursor_.load(std::memory_order_acquire) == nullptr;
    }

    template <typename... P>
    T* alloc(P&&... pars) {
        auto curr = el_.exchange(nullptr, std::memory_order_relaxed);
        if (curr == nullptr) {
            curr = cursor_.load(std::memory_order_acquire);
            if (curr == nullptr) {
                return &((new node { std::forward<P>(pars)... })->data_);
            }
            while (1) {
                auto next = curr->next_.load(std::memory_order_relaxed);
                if (cursor_.compare_exchange_weak(curr, next, std::memory_order_acquire)) {
                    break;
                }
            }
        }
        return ::new (&(curr->data_)) T { std::forward<P>(pars)... };
    }

    void free(void* p) {
        if (p == nullptr) return;
        auto temp = reinterpret_cast<node*>(p);
        temp = el_.exchange(temp, std::memory_order_relaxed);
        if (temp == nullptr) {
            return;
        }
        auto curr = cursor_.load(std::memory_order_relaxed);
        while (1) {
            temp->next_.store(curr, std::memory_order_relaxed);
            if (cursor_.compare_exchange_weak(curr, temp, std::memory_order_release)) {
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
    std::atomic<node*> tail_ { &dummy_ };

    pool<node> allocator_;

public:
    void quit() {}

    bool empty() const {
        return head_.load(std::memory_order_acquire)
             ->next_.load(std::memory_order_relaxed) == nullptr;
    }

//    void push(T const & val) {
//        auto n = allocator_.alloc(val, nullptr);
//        tail_.exchange(n, std::memory_order_relaxed)
//         ->next_.store(n, std::memory_order_release);
//    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, nullptr);
        auto t = tail_.load(std::memory_order_relaxed);
        t->next_.store(n, std::memory_order_relaxed);
        tail_.store(n, std::memory_order_release);
    }

    std::tuple<T, bool> pop() {
        auto curr = head_.load(std::memory_order_acquire);
        auto next = curr->next_.load(std::memory_order_relaxed);
        if (next == nullptr) {
            return {};
        }
        head_.store(next, std::memory_order_relaxed);
        if (curr != &dummy_) {
            allocator_.free(curr);
        }
        return std::make_tuple(next->data_, true);
    }
};

} // namespace spsc
