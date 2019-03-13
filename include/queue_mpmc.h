#pragma once

#include <atomic>
#include <new>
#include <utility>
#include <tuple>
#include <cstdint>
#include <thread>
#include <limits>

#include "queue_spsc.h"

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

    tagged(std::uint64_t num)
        : data_(num)
    {}

    tagged(T ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged(T ptr, std::uint64_t tag)
        : data_(reinterpret_cast<std::uint64_t>(ptr) | (tag & ~mask))
    {}

    tagged& operator=(tagged const &) = default;

    friend bool operator==(tagged a, tagged b) { return a.data_ == b.data_; }
    friend bool operator!=(tagged a, tagged b) { return !(a == b); }

    static std::uint64_t add(std::uint64_t tag) { return tag + incr; }
    static std::uint64_t del(std::uint64_t tag) { return tag - incr; }

    explicit operator T() const {
        auto ret = data_ & mask;
        return *reinterpret_cast<T*>(&ret);
    }

    std::uint64_t data() const {
        return data_;
    }

    T ptr() const {
        return static_cast<T>(*this);
    }

    T    operator->() const { return  static_cast<T>(*this); }
    auto operator* () const { return *static_cast<T>(*this); }
};

template <typename F>
class scope_exit {
    F f_;

public:
    scope_exit(F f) : f_(f) {}
    ~scope_exit() { f_(); }
};

} // namespace detail

template <typename T>
class tagged {
public:
    using dt_t = detail::tagged<T>;

private:
    std::atomic<std::uint64_t> data_ { 0 };

    bool compare_exchange(bool (std::atomic<std::uint64_t>::* cas)(std::uint64_t&, std::uint64_t, std::memory_order),
                          dt_t& exp, T val, std::memory_order order) {
        auto num = exp.data();
        auto guard = detail::scope_exit { [&] { exp = num; } };
        return (data_.*cas)(num, dt_t { val, dt_t::add(num) }.data(), order);
    }

public:
    tagged() = default;
    tagged(tagged const &) = default;

    tagged(T ptr)
        : data_(reinterpret_cast<std::uint64_t>(ptr))
    {}

    tagged& operator=(tagged const &) = default;

    auto load(std::memory_order order) const {
        return static_cast<T>(dt_t { data_.load(order) });
    }

    auto tag_load(std::memory_order order) const {
        return dt_t { data_.load(order) };
    }

    auto exchange(T val, std::memory_order order) {
        auto old = this->tag_load(std::memory_order_relaxed);
        while (!this->compare_exchange_weak(old, val, order)) ;
        return old;
    }

    void store(T val, std::memory_order order) {
        this->exchange(val, order);
    }

    bool compare_exchange_weak(dt_t& exp, T val, std::memory_order order) {
        return compare_exchange(&std::atomic<std::uint64_t>::compare_exchange_weak, exp, val, order);
    }

