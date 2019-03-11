#include <iostream>
#include <thread>
#include <vector>
#include <typeinfo>
#include <string>
#include <cstdint>
#include <type_traits>

#include "queue_unsafe.h"
#include "queue_locked.h"
#include "queue_spsc.h"
#include "queue_mpmc.h"

#if defined(__GNUC__)
#   include <memory>
#   include <cxxabi.h>  // abi::__cxa_demangle
#endif/*__GNUC__*/

#include "stopwatch.hpp"

template <typename T>
constexpr std::uint64_t calc(T n) {
    std::uint64_t r = n;
    return (r * (r - 1)) / 2;
}

template <typename T>
std::string type_name() {
#if defined(__GNUC__)
    const char* typeid_name = typeid(T).name();
    const char* real_name = abi::__cxa_demangle(typeid_name, nullptr, nullptr, nullptr);
    std::unique_ptr<void, decltype(::free)*> guard { (void*)real_name, ::free };
    if (real_name == nullptr) real_name = typeid_name;
    return real_name;
#else
    return typeid(T).name();
#endif/*__GNUC__*/
}

enum {
    loop_count = 80640,
    rept_count = 100
};

template <int PushN, int PopN, template <typename> class Queue>
void benchmark() {
    Queue<int> que;
    capo::stopwatch<> sw { true };
    int cnt = (loop_count / PushN);

    std::thread push_trds[PushN];
    for (int i = 0; i < PushN; ++i) {
        (push_trds[i] = std::thread {[i, cnt, &que] {
            for (int k = 0; k < rept_count; ++k) {
                int beg = i * cnt;
                for (int n = beg; n < (beg + cnt); ++n) {
                    while (!que.push(n)) {
                        std::this_thread::yield();
                    }
                }
            }
            //std::cout << "end push: " << i << std::endl;
            while (!que.push(-1)) {
                std::this_thread::yield();
            }
        }}).detach();
    }

    std::uint64_t sum[PopN] {};
    std::atomic<int> push_end { 0 };
    std::thread pop_trds[PopN];
    for (int i = 0; i < PopN; ++i) {
        pop_trds[i] = std::thread {[i, &que, &sum, &push_end] {
            decltype(que.pop()) tp;
            while (push_end.load(std::memory_order_acquire) < PushN) {
                while (std::get<1>(tp = que.pop())) {
                    if (std::get<0>(tp) < 0) {
                        int pop_count = push_end.fetch_add(1, std::memory_order_release) + 1;
                        //std::cout << "pop count: " << pop_count << std::endl;
                        if (pop_count >= PushN) {
                            que.quit();
                            return;
                        }
                    }
                    else sum[i] += std::get<0>(tp);
                }
                std::this_thread::yield();
            }
        }};
    }

    std::uint64_t ret = 0;
    for (int i = 0; i < PopN; ++i) {
        pop_trds[i].join();
        ret += sum[i];
    }
    if ((calc(loop_count) * rept_count) != ret) {
        std::cout << "fail... " << ret << std::endl;
    }

    auto t = sw.elapsed<std::chrono::milliseconds>();
    std::cout << type_name<decltype(que)>() << " "
              << PushN << ":" << PopN << " - " << t << "ms\t" << std::endl;
}

template <int PushN, int PopN,
          template <typename> class Q1,
          template <typename> class Q2,
          template <typename> class... Qs>
void benchmark() {
    benchmark<PushN, PopN, Q1>();
    benchmark<PushN, PopN, Q2, Qs...>();
}

template <typename F, std::size_t...I>
constexpr void static_for(std::index_sequence<I...>, F&& f) {
    [[maybe_unused]] auto expand = { (f(std::integral_constant<size_t, I>{}), 0)... };
}

template <int PushN, int PopN, template <typename> class Queue>
void benchmark_batch() {
    static_for(std::make_index_sequence<(std::max)(PushN, PopN)>{}, [](auto index) {
        benchmark<(PushN <= 1 ? 1 : decltype(index)::value + 1),
                  (PopN  <= 1 ? 1 : decltype(index)::value + 1), Queue>();
    });
    std::cout << std::endl;
}

template <int PushN, int PopN,
          template <typename> class Q1,
          template <typename> class Q2,
          template <typename> class... Qs>
void benchmark_batch() {
    benchmark_batch<PushN, PopN, Q1>();
    benchmark_batch<PushN, PopN, Q2, Qs...>();
}

int main() {
    //for (int i = 0; i < 100; ++i) {
    //    std::cout << i << std::endl;

        benchmark<1, 1, lock::queue,
                        cond::queue,
                        mpmc::queue,
                        spsc::queue,
                        mpmc::qlock,
                        mpmc::qring,
                        spmc::qring,
                        spsc::qring>();

        std::cout << std::endl;

        benchmark_batch<1, 8, lock::queue,
                              cond::queue,
                              mpmc::queue,
                              mpmc::qlock,
                              mpmc::qring,
                              spmc::qring>();

        benchmark_batch<8, 1, lock::queue,
                              cond::queue,
                              mpmc::queue,
                              mpmc::qlock,
                              mpmc::qring>();

        benchmark_batch<8, 8, lock::queue,
                              cond::queue,
                              mpmc::queue,
                              mpmc::qlock,
                              mpmc::qring>();
    //}
    return 0;
}
