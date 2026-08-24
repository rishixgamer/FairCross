#pragma once

// Shared infrastructure for the FairCross C++ benchmark harness.
//
// Benchmarks are a separate binary from the test suite on purpose: the tests
// build with ASan and UBSan, which inflate timings by a large and variable
// factor. Numbers measured under sanitizers would not be reproducible or
// comparable run to run. `bin/faircross_bench` is built -O2 with no
// sanitizers; `scripts/check.sh` keeps running the sanitized tests.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "faircross/util/json_writer.hpp"

namespace faircross::bench {

/// Monotonic microsecond timer. Steady clock, so a wall-clock adjustment during
/// a run cannot produce a negative or absurd duration.
class Timer {
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}

    [[nodiscard]] uint64_t elapsed_us() const {
        const auto delta = std::chrono::steady_clock::now() - start_;
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(delta).count());
    }

private:
    std::chrono::steady_clock::time_point start_;
};

/// Environment metadata recorded with every artifact, so a number can be traced
/// to the machine and toolchain that produced it.
inline void write_environment(json::Writer& w) {
    w.key("environment");
    w.begin_object();
#if defined(__APPLE__)
    w.field_string("os", "macos");
#elif defined(__linux__)
    w.field_string("os", "linux");
#else
    w.field_string("os", "unknown");
#endif
#if defined(__aarch64__) || defined(__arm64__)
    w.field_string("arch", "aarch64");
#elif defined(__x86_64__)
    w.field_string("arch", "x86_64");
#else
    w.field_string("arch", "unknown");
#endif
#if defined(__clang__)
    w.field_string("compiler", "clang++ " __clang_version__);
#elif defined(__GNUC__)
    w.field_string("compiler", "g++");
#else
    w.field_string("compiler", "unknown");
#endif
    w.field_string("cxx_standard", "C++20");
    w.field_string("optimization", "-O2");
    w.field_bool("sanitizers_enabled", false);
    w.field_string("implementation", "cpp");
    w.end_object();
}

/// Median of a sample set. Used instead of the mean where a single scheduling
/// outlier would otherwise dominate.
inline uint64_t median(std::vector<uint64_t> samples) {
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

inline uint64_t percentile(std::vector<uint64_t> samples, double p) {
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    auto idx = static_cast<size_t>(p * static_cast<double>(samples.size() - 1));
    return samples[idx];
}

inline double mean(const std::vector<uint64_t>& samples) {
    if (samples.empty()) return 0.0;
    const auto total = std::accumulate(samples.begin(), samples.end(), uint64_t{0});
    return static_cast<double>(total) / static_cast<double>(samples.size());
}

/// Writes an artifact under `experiments/results/`.
///
/// Each artifact carries its own environment metadata and reproduction command,
/// so a published measurement stays attributable to the build that produced it
/// rather than to the directory it happens to sit in.
inline void write_artifact(const std::string& filename, const std::string& contents) {
    const std::string dir = "experiments/results";
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/" + filename;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write " + path);
    }
    out << contents;
}

} // namespace faircross::bench
