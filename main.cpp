#include <iostream>
#include <thread>
#include <vector>
#include <typeinfo>
#include <string>
#include <cstdint>

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

template <template <typename> class Queue>
void benchmark(int loop = 100000) {
    Queue<int> que;
    capo::stopwatch<> sw { true };

    for (int n = 1; n <= 100; ++n) {
        std::thread {[loop, &que] {
            for (int i = 0; i < loop; ++i) {
                que.push(i);
            }
            que.push(-1);
        }}.detach();

        std::uint64_t sum = 0;
        std::thread {[&que, &sum] {
            decltype(que.pop()) tp;
            while (1) {
                while (std::get<1>(tp = que.pop())) {
                    if (std::get<0>(tp) < 0) return;
                    sum += std::get<0>(tp);
                }
                std::this_thread::yield();
            }
        }}.join();

        if (calc(loop) != sum) {
            std::cout << n << ": fail... " << sum << std::endl;
        }
    }

    auto t = sw.elapsed<std::chrono::milliseconds>();
    std::cout << "done! " << t << "ms\t- " << type_name<decltype(que)>() << std::endl;
}

int main() {
    benchmark<locked::queue>();
    benchmark<m2m::queue>();
    benchmark<s2s::queue>();
    return 0;
}
