CXX ?= clang++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion -O2 -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer -MMD -MP
LDFLAGS ?= -fsanitize=address,undefined

SRC_DIRS := src/domain src/commitments src/engine src/adversary src/proof src/proof/constraints src/proof/recursive src/simulator src/marketdata
SRCS := $(wildcard src/domain/*.cpp src/commitments/*.cpp src/engine/*.cpp src/adversary/*.cpp src/proof/*.cpp src/proof/constraints/*.cpp src/proof/recursive/*.cpp src/simulator/*.cpp src/marketdata/*.cpp)
OBJS := $(SRCS:.cpp=.o)

# Benchmarks build without sanitizers: ASan and UBSan inflate timings by a
# large and variable factor, so numbers measured under them would not be
# reproducible or comparable run to run.
BENCH_CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion -O2 -Iinclude -Ibench -MMD -MP
BENCH_SRCS := $(wildcard bench/*.cpp)
BENCH_OBJS := $(BENCH_SRCS:.cpp=.bench.o)
BENCH_CORE_OBJS := $(SRCS:.cpp=.bench.o)

TEST_SRCS := $(wildcard tests/*.cpp)
TEST_OBJS := $(TEST_SRCS:.cpp=.o)

ALL_OBJS := $(OBJS) $(TEST_OBJS) src/cli/main.o $(BENCH_OBJS) $(BENCH_CORE_OBJS)
DEPS := $(ALL_OBJS:.o=.d)

all: bin/faircross_cpp bin/faircross_tests bin/faircross_bench

bin:
	mkdir -p bin

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.bench.o: %.cpp
	$(CXX) $(BENCH_CXXFLAGS) -c $< -o $@

bin/faircross_cpp: $(OBJS) src/cli/main.o | bin
	$(CXX) $(LDFLAGS) $^ -o $@

bin/faircross_tests: $(OBJS) $(TEST_OBJS) | bin
	$(CXX) $(LDFLAGS) $^ -o $@

bin/faircross_bench: $(BENCH_CORE_OBJS) $(BENCH_OBJS) | bin
	$(CXX) -O2 $^ -o $@

bench: bin/faircross_bench
	./bin/faircross_bench all

test: bin/faircross_tests
	./bin/faircross_tests

clean:
	rm -rf $(ALL_OBJS) $(DEPS) bin

-include $(DEPS)

.PHONY: all test bench clean
