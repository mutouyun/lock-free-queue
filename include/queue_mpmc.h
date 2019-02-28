/*
 * https://coolshell.cn/articles/8239.html
 * http://www.voidcn.com/article/p-sijuqlbv-zs.html
 * http://blog.jobbole.com/107955/
 * http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
*/

#pragma once

#include <atomic>
#include <new>
#include <utility>
#include <tuple>
#include <cstdint>

namespace mpmc {
namespace detail {

template <std::size_t Bytes>
struct tagged_factor;

template <>
struct tagged_factor<4> {
    enum : std::uint64_t {
        mask = 0x00000000fffffffful,
        incr = 0x0000000100000000ul
    };
};

template <>
struct tagged_factor<8> {
    enum : std::uint64_t {
        mask = 0x0000fffffffffffful,
        incr = 0x0001000000000000ul
    };
};

template <typename T, std::size_t Bytes = sizeof(T)>
class tagged {

    std::uint64_t data_ { 0 };

public:
    enum : std::uint64_t {
        mask = tagged_factor<Bytes>::mask,
        incr = tagged_factor<Bytes>::incr
    };

    tagged() = default;
    tagged(tagged const &) = default;

    tagged(T ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged(T ptr, std::uint64_t tag)
        : data_(reinterpret_cast<std::uint64_t>(ptr) | (add(tag) & ~mask))
    {}

    tagged(std::uint64_t num)
        : data_(num)
    {}

    tagged& operator=(tagged const &) = default;

    std::uint64_t data() const {
        return data_;
    }

    T ptr() const {
        return static_cast<T>(*this);
    }

    static std::uint64_t add(std::uint64_t tag) { return tag + incr; }
    static std::uint64_t del(std::uint64_t tag) { return tag - incr; }

    operator T() const {
        return reinterpret_cast<T>(data_ & mask);
    }

    T    operator->() const { return  static_cast<T>(*this); }
    auto operator* () const { return *static_cast<T>(*this); }
};

} // namespace detail

template <typename T>
class tagged {

    std::atomic<std::uint64_t> data_ { 0 };

    bool compare_exchange(bool (std::atomic<std::uint64_t>::* cas)(std::uint64_t&, std::uint64_t, std::memory_order),
                               detail::tagged<T>& exp, T val, std::memory_order order) {
        auto num = exp.data();
        if ((data_.*cas)(num, detail::tagged<T> { val, num }.data(), order)) {
            return true;
        }
        exp = num;
        return false;
    }

public:
    tagged() = default;
    tagged(tagged const &) = default;

    tagged(T ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged& operator=(tagged const &) = default;

    operator T() const { return load(std::memory_order_acquire); }

    T    operator->() const { return  static_cast<T>(*this); }
    auto operator* () const { return *static_cast<T>(*this); }

    auto load(std::memory_order order) const {
        return detail::tagged<T> { data_.load(order) };
    }

    void store(T val, std::memory_order order) {
        auto exp = this->load(std::memory_order_relaxed);
        while (!this->compare_exchange_weak(exp, val, order)) ;
    }

    bool compare_exchange_weak(detail::tagged<T>& exp, T val, std::memory_order order) {
        return compare_exchange(&std::atomic<std::uint64_t>::compare_exchange_weak, exp, val, order);
    }

    bool compare_exchange_strong(detail::tagged<T>& exp, T val, std::memory_order order) {
        return compare_exchange(&std::atomic<std::uint64_t>::compare_exchange_strong, exp, val, order);
    }
};

template <typename T>
class pool {

    union node {
        T data_;
        tagged<node*> next_;
    };

    tagged     <node*> cursor_ { nullptr };
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
        detail::tagged<node*> curr = el_.exchange(nullptr, std::memory_order_relaxed);
        if (curr == nullptr) {
            curr = cursor_.load(std::memory_order_acquire);
            while (1) {
                if (curr == nullptr) {
                    return &((new node { std::forward<P>(pars)... })->data_);
                }
                if (cursor_.compare_exchange_weak(curr, curr->next_, std::memory_order_acquire)) {
                    return ::new (&(curr->data_)) T { std::forward<P>(pars)... };
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

template <typename F>
class scope_exit {
    F f_;

public:
    scope_exit(F f) : f_(f) {}
    ~scope_exit() { f_(); }
};

template <typename T>
class queue {

    struct node {
        T data_;
        tagged<node*> next_;
    } dummy_ { {}, nullptr };

    tagged<node*> head_ { &dummy_ };
    tagged<node*> tail_ { &dummy_ };

    pool<node> allocator_;

public:
    void quit() {}

    bool empty() const {
        return head_.load(std::memory_order_acquire)
             ->next_.load(std::memory_order_relaxed) == nullptr;
    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, nullptr);
        auto tail = tail_.load(std::memory_order_relaxed);
        while (1) {
            auto next = tail->next_.load(std::memory_order_relaxed);
            if (tail == tail_.load(std::memory_order_relaxed)) {
                if (next == nullptr) {
                    if (tail->next_.compare_exchange_weak(next, n, std::memory_order_relaxed)) {
                        tail_.compare_exchange_strong(tail, n, std::memory_order_release);
                        break;
                    }
                }
                else if (!tail_.compare_exchange_weak(tail, next, std::memory_order_release)) {
                    continue;
                }
            }
            tail = tail_.load(std::memory_order_relaxed);
        }
    }

    std::tuple<T, bool> pop() {
        T ret;
        auto head = head_.load(std::memory_order_relaxed);
        auto tail = tail_.load(std::memory_order_relaxed);
        while (1) {
            std::atomic_thread_fence(std::memory_order_acq_rel);
            auto next = head->next_.load(std::memory_order_relaxed);
            if (head == head_.load(std::memory_order_relaxed)) {
                if (next == nullptr) {
                    return {};
                }
                if (head.ptr() == tail.ptr()) {
                    if (!tail_.compare_exchange_weak(tail, next, std::memory_order_relaxed)) {
                        head = head_.load(std::memory_order_relaxed);
                        continue;
                    }
                }
                else {
                    ret = next->data_;
                    if (head_.compare_exchange_weak(head, next, std::memory_order_relaxed)) {
                        if (head != &dummy_) {
                            allocator_.free(head);
                        }
                        break;
                    }
                    else {
                        tail = tail_.load(std::memory_order_relaxed);
                        continue;
                    }
                }
            }
            head = head_.load(std::memory_order_relaxed);
            tail = tail_.load(std::memory_order_relaxed);
        }
        return std::make_tuple(ret, true);
    }
};

} // namespace mpmc
