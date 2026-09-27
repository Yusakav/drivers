cmake_minimum_required(VERSION 3.16)
project(usbpd_host_tests C)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Werror)
endif()

enable_testing()
add_executable(test_message_buffer
    common/message_buffer/message_buffer.c
    usbpd_message.c
    tests/test_message_buffer.c)
target_include_directories(test_message_buffer PRIVATE
    inc
    common/message_buffer)
add_test(NAME message_buffer COMMAND test_message_buffer)

add_executable(test_types tests/test_types.c)
target_include_directories(test_types PRIVATE inc)
add_test(NAME types COMMAND test_types)

add_executable(test_frame
    usbpd_frame.c
    tests/test_frame.c)
target_include_directories(test_frame PRIVATE inc)
add_test(NAME frame COMMAND test_frame)

add_executable(test_protocol
    usbpd_protocol.c
    tests/test_protocol.c)
target_include_directories(test_protocol PRIVATE inc common/message_buffer platform/ch32x035)
add_test(NAME protocol COMMAND test_protocol)

add_executable(test_sink
    common/message_buffer/message_buffer.c
    usbpd_message.c
    usbpd_protocol.c
    pd_sink.c
    tests/test_sink.c)
target_include_directories(test_sink PRIVATE
    inc
    common/message_buffer
    platform/ch32x035)
add_test(NAME sink COMMAND test_sink)

add_executable(test_source
    common/message_buffer/message_buffer.c
    usbpd_message.c
    usbpd_protocol.c
    pd_source.c
    tests/test_source.c)
target_include_directories(test_source PRIVATE
    inc
    common/message_buffer
    platform/ch32x035)
add_test(NAME source COMMAND test_source)

add_executable(test_drp
    common/message_buffer/message_buffer.c
    usbpd_message.c
    usbpd_protocol.c
    pd_sink.c
    pd_source.c
    usbpd.c
    tests/test_drp.c)
target_include_directories(test_drp PRIVATE
    inc
    common/message_buffer
    platform/ch32x035)
add_test(NAME drp COMMAND test_drp)