    bool compare_exchange_strong(dt_t& exp, T val, std::memory_order order) {
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
        typename tagged<node*>::dt_t curr = el_.exchange(nullptr, std::memory_order_relaxed);
        if (curr.ptr() == nullptr) {
            curr = cursor_.tag_load(std::memory_order_acquire);
            while (1) {
                if (curr.ptr() == nullptr) {
                    return &((new node { std::forward<P>(pars)... })->data_);
                }
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
        auto curr = cursor_.tag_load(std::memory_order_relaxed);
        while (1) {
            temp->next_.store(curr.ptr(), std::memory_order_relaxed);
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
        tagged<node*> next_;
    };

    pool<node> allocator_;

    tagged<node*> head_ { allocator_.alloc() };
    tagged<node*> tail_ { head_.load(std::memory_order_relaxed) };

public:
    void quit() {}

    bool empty() const {
        return head_.load(std::memory_order_acquire)
             ->next_.load(std::memory_order_relaxed) == nullptr;
    }

    bool push_v1(T const & val) {
        auto p = allocator_.alloc(val, nullptr);
        while (1) {
            auto tail = tail_.tag_load(std::memory_order_relaxed);
            auto next = tail->next_.tag_load(std::memory_order_acquire);
            if (next.ptr() == nullptr &&
                tail->next_.compare_exchange_weak(next, p, std::memory_order_relaxed)) {
                tail_.compare_exchange_strong(tail, p, std::memory_order_release);
                return true;
            }
        }
    }

    bool push_v2(T const & val) {
        auto p = allocator_.alloc(val, nullptr);
        auto tail = tail_.tag_load(std::memory_order_relaxed);
        while (1) {
            auto next = tail->next_.tag_load(std::memory_order_acquire);
            if (next.ptr() == nullptr) {
                if (tail->next_.compare_exchange_weak(next, p, std::memory_order_relaxed)) {
                    tail_.compare_exchange_strong(tail, p, std::memory_order_release);
                    return true;
                }
            }
            else if (!tail_.compare_exchange_weak(tail, next.ptr(), std::memory_order_relaxed)) {
                continue;
            }
            tail = tail_.tag_load(std::memory_order_relaxed);
        }
    }

    bool push_v3(T const & val) {
        auto p = allocator_.alloc(val, nullptr);
        tail_.exchange(p, std::memory_order_relaxed)
         ->next_.store(p, std::memory_order_release);
        return true;
    }

    std::tuple<T, bool> pop_v1() {
        auto head = head_.tag_load(std::memory_order_acquire);
        while (1) {
            auto next = head->next_.load(std::memory_order_acquire);
            if (next == nullptr) {
                return {};
            }
            auto ret = std::make_tuple(next->data_, true);
            if (head_.compare_exchange_weak(head, next, std::memory_order_acquire)) {
                allocator_.free(head.ptr());
                return ret;
            }
        }
    }

    std::tuple<T, bool> pop_v2() {
        auto head = head_.tag_load(std::memory_order_relaxed);
        auto tail = tail_.tag_load(std::memory_order_relaxed);
        while (1) {
            auto next = head->next_.load(std::memory_order_acquire);
            if (next == nullptr) {
                return {};
            }
            if (head.ptr() == tail.ptr()) {
                if (!tail_.compare_exchange_weak(tail, next, std::memory_order_relaxed)) {
                    head = head_.tag_load(std::memory_order_relaxed);
                    continue;
                }
            }
            else {
                auto ret = std::make_tuple(next->data_, true);
                if (head_.compare_exchange_weak(head, next, std::memory_order_acquire)) {
                    allocator_.free(head.ptr());
                    return ret;
                }
                tail = tail_.tag_load(std::memory_order_acquire);
                continue;
            }
            head = head_.tag_load(std::memory_order_relaxed);
            tail = tail_.tag_load(std::memory_order_relaxed);
        }
    }

    /*
     * Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms
     *  - Maged M. Michael, Michael L. Scott
     * http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
    */

    bool push(T const & val) {
        auto p = allocator_.alloc(val, nullptr);
        auto tail = tail_.tag_load(std::memory_order_relaxed);
        while (1) {
            auto next = tail->next_.tag_load(std::memory_order_acquire);
            if (tail == tail_.tag_load(std::memory_order_relaxed)) {
                if (next.ptr() == nullptr) {
                    if (tail->next_.compare_exchange_weak(next, p, std::memory_order_relaxed)) {
                        tail_.compare_exchange_strong(tail, p, std::memory_order_release);
                        break;
                    }
                }
                else if (!tail_.compare_exchange_weak(tail, next.ptr(), std::memory_order_relaxed)) {
                    continue;
                }
            }
            tail = tail_.tag_load(std::memory_order_relaxed);
        }
        return true;
    }

    std::tuple<T, bool> pop() {
        auto head = head_.tag_load(std::memory_order_acquire);
        auto tail = tail_.tag_load(std::memory_order_acquire);
        while (1) {
            auto next = head->next_.load(std::memory_order_acquire);
            if (head == head_.tag_load(std::memory_order_relaxed)) {
                if (head.ptr() == tail.ptr()) {
                    if (next == nullptr) {
                        return {};
                    }
                    tail_.compare_exchange_weak(tail, next, std::memory_order_relaxed);
                }
                else {
                    auto ret = std::make_tuple(next->data_, true);
                    if (head_.compare_exchange_weak(head, next, std::memory_order_acquire)) {
                        allocator_.free(head.ptr());
                        return ret;
                    }
                    tail = tail_.tag_load(std::memory_order_acquire);
                    continue;
                }
            }
            head = head_.tag_load(std::memory_order_acquire);
            tail = tail_.tag_load(std::memory_order_acquire);
        }
    }
};

} // namespace mpmc

namespace spmc {

template <typename T>
class qring : public spsc::qring<T> {
protected:
    using spsc::qring<T>::rd_;
    using spsc::qring<T>::wt_;
    using spsc::qring<T>::block_;
    using spsc::qring<T>::index_of;

public:
    /*
     * Yet another implementation of a lock-free circular array queue
     *  - Faustino Frechilla
     * https://www.codeproject.com/Articles/153898/Yet-another-implementation-of-a-lock-free-circular
    */
    std::tuple<T, bool> pop() {
        auto cur_rd = rd_.load(std::memory_order_relaxed);
        while (1) {
            auto id_rd = index_of(cur_rd);
            if (id_rd == index_of(wt_.load(std::memory_order_acquire))) {
                return {}; // empty
            }
            auto ret = std::make_tuple(block_[id_rd], true);
            if (rd_.compare_exchange_weak(cur_rd, cur_rd + 1, std::memory_order_release)) {
                return ret;
            }
        }
    }
};

} // namespace spmc

namespace mpmc {

template <typename T>
class qlock : public spmc::qring<T> {
    using base_t = spmc::qring<T>;

protected:
    using typename base_t::ti_t;

    using base_t::rd_;
    using base_t::wt_;
    using base_t::block_;
    using base_t::index_of;

    std::atomic<ti_t> ct_ { 0 }; // commit index

public:
    /*
     * Yet another implementation of a lock-free circular array queue
     *  - Faustino Frechilla
     * https://www.codeproject.com/Articles/153898/Yet-another-implementation-of-a-lock-free-circular
    */
    bool push(T const & val) {
        ti_t cur_ct = ct_.load(std::memory_order_acquire), nxt_ct;
        while (1) {
            if (index_of(nxt_ct = cur_ct + 1) ==
                index_of(rd_.load(std::memory_order_acquire))) {
                return false; // full
            }
            if (ct_.compare_exchange_weak(cur_ct, nxt_ct, std::memory_order_acq_rel)) {
                break;
            }
        }
        block_[index_of(cur_ct)] = val;
        while (1) {
//            if (cur_ct == wt_.load(std::memory_order_relaxed)) {
//                wt_.store(nxt_ct, std::memory_order_release);
//                return true;
//            }
            auto exp_wt = cur_ct;
            if (wt_.compare_exchange_weak(exp_wt, nxt_ct, std::memory_order_release)) {
                return true;
            }
            std::this_thread::yield();
        }
    }
};

enum : std::uint64_t {
    invalid_index = (std::numeric_limits<std::uint64_t>::max)()
};

template <typename T>
struct rnode {
    T data_;
    std::atomic<std::uint64_t> f_ct_ { invalid_index }; // commit flag
};

template <typename T>
class qring : public qlock<rnode<T>> {
    using base_t = qlock<rnode<T>>;

protected:
    using typename base_t::ti_t;

    using base_t::rd_;
    using base_t::wt_;
    using base_t::ct_;
    using base_t::block_;
    using base_t::index_of;

public:
    bool push(T const & val) {
        ti_t cur_ct = ct_.load(std::memory_order_acquire), nxt_ct;
        while (1) {
            if (index_of(nxt_ct = cur_ct + 1) ==
                index_of(rd_.load(std::memory_order_acquire))) {
                return false; // full
            }
            if (ct_.compare_exchange_weak(cur_ct, nxt_ct, std::memory_order_acq_rel)) {
                break;
            }
        }
        auto* item = block_ + index_of(cur_ct);
        item->data_ = val;
        item->f_ct_.store(cur_ct, std::memory_order_release);
        while (1) {
            auto cac_ct = item->f_ct_.load(std::memory_order_acquire);
            if (cur_ct != wt_.load(std::memory_order_acquire)) {
                return true;
            }
            if (cac_ct != cur_ct) {
                return true;
            }
            if (!item->f_ct_.compare_exchange_strong(cac_ct, invalid_index, std::memory_order_relaxed)) {
                return true;
            }
            wt_.store(nxt_ct, std::memory_order_release);
            cur_ct = nxt_ct;
            nxt_ct = cur_ct + 1;
            item = block_ + index_of(cur_ct);
        }
    }

    std::tuple<T, bool> pop() {
        auto cur_rd = rd_.load(std::memory_order_relaxed);
        while (1) {
            auto id_rd  = index_of(cur_rd);
            auto cur_wt = wt_.load(std::memory_order_acquire);
            if (id_rd == index_of(cur_wt)) {
                auto* item = block_ + index_of(cur_wt);
                auto cac_ct = item->f_ct_.load(std::memory_order_acquire);
                if (cac_ct != cur_wt) {
                    return {}; // empty
                }
                if (item->f_ct_.compare_exchange_weak(cac_ct, invalid_index, std::memory_order_relaxed)) {
                    wt_.store(cur_wt + 1, std::memory_order_release);
                }
                cur_rd = rd_.load(std::memory_order_relaxed);
            }
            else {
                auto ret = std::make_tuple(block_[id_rd].data_, true);
                if (rd_.compare_exchange_weak(cur_rd, cur_rd + 1, std::memory_order_release)) {
                    return ret;
                }
            }
        }
    }
};

template <typename T>
class qring2 : public spsc::qring<rnode<T>> {
    using base_t = spsc::qring<rnode<T>>;

protected:
    using base_t::rd_;
    using base_t::wt_;
    using base_t::block_;
    using base_t::index_of;

    std::atomic<bool> quit_ { false };

public:
    void quit() {
        quit_.store(true, std::memory_order_relaxed);
    }

    /*
     * A bounded wait-free(almost) zero-copy MPMC queue written in C++11, which can also reside in SHM for IPC
     *  - MengRao/WFMPMC
     * https://github.com/MengRao/WFMPMC
    */

    bool push(T const & val) {
        auto cur_wt = wt_.fetch_add(1, std::memory_order_relaxed);
        auto& item = block_[index_of(cur_wt)];
        while (1) {
            auto cac_id = item.f_ct_.load(std::memory_order_acquire);
            if (cac_id == cur_wt || cac_id == invalid_index) {
                break;
            }
            std::this_thread::yield(); // full
        }
        item.data_ = val;
        item.f_ct_.store(static_cast<decltype(cur_wt)>(~cur_wt), std::memory_order_release);
        return true;
    }

    std::tuple<T, bool> pop() {
        auto cur_rd = rd_.fetch_add(1, std::memory_order_relaxed);
        auto& item = block_[index_of(cur_rd)];
        while (item.f_ct_.load(std::memory_order_acquire) != static_cast<decltype(cur_rd)>(~cur_rd)) {
            if (quit_.load(std::memory_order_relaxed)) {
                return {};
            }
            std::this_thread::yield(); // empty
        }
        auto ret = std::make_tuple(item.data_, true);
        item.f_ct_.store(cur_rd + base_t::elem_max, std::memory_order_release);
        return ret;
    }
};

} // namespace mpmc
