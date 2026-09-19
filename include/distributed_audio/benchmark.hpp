#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace distributed_audio::benchmark {

struct Result {
    std::string operation;
    std::size_t iterations{0};
    double total_milliseconds{0.0};
    double average_microseconds{0.0};
    double throughput_per_second{0.0};
};

[[nodiscard]] std::vector<Result> run(std::size_t iterations = 10'000);
void print(const std::vector<Result>& results);

}  // namespace distributed_audio::benchmark
