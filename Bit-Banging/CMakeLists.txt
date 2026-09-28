cmake_minimum_required(VERSION 3.16)
project(soft_i2c_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow)
endif()

enable_testing()

add_executable(test_soft_i2c
    soft_i2c.c
    soft_smbus.c
    tests/test_soft_i2c.c)
target_include_directories(test_soft_i2c PRIVATE .)
add_test(NAME soft_i2c COMMAND test_soft_i2c)

# Compile real repository adapters to catch API drift without linking platform code.
add_library(ina226_soft_i2c_adapter_compile OBJECT
    ../INA226/example/ina226_app.c)
target_include_directories(ina226_soft_i2c_adapter_compile PRIVATE
    . ../INA226 ../INA226/example)

add_library(bq25710_soft_i2c_adapter_compile OBJECT
    ../BQ25710/example/bq25710_soft_i2c_adapter.c)
target_include_directories(bq25710_soft_i2c_adapter_compile PRIVATE
    . ../BQ25710 ../BQ25710/example ../Battery)

add_executable(test_bq25710_adapter
    ../BQ25710/example/bq25710_soft_i2c_adapter.c
    tests/test_bq25710_adapter.c)
target_include_directories(test_bq25710_adapter PRIVATE
    . ../BQ25710 ../BQ25710/example ../Battery)
add_test(NAME bq25710_adapter COMMAND test_bq25710_adapter)

add_executable(test_ina226_adapter
    ../INA226/example/ina226_app.c
    tests/test_ina226_adapter.c)
target_include_directories(test_ina226_adapter PRIVATE
    . ../INA226 ../INA226/example)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ina226_adapter PRIVATE -ffunction-sections)
    target_link_options(test_ina226_adapter PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME ina226_adapter COMMAND test_ina226_adapter)
