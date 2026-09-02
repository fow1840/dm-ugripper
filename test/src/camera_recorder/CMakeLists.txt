include(GoogleTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(add_host_only_camera_gtest target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name}
        PRIVATE
            camera_domain
            UGripper::utils
            GTest::gtest_main
    )
    gtest_discover_tests(${target_name}
        PROPERTIES
            LABELS "host-only;unit;camera"
    )
endfunction()

function(add_host_only_camera_xu_gtest target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name}
        PRIVATE
            main_camera_xu_rtc
            GTest::gtest_main
    )
    gtest_discover_tests(${target_name}
        PROPERTIES
            LABELS "host-only;unit;camera"
    )
endfunction()

add_host_only_camera_gtest(test_camera_config
    test_camera_config.cc
)

add_host_only_camera_gtest(test_camera_command_builder
    test_camera_command_builder.cc
)

add_host_only_camera_gtest(test_camera_timing
    test_camera_timing.cc
)

add_host_only_camera_xu_gtest(test_main_camera_xu_rtc
    test_main_camera_xu_rtc.cc
)

add_host_only_camera_gtest(test_stereo_control_json
    test_stereo_control_json.cc
)

add_test(
    NAME test_check_video_windows_rules
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/test_check_video_windows_rules.py
)
set_tests_properties(test_check_video_windows_rules PROPERTIES
    LABELS "host-only;unit;camera"
)
