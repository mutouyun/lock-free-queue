#pragma once

#include <atomic>
#include <new>
#include <utility>

namespace s2s {

template <typename T>
class pool {

    struct node {
        T     data_;
        node* next_;
    };

    std::atomic<node*> cursor_ { nullptr };

public:
    ~pool() {
        auto curr = cursor_.load();
        while (curr != nullptr) {
            auto tmp = curr->next_;
            delete curr;
            curr = tmp;
        }
    }

    bool empty() const {
        return cursor_ == nullptr;
    }

    template <typename... P>
    T* alloc(P&&... pars) {
        node* curr = cursor_.load();
        if (curr == nullptr) {
            return reinterpret_cast<T*>(new node { { std::forward<P>(pars)... }, nullptr });
        }
        void* p = curr;
        while (1) {
            if (cursor_.compare_exchange_weak(curr, curr->next_)) {
                break;
            }
            p = curr;
        }
        return ::new (p) T { std::forward<P>(pars)... };
    }

    void free(void* p) {
        if (p == nullptr) return;
        auto tmp = reinterpret_cast<node*>(p);
        node* curr = cursor_.load();
        while (1) {
            tmp->next_ = curr;
            if (cursor_.compare_exchange_weak(curr, tmp)) {
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
    };

    std::atomic<node*> head_ { nullptr };
    std::atomic<node*> tail_ { nullptr };

    pool<node> allocator_;

public:
    bool empty() const {
        return head_ == nullptr;
    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, nullptr);
        auto curr = tail_.exchange(n);
        if (curr == nullptr) {
            head_.store(n);
        }
        else {
            node* temp = nullptr;
            head_.compare_exchange_strong(temp, n);
            curr->next_.store(n);
        }
    }

    std::tuple<T, bool> pop() {
        auto curr = head_.load();
        if (curr == nullptr) {
            return {};
        }
        T val = curr->data_;
        head_.store(curr->next_.load());
        auto temp = curr;
        tail_.compare_exchange_strong(temp, nullptr);
        allocator_.free(curr);
        return std::make_tuple(val, true);
    }
};

} // namespace s2s
