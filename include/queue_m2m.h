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
#include <thread>

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

    tagged<node*> cursor_ { nullptr };

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
        while (1) {
            auto curr = cursor_.load(std::memory_order_acquire);
            if (curr == nullptr) {
                return &((new node { std::forward<P>(pars)... })->data_);
            }
            if (cursor_.compare_exchange_weak(curr, curr->next_, std::memory_order_acquire)) {
                return ::new (&(curr->data_)) T { std::forward<P>(pars)... };
            }
            std::this_thread::yield();
        }
    }

    void free(void* p) {
        if (p == nullptr) return;
        auto temp = reinterpret_cast<node*>(p);
        while (1) {
            auto curr = cursor_.load(std::memory_order_relaxed);
            temp->next_.store(curr, std::memory_order_relaxed);
            if (cursor_.compare_exchange_weak(curr, temp, std::memory_order_release)) {
                break;
            }
            std::this_thread::yield();
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

    std::atomic<unsigned> counter_   { 0 };
    std::atomic<node*>    free_list_ { nullptr };

    void add_ref() {
        counter_.fetch_add(1, std::memory_order_release);
    }

    void del_ref(node* item) {
        if (item == &dummy_ || item == nullptr) {
            counter_.fetch_sub(1, std::memory_order_release);
            return;
        }
        auto put_free_list = [this](node* first, node* last) {
            auto list = free_list_.load(std::memory_order_relaxed);
            while (1) {
                last->next_.store(list, std::memory_order_relaxed);
                if (free_list_.compare_exchange_weak(list, first, std::memory_order_release)) {
                    break;
                }
            }
        };
        if (counter_.load(std::memory_order_acquire) > 1) {
            put_free_list(item, item);
            counter_.fetch_sub(1, std::memory_order_release);
        }
        else {
            auto temp = free_list_.exchange(nullptr, std::memory_order_acq_rel);
            if (counter_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                while (temp != nullptr) {
                    auto next = temp->next_.load(std::memory_order_relaxed);
                    allocator_.free(temp);
                    temp = next;
                }
            }
            else if (temp != nullptr) {
                // fetch the last
                auto last = temp;
                while (1) {
                    auto next = last->next_.load(std::memory_order_relaxed);
                    if (next == nullptr) {
                        break;
                    }
                    last = next;
                }
                put_free_list(temp, last);
            }
            allocator_.free(item);
        }
    }

public:
    void quit() {}

    bool empty() const {
        return head_.load(std::memory_order_acquire)
             ->next_.load(std::memory_order_relaxed) == nullptr;
    }

    void push(T const & val) {
        auto n = allocator_.alloc(val, nullptr);
        while (1) {
            auto tail = tail_.load(std::memory_order_relaxed);
            auto next = tail->next_.load(std::memory_order_relaxed);
            if (tail == tail_.load(std::memory_order_relaxed)) {
                if (next == nullptr) {
                    if (tail->next_.compare_exchange_weak(next, n, std::memory_order_relaxed)) {
                        tail_.compare_exchange_strong(tail, n, std::memory_order_release);
                        break;
                    }
                }
                else tail_.compare_exchange_strong(tail, next, std::memory_order_release);
            }
            std::this_thread::yield();
        }
    }

    std::tuple<T, bool> pop() {
        T ret;
        while (1) {
            auto head = head_.load(std::memory_order_acquire);
            auto tail = tail_.load(std::memory_order_relaxed);
            auto next = head->next_.load(std::memory_order_relaxed);
            if (head == head_.load(std::memory_order_relaxed)) {
                if (head.ptr() == tail.ptr()) {
                    if (next == nullptr) {
                        return {};
                    }
                    tail_.compare_exchange_strong(tail, next, std::memory_order_relaxed);
                }
                else {
                    ret = next->data_;
                    if (head_.compare_exchange_weak(head, next, std::memory_order_relaxed)) {
                        if (head != &dummy_) {
                            allocator_.free(head);
                        }
                        break;
                    }
                }
            }
            std::this_thread::yield();
        }
        return std::make_tuple(ret, true);
    }
};

} // namespace m2m
