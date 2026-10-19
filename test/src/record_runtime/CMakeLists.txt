cmake_minimum_required(VERSION 3.10)

include(GoogleTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(test_ego_feedback test_ego_feedback.cc
    ${CMAKE_SOURCE_DIR}/standalone/UgripperRuntime/bms_reader.cc)
target_include_directories(test_ego_feedback PRIVATE ${CMAKE_SOURCE_DIR}/standalone/UgripperRuntime/include)
target_link_libraries(test_ego_feedback PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json Threads::Threads)
gtest_discover_tests(test_ego_feedback PROPERTIES LABELS "host-only;unit;record_runtime")

function(add_host_only_runtime_gtest target_name)
    add_executable(${target_name} ${ARGN})
    target_include_directories(${target_name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/standalone/UgripperRuntime/include
            ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(${target_name}
        PRIVATE
            runtime_control_plane
            runtime_process_boundary
            UGripper::utils
            GTest::gtest_main
    )
    gtest_discover_tests(${target_name}
        PROPERTIES
            LABELS "host-only;unit;record_runtime"
    )
endfunction()

add_host_only_runtime_gtest(test_process_policy
    test_process_policy.cc
)

add_host_only_runtime_gtest(test_audio_coordinator
    test_audio_coordinator.cc
)

add_host_only_runtime_gtest(test_stereo_session_client
    test_stereo_session_client.cc
)

add_host_only_runtime_gtest(test_button_logic
    test_button_logic.cc
)

add_host_only_runtime_gtest(test_button_event_timing
    test_button_event_timing.cc
)

add_host_only_runtime_gtest(test_button_action_router
    test_button_action_router.cc
)

add_host_only_runtime_gtest(test_ego_binding_config
    test_ego_binding_config.cc
)
target_include_directories(test_ego_binding_config
    PRIVATE
        ${CMAKE_SOURCE_DIR}/standalone/GripperHmiTool/include
)

add_host_only_runtime_gtest(test_gripper_refresh_logic
    test_gripper_refresh_logic.cc
)

add_host_only_runtime_gtest(test_gripper_extrinsics
    test_gripper_extrinsics.cc
)

add_host_only_runtime_gtest(test_runtime_health
    test_runtime_health.cc
)

add_host_only_runtime_gtest(test_recording_orchestrator
    test_recording_orchestrator.cc
)

add_host_only_runtime_gtest(test_led_state_bridge
    test_led_state_bridge.cc
)

add_host_only_runtime_gtest(test_error_code_mapping
    test_error_code_mapping.cc
)

add_host_only_runtime_gtest(test_encoder_health
    test_encoder_health.cc
)

add_test(NAME test_sensor_startup_failure
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/test_sensor_startup_failure.py
            $<TARGET_FILE:SensorRecorder>)
set_tests_properties(test_sensor_startup_failure PROPERTIES
    LABELS "host-only;unit;sensor" SKIP_RETURN_CODE 77)

add_host_only_runtime_gtest(test_shutdown_request_port
    test_shutdown_request_port.cc
)

add_host_only_runtime_gtest(test_tactile_residual
    test_tactile_residual.cc
    ${CMAKE_SOURCE_DIR}/standalone/UgripperRuntime/tactile_residual.cc
)

add_test(
    NAME test_check_hmi_event_log_pass
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/check_hmi_event_log.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/hmi_diag_pass.log
            --require-event ShortUpPressed
            --require-led-state Error2
)
set_tests_properties(test_check_hmi_event_log_pass PROPERTIES
    LABELS "host-only;unit;record_runtime"
)

add_test(
    NAME test_check_hmi_event_log_fail
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/check_hmi_event_log.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/hmi_diag_fail.log
)
set_tests_properties(test_check_hmi_event_log_fail PROPERTIES
    LABELS "host-only;unit;record_runtime"
    WILL_FAIL TRUE
)

add_test(
    NAME test_check_hmi_event_log_led_mismatch
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/check_hmi_event_log.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/hmi_diag_led_mismatch.log
)
set_tests_properties(test_check_hmi_event_log_led_mismatch PROPERTIES
    LABELS "host-only;unit;record_runtime"
    WILL_FAIL TRUE
)

add_test(
    NAME test_check_hmi_event_log_health_faults_pass
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/check_hmi_event_log.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/hmi_diag_health_faults_pass.log
            --require-health-fault
            hmi_all_disconnected
            --require-health-fault
            hmi_ports_inactive
            --require-health-fault
            stereo_daemon_not_running
            --require-health-fault-led
            hmi_all_disconnected:Error2
            --require-health-fault-led
            hmi_ports_inactive:Error2
            --require-health-fault-led
            stereo_daemon_not_running:Error2
)
set_tests_properties(test_check_hmi_event_log_health_faults_pass PROPERTIES
    LABELS "host-only;unit;record_runtime"
)

add_test(
    NAME test_check_hmi_event_log_health_faults_fail
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/check_hmi_event_log.py
            --input
            ${CMAKE_CURRENT_SOURCE_DIR}/testdata/hmi_diag_health_faults_fail.log
            --require-health-fault
            hmi_ports_inactive
            --require-health-fault-led
            hmi_ports_inactive:Error2
            --require-health-fault
            stereo_daemon_not_running
)
set_tests_properties(test_check_hmi_event_log_health_faults_fail PROPERTIES
    LABELS "host-only;unit;record_runtime"
    WILL_FAIL TRUE
)

add_test(
    NAME test_summarize_episode_reports
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/test_summarize_episode_reports.py
)
set_tests_properties(test_summarize_episode_reports PROPERTIES
    LABELS "host-only;unit;record_runtime"
)

add_test(
    NAME test_validate_episode_profiles
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/test/scripts/test_validate_episode_profiles.py
)
set_tests_properties(test_validate_episode_profiles PROPERTIES
    LABELS "host-only;unit;record_runtime"
)

add_executable(test_episode_metadata_contract test_episode_metadata_contract.cc)
target_include_directories(test_episode_metadata_contract PRIVATE ${CMAKE_SOURCE_DIR}/standalone/UgripperRuntime/include)
target_link_libraries(test_episode_metadata_contract PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
gtest_discover_tests(test_episode_metadata_contract PROPERTIES LABELS "host-only;unit;record_runtime")

add_test(NAME test_ego_mp4_repair
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/test_ego_mp4_repair.py)
set_tests_properties(test_ego_mp4_repair PROPERTIES LABELS "host-only;unit;record_runtime")
add_test(NAME test_system_log_snapshot
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/test_system_log_snapshot.py)
set_tests_properties(test_system_log_snapshot PROPERTIES LABELS "host-only;unit;record_runtime")

add_test(NAME test_ego_transfer_recovery
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/test_ego_transfer_recovery.py)
set_tests_properties(test_ego_transfer_recovery PROPERTIES LABELS "host-only;unit;record_runtime")

add_host_only_runtime_gtest(test_ego_warm_process test_ego_warm_process.cc)
add_test(NAME test_ego_start_latency_python
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/test_ego_start_latency.py)
set_tests_properties(test_ego_start_latency_python PROPERTIES LABELS "host-only;unit;record_runtime;ego")

add_test(NAME test_ram_logging_config
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/scripts/test_ram_logging_config.py)
set_tests_properties(test_ram_logging_config PROPERTIES LABELS "host-only;unit;record_runtime")
