#include <iostream>
#include <thread>
#include <vector>
#include <cstdint>

#include "queue_locked.h"
#include "queue_s2s.h"

constexpr std::uint64_t calc(int n) {
    std::uint64_t r = n;
    return (r * (r - 1)) / 2;
}

int main() {
    s2s::queue<int> que;

    for (int n = 1; n <= 50; ++n) {
        std::thread {[&que] {
            for (int i = 0; i < 100000; ++i) {
                que.push(i);
            }
            que.push(-1);
        }}.detach();

        std::uint64_t sum = 0;
        std::thread {[&que, &sum] {
            decltype(que.pop()) tp;
            while (1) if (std::get<1>(tp = que.pop())) {
                if (std::get<0>(tp) < 0) return;
                sum += std::get<0>(tp);
            }
        }}.join();

        if (calc(100000) == sum) {
            std::cout << n << ": pass..." << std::endl;
        }
        else {
            std::cout << n << ": fail... " << sum << std::endl;
        }
    }

    std::cout << "done!" << std::endl;
    return 0;
}
