include(GoogleTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(test_fays_sdk_warning_gate test_fays_sdk_warning_gate.cc)
target_include_directories(test_fays_sdk_warning_gate PRIVATE
    ${PROJECT_SOURCE_DIR}/standalone/FaysStereoRecorder/include
)
target_link_libraries(test_fays_sdk_warning_gate PRIVATE GTest::gtest_main)
gtest_discover_tests(test_fays_sdk_warning_gate PROPERTIES LABELS "host-only;unit;camera")

add_executable(test_fays_gray_nv12 test_fays_gray_nv12.cc)
target_include_directories(test_fays_gray_nv12 PRIVATE
    ${PROJECT_SOURCE_DIR}/standalone/FaysStereoRecorder/include
)
target_link_libraries(test_fays_gray_nv12 PRIVATE GTest::gtest_main)
gtest_discover_tests(test_fays_gray_nv12 PROPERTIES LABELS "host-only;unit;camera")

add_executable(test_fays_video_frame_queue test_fays_video_frame_queue.cc)
target_include_directories(test_fays_video_frame_queue PRIVATE
    ${PROJECT_SOURCE_DIR}/standalone/FaysStereoRecorder/include
)
target_link_libraries(test_fays_video_frame_queue PRIVATE GTest::gtest_main)
gtest_discover_tests(test_fays_video_frame_queue PROPERTIES LABELS "host-only;unit;camera")

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
