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

    auto load() {
        return detail::tagged<T>{ data_.load() };
    }

    bool compare_exchange_weak(detail::tagged<T>& exp, T val) {
        auto num = exp.data();
        if (data_.compare_exchange_weak(num, detail::tagged<T>{ val, num }.data())) {
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
        auto curr = cursor_.load();
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
        auto curr = cursor_.load();
        while (1) {
            if (curr == nullptr) {
                return &((new node { std::forward<P>(pars)... })->data_);
            }
            if (cursor_.compare_exchange_weak(curr, curr->next_)) {
                break;
            }
        }
        return ::new (&(curr->data_)) T { std::forward<P>(pars)... };
    }

    void free(void* p) {
        if (p == nullptr) return;
        auto temp = reinterpret_cast<node*>(p);
        auto curr = cursor_.load();
        while (1) {
            temp->next_ = curr;
            if (cursor_.compare_exchange_weak(curr, temp)) {
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

        template <typename A>
        static node* alloc(A& alc, T const & val) {
            return alc.alloc(val, 1u, nullptr);
        }

        template <typename A>
        void free(A& alc, node* dummy) {
            if (this != dummy && counter_.fetch_sub(1) == 1) {
                alc.free(this);
            }
        }
    } dummy_ { {}, 0u, nullptr };

    tagged     <node*> head_ { &dummy_ };
    std::atomic<node*> tail_ { nullptr };

    pool<node> allocator_;
    mutable std::mutex mtx_;

public:
    bool empty() const {
        return head_.load()->next_ == nullptr;
    }

    void push(T const & val) {
        auto n = node::alloc(allocator_, val);
        auto curr = tail_.exchange(n);
        if (curr == nullptr) {
            head_.load()->next_.store(n);
            return;
        }
        curr->next_.store(n);
    }

    std::tuple<T, bool> pop() {
        auto curr = head_.load();
        node* next;
        T ret;

        while (1) {
            if ((next = curr->next_.load()) == nullptr) {
                return {};
            }

            auto cnt = next->counter_.load();
            if (cnt == 0) {
                curr = head_.load();
                continue;
            }
            if (!next->counter_.compare_exchange_strong(cnt, cnt + 1)) {
                continue;
            }

            auto guard_next = scope_exit {[this, next] {
                next->free(allocator_, &dummy_);
            }};
            if (head_.compare_exchange_weak(curr, next)) {
                ret = next->data_;
                curr->free(allocator_, &dummy_);
                break;
            }
        }
        return std::make_tuple(ret, true);
    }
};

} // namespace m2m
