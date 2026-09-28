cmake_minimum_required(VERSION 3.28.3)
project(lab1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_executable(bench
    src/BaselineCollector.cpp
    src/MutexCollector.cpp
    src/Benchmark.cpp
)
target_link_libraries(bench PRIVATE Threads::Threads)
target_compile_options(bench PRIVATE -Wall -Wextra -Wpedantic -O2)

add_executable(bench_tsan
    src/BaselineCollector.cpp
    src/MutexCollector.cpp
    src/Benchmark.cpp
)
target_link_libraries(bench_tsan PRIVATE Threads::Threads)
target_compile_options(bench_tsan PRIVATE -Wall -Wextra -Wpedantic -fsanitize=thread -g -O1)
target_link_options(bench_tsan PRIVATE -fsanitize=thread)

# Основная сборка для всех замеров (с оптимизацией):
# g++ -O2 -std=c++20 -pthread src/*.cpp -o bench

# Проверка на гонки данных через ThreadSanitizer (TSan):
# ВНИМАНИЕ: работает в 5-15 раз медленнее! Замеры скорости здесь делать нельзя!
# clang++ -fsanitize=thread -g -O1 -std=c++20 -pthread src/*.cpp -o bench_tsan
