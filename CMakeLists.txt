cmake_minimum_required(VERSION 3.10)
project(hydrods)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimize for cache architecture & modern vectorization
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native -mtune=native -DNDEBUG")

find_package(OpenMP REQUIRED)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX Flags:  ${CMAKE_CXX_FLAGS}")

# Core include paths
set(HYDRO_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hydrods_core
    ${CMAKE_CURRENT_SOURCE_DIR}/baselines
    ${CMAKE_CURRENT_SOURCE_DIR}/baselines/tlx
    ${CMAKE_CURRENT_SOURCE_DIR}/baselines/alex
    ${CMAKE_CURRENT_SOURCE_DIR}/baselines/pgm/include
)

# 1. Insert Benchmark Target
add_executable(insert_benchmark benchmark/insert/insert_benchmark.cpp)
target_include_directories(insert_benchmark PRIVATE ${HYDRO_INCLUDES})
target_compile_options(insert_benchmark PRIVATE -mavx2 -mfma -O3 -pthread -fopenmp)
target_link_libraries(insert_benchmark PRIVATE pthread OpenMP::OpenMP_CXX)

# 2. Search Benchmark Target
add_executable(search_benchmark benchmark/search/search_benchmark.cpp)
target_include_directories(search_benchmark PRIVATE ${HYDRO_INCLUDES})
target_compile_options(search_benchmark PRIVATE -mavx2 -mfma -O3 -pthread -fopenmp)
target_link_libraries(search_benchmark PRIVATE pthread OpenMP::OpenMP_CXX)

# 3. Delete Benchmark Target
add_executable(delete_benchmark benchmark/delete/delete_benchmark.cpp)
target_include_directories(delete_benchmark PRIVATE ${HYDRO_INCLUDES})
target_compile_options(delete_benchmark PRIVATE -mavx2 -mfma -O3 -pthread -fopenmp)
target_link_libraries(delete_benchmark PRIVATE pthread OpenMP::OpenMP_CXX)

# 4. Range Benchmark Target
add_executable(range_benchmark benchmark/range/range_benchmark.cpp)
target_include_directories(range_benchmark PRIVATE ${HYDRO_INCLUDES})
target_compile_options(range_benchmark PRIVATE -mavx2 -mfma -O3 -pthread -fopenmp)
target_link_libraries(range_benchmark PRIVATE pthread OpenMP::OpenMP_CXX)
