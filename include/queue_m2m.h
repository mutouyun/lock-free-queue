/*
 * https://coolshell.cn/articles/8239.html
 * http://www.voidcn.com/article/p-sijuqlbv-zs.html
 * http://blog.jobbole.com/107955/
*/

#pragma once

#include <atomic>
#include <new>
#include <utility>
#include <cstdint>

namespace m2m {

namespace detail {

template <std::size_t Bytes>
struct tagged_factor;

template <>
struct tagged_factor<4> {
    enum : std::uint64_t {
        mask = 0x00000000ffffffffUL,
        plus = 0x0000000100000000UL
    };
};

template <>
struct tagged_factor<8> {
    enum : std::uint64_t {
        mask = 0x0000ffffffffffffUL,
        plus = 0x0001000000000000UL
    };
};

template <typename T, std::size_t Bytes = sizeof(T*)>
class tagged_ptr {

    enum : std::uint64_t {
        mask = tagged_factor<Bytes>::mask,
        plus = tagged_factor<Bytes>::plus
    };

    std::uint64_t data_ { 0 };

public:
    tagged_ptr() = default;
    tagged_ptr(tagged_ptr const &) = default;

    tagged_ptr(T* ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged_ptr(std::uint64_t ptr)
        : data_(ptr)
    {}

    tagged_ptr& operator=(tagged_ptr const &) = default;

    std::uint64_t data() const {
        return data_;
    }

    operator T*() const {
        return reinterpret_cast<T*>(data_ & mask);
    }

    T* operator->() const { return  static_cast<T*>(*this); }
    T& operator* () const { return *static_cast<T*>(*this); }

    template <typename Atomic>
    static tagged_ptr acquire_unique(Atomic& ptr) {
        return { reinterpret_cast<T*>(ptr.fetch_add(plus) + plus) };
    }
};

} // namespace detail

template <typename T>
class tagged_ptr {

    std::atomic<std::uint64_t> data_ { 0 };

public:
    tagged_ptr() = default;
    tagged_ptr(tagged_ptr const &) = default;

    tagged_ptr(T* ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged_ptr& operator=(tagged_ptr const &) = default;

    operator T*() const {
        return detail::tagged_ptr<T>{ data_.load() };
    }

    T* operator->() const { return  static_cast<T*>(*this); }
    T& operator* () const { return *static_cast<T*>(*this); }

    auto acquire_unique() {
        return detail::tagged_ptr<T>::acquire_unique(data_);
    }

    auto load() {
        return acquire_unique();
    }

    bool compare_exchange_weak(detail::tagged_ptr<T>& exp, T* val) {
        auto num = exp.data();
        if (data_.compare_exchange_weak(num, reinterpret_cast<std::uint64_t>(val))) {
            return true;
        }
        exp = acquire_unique();
        return false;
    }
};

template <typename T>
class pool {

    union node {
        T     data_;
        node* next_;
    };

    tagged_ptr<node> cursor_ { nullptr };

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

template <typename T>
class queue {

    struct node {
        T data_;
        std::atomic<node*> next_;
    } dummy_ { {}, nullptr };

    tagged_ptr <node > head_ { &dummy_ };
    std::atomic<node*> tail_ { nullptr };

    pool<node> allocator_;

public:
    bool empty() const {
        return head_.load()->next_ == nullptr;
    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, nullptr);
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
        while (1) {
            if ((next = curr->next_.load()) == nullptr) {
                return {};
            }
            if (head_.compare_exchange_weak(curr, next)) {
                if (curr != &dummy_) {
                    allocator_.free(curr);
                }
                break;
            }
        }
        return std::make_tuple(next->data_, true);
    }
};

} // namespace m2m
