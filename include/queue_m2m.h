/*
 * https://coolshell.cn/articles/8239.html
 * http://www.voidcn.com/article/p-sijuqlbv-zs.html
 * http://blog.jobbole.com/107955/
*/

#pragma once

#include <atomic>
#include <new>
#include <utility>
#include <tuple>
#include <cstdint>

namespace m2m {
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

    enum : std::uint64_t {
        mask = tagged_factor<Bytes>::mask,
        incr = tagged_factor<Bytes>::incr
    };

    std::uint64_t data_ { 0 };

public:
    tagged() = default;
    tagged(tagged const &) = default;

    tagged(T ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged(T ptr, std::uint64_t tag)
        : data_(reinterpret_cast<std::uint64_t>(ptr) | ((tag + incr) & ~mask))
    {}

    tagged(std::uint64_t num)
        : data_(num)
    {}

    tagged& operator=(tagged const &) = default;

    std::uint64_t data() const {
        return data_;
    }

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

public:
    tagged() = default;
    tagged(tagged const &) = default;

    tagged(T ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged& operator=(tagged const &) = default;

    operator T() const { return load(); }

    T    operator->() const { return  static_cast<T>(*this); }
    auto operator* () const { return *static_cast<T>(*this); }

    auto load(std::memory_order order) {
        return detail::tagged<T>{ data_.load(order) };
    }

    bool compare_exchange_weak(detail::tagged<T>& exp, T val, std::memory_order order) {
        auto num = exp.data();
        if (data_.compare_exchange_weak(num, detail::tagged<T>{ val, num }.data(), order)) {
            return true;
        }
        exp = num;
        return false;
    }
};

template <typename T>
class pool {

    union node {
        T     data_;
        node* next_;
    };

    tagged<node*> cursor_ { nullptr };

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
        auto curr = cursor_.load(std::memory_order_acquire);
        while (1) {
            if (curr == nullptr) {
                return &((new node { std::forward<P>(pars)... })->data_);
            }
            if (cursor_.compare_exchange_weak(curr, curr->next_, std::memory_order_acq_rel)) {
                break;
            }
        }
        return ::new (&(curr->data_)) T { std::forward<P>(pars)... };
    }

    void free(void* p) {
        if (p == nullptr) return;
        auto temp = reinterpret_cast<node*>(p);
        auto curr = cursor_.load(std::memory_order_acquire);
        while (1) {
            temp->next_ = curr;
            if (cursor_.compare_exchange_weak(curr, temp, std::memory_order_acq_rel)) {
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
        std::atomic<unsigned> counter_;
        std::atomic<node*>    next_;
    } dummy_ { {}, 0u, nullptr };

    tagged     <node*> head_ { &dummy_ };
    std::atomic<node*> tail_ { nullptr };

    pool<node> allocator_;

    enum {
        ref_invalid,
        ref_succ,
        ref_fail
    };

    int add_ref(node* item) {
        if (item == &dummy_) return ref_succ;
        auto cnt = item->counter_.load(std::memory_order_acquire);
        if (cnt == 0) {
            return ref_invalid;
        }
        return item->counter_.compare_exchange_weak(cnt, cnt + 1, std::memory_order_release) ?
                    ref_succ : ref_fail;
    }

    void del_ref(node* item) {
        if (item == &dummy_) return;
        if (item->counter_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            allocator_.free(item);
        }
    }

public:
    bool empty() const {
        return head_.load(std::memory_order_acquire)->next_ == nullptr;
    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, 1u, nullptr);
        auto curr = tail_.exchange(n, std::memory_order_acq_rel);
        if (curr == nullptr) {
            head_.load(std::memory_order_acquire)->next_.store(n, std::memory_order_release);
            return;
        }
        curr->next_.store(n, std::memory_order_release);
    }

    std::tuple<T, bool> pop() {
        T ret;
        auto curr = head_.load(std::memory_order_acquire);
        while (1) {
        next_loop:
            while (1) {
                switch (add_ref(curr)) {
                case ref_succ:
                    break;
                case ref_invalid:
                    curr = head_.load(std::memory_order_acquire);
                default:
                    continue;
                }
                break;
            }

            node* next = curr->next_.load(std::memory_order_acquire);
            if (next == nullptr) {
                return {};
            }

            while (add_ref(next) != ref_succ) {
                auto temp = head_.load(std::memory_order_acquire);
                if (curr == temp) continue;
                del_ref(curr);
                curr = temp;
                goto next_loop;
            }

            auto guard_next = scope_exit {[this, next] {
                del_ref(next);
            }};
            auto guard_curr = scope_exit {[this, curr] {
                del_ref(curr);
            }};
            if (head_.compare_exchange_weak(curr, next, std::memory_order_acq_rel)) {
                ret = next->data_;
                break;
            }
        }
        return std::make_tuple(ret, true);
    }
};

} // namespace m2m
