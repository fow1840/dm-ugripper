cmake_minimum_required(VERSION 3.10)

find_package(GTest REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME test_logging_v3_python
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/test_logging_v3.py
)
set_tests_properties(test_logging_v3_python PROPERTIES LABELS "host-only;unit;logging")

add_test(
    NAME check_logging_sources_v3
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/check_logging_sources_v3.py
)
set_tests_properties(check_logging_sources_v3 PROPERTIES LABELS "host-only;static;logging")

add_subdirectory(src)
