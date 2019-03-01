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

template <int PushN, int PopN, template <typename> class Queue>
void benchmark(int loop = 100000) {
    Queue<int> que;
    capo::stopwatch<> sw { true };
    int cnt = (loop / PushN);

    std::thread push_trds[PushN];
    for (int i = 0; i < PushN; ++i) {
        (push_trds[i] = std::thread {[i, cnt, &que] {
            for (int k = 0; k < 100; ++k) {
                int beg = i * cnt;
                for (int n = beg; n < (beg + cnt); ++n) {
                    while (!que.push(n)) {
                        std::this_thread::yield();
                    }
                }
            }
            que.push(-1);
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
                        if ((push_end.fetch_add(1, std::memory_order_release) + 1) >= PushN) {
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

//    std::uint64_t ret = 0;
    for (int i = 0; i < PopN; ++i) {
        pop_trds[i].join();
//        ret += sum[i];
    }
//    if ((calc(loop) * 100) != ret) {
//        std::cout << "fail... " << ret << std::endl;
//    }

    auto t = sw.elapsed<std::chrono::milliseconds>();
    std::cout << type_name<decltype(que)>() << " "
              << PushN << ":" << PopN << " - " << t << "ms\t" << std::endl;
}

template <int PushN, int PopN,
          template <typename> class Q1,
          template <typename> class Q2,
          template <typename> class... Qs>
void benchmark(int loop = 100000) {
    benchmark<PushN, PopN, Q1>(loop);
    benchmark<PushN, PopN, Q2, Qs...>(loop);
}

template <typename F, std::size_t...I>
constexpr void static_for(std::index_sequence<I...>, F&& f) {
    auto expand = { (f(std::integral_constant<size_t, I>{}), 0)... };
}

template <int PushN, int PopN, template <typename> class Queue>
void benchmark_batch(int loop = 100000) {
    static_for(std::make_index_sequence<(std::max)(PushN, PopN)>{}, [loop](auto index) {
        benchmark<(PushN <= 1 ? 1 : decltype(index)::value + 1),
                  (PopN  <= 1 ? 1 : decltype(index)::value + 1), Queue>(loop);
    });
    std::cout << std::endl;
}

template <int PushN, int PopN,
          template <typename> class Q1,
          template <typename> class Q2,
          template <typename> class... Qs>
void benchmark_batch(int loop = 100000) {
    benchmark_batch<PushN, PopN, Q1>(loop);
    benchmark_batch<PushN, PopN, Q2, Qs...>(loop);
}

int main() {
    benchmark<1, 1, lock::queue,
                    cond::queue,
                    mpmc::queue,
                    mpmc::qring,
                    mpmc::qspmc,
                    spsc::queue,
                    spsc::qring>();

    std::cout << std::endl;

    benchmark_batch<1, 8, lock::queue,
                          cond::queue,
                          mpmc::queue,
                          mpmc::qring,
                          mpmc::qspmc>();

    benchmark_batch<8, 1, lock::queue,
                          cond::queue,
                          mpmc::queue,
                          mpmc::qring>();

    benchmark_batch<8, 8, lock::queue,
                          cond::queue,
                          mpmc::queue,
                          mpmc::qring>();
    return 0;
}
