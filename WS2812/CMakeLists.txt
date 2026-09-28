cmake_minimum_required(VERSION 3.16)
project(ws2812_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror -Wpedantic)
endif()

enable_testing()

add_executable(test_ws2812
    ws2812.c
    SPI/ws2812_spi.c
    PWM/ws2812_pwm.c
    tests/test_ws2812.c)
target_include_directories(test_ws2812 PRIVATE . SPI PWM)
add_test(NAME ws2812 COMMAND test_ws2812)

add_executable(test_ch582m_phy
    SPI/ws2812_phy_ch582m.c
    tests/test_ch582m_phy.c)
target_include_directories(test_ch582m_phy PRIVATE
    . SPI tests/include)
add_test(NAME ch582m_phy COMMAND test_ch582m_phy)

# Compile the board example against the small host SDK shim to catch API drift.
add_library(ch582m_example_compile OBJECT example/ws2812_app.c)
target_include_directories(ch582m_example_compile PRIVATE
    . SPI example tests/include)
