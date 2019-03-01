#pragma once

#include <atomic>
#include <new>
#include <utility>
#include <limits>
#include <cstdint>

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

//    bool push(T const & val) {
//        auto p = allocator_.alloc(val, nullptr);
//        tail_.exchange(p, std::memory_order_relaxed)
//         ->next_.store(p, std::memory_order_release);
//        return true;
//    }

    bool push(T const & val) {
        auto p = allocator_.alloc(val, nullptr);
        auto t = tail_.load(std::memory_order_relaxed);
        t->next_.store(p, std::memory_order_relaxed);
        tail_.store(p, std::memory_order_release);
        return true;
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

template <typename T>
class qring {
public:
    enum : std::size_t {
        elem_max = (std::numeric_limits<std::uint8_t>::max)() + 1, // default is 255 + 1
    };

protected:
    T block_[elem_max];

    std::atomic<std::uint16_t> rd_ { 0 }; // read index
    std::atomic<std::uint16_t> wt_ { 0 }; // write index

    constexpr static std::uint8_t index_of(std::uint16_t index) noexcept {
        return static_cast<std::uint8_t>(index);
    }

public:
    void quit() {}

    bool empty() const {
        return index_of(rd_.load(std::memory_order_relaxed)) ==
               index_of(wt_.load(std::memory_order_acquire));
    }

    bool push(T const & val) {
        auto id_wt = index_of(wt_.load(std::memory_order_relaxed));
        if (id_wt == index_of(rd_.load(std::memory_order_acquire) - 1)) {
            return false; // full
        }
        block_[id_wt] = val;
        wt_.fetch_add(1, std::memory_order_release);
        return true;
    }

    std::tuple<T, bool> pop() {
        auto id_rd = index_of(rd_.load(std::memory_order_relaxed));
        if (id_rd == index_of(wt_.load(std::memory_order_acquire))) {
            return {}; // empty
        }
        auto ret = block_[id_rd];
        rd_.fetch_add(1, std::memory_order_release);
        return std::make_tuple(ret, true);
    }
};

} // namespace spsc
