#include <iostream>
#include <thread>
#include <vector>
#include <typeinfo>
#include <string>
#include <cstdint>

#include "queue_unsafe.h"
#include "queue_locked.h"
#include "queue_s2s.h"
#include "queue_m2m.h"

#if defined(__GNUC__)
#   include <memory>
#   include <cxxabi.h>  // abi::__cxa_demangle
#endif/*__GNUC__*/

#include "stopwatch.hpp"

constexpr std::uint64_t calc(int n) {
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

template <template <typename> class Queue, int PushN, int PopN>
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
                    que.push(n);
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

    std::uint64_t ret = 0;
    for (int i = 0; i < PopN; ++i) {
        pop_trds[i].join();
        ret += sum[i];
    }
    if ((calc(loop) * 100) != ret) {
        std::cout << "fail... " << ret << std::endl;
    }

    auto t = sw.elapsed<std::chrono::milliseconds>();
    std::cout << PushN << ":" << PopN << " done! "
              << t << "ms\t- " << type_name<decltype(que)>() << std::endl;
}

int main() {
    benchmark<locked::queue, 1, 1>();
    benchmark<cond::queue  , 1, 1>();
    benchmark<m2m::queue   , 1, 1>();
    benchmark<s2s::queue   , 1, 1>();

    std::cout << std::endl;

    benchmark<locked::queue, 4, 1>();
    benchmark<cond::queue  , 4, 1>();
    benchmark<m2m::queue   , 4, 1>();

    std::cout << std::endl;

    benchmark<locked::queue, 1, 4>();
    benchmark<cond::queue  , 1, 4>();
    benchmark<m2m::queue   , 1, 4>();

    std::cout << std::endl;

    benchmark<locked::queue, 4, 4>();
    benchmark<cond::queue  , 4, 4>();
    benchmark<m2m::queue   , 4, 4>();

    std::cout << std::endl;

    benchmark<locked::queue, 8, 8>();
    benchmark<cond::queue  , 8, 8>();
    benchmark<m2m::queue   , 8, 8>();

    std::cout << std::endl;

    benchmark<locked::queue, 16, 16>();
    benchmark<cond::queue  , 16, 16>();
    benchmark<m2m::queue   , 16, 16>();

    std::cout << std::endl;

    benchmark<locked::queue, 32, 32>();
    benchmark<cond::queue  , 32, 32>();
    benchmark<m2m::queue   , 32, 32>();
    return 0;
}
