set(UNIT_TESTS_DISPATCH_SETTINGS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/test_cq_device_settings.cpp)

add_library(unit_tests_dispatch_settings_o STATIC ${UNIT_TESTS_DISPATCH_SETTINGS_SRC})

target_link_libraries(unit_tests_dispatch_settings_o PRIVATE test_metal_common_libs)

target_include_directories(
    unit_tests_dispatch_settings_o
    PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/tt_metal
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/tests/tt_metal/tt_metal/common
        ${PROJECT_SOURCE_DIR}/tests/tt_metal/tt_metal/dispatch
        ${PROJECT_SOURCE_DIR}/tests/tt_metal/tt_metal/dispatch/common
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
)

add_executable(unit_tests_dispatch_settings $<TARGET_OBJECTS:unit_tests_dispatch_settings_o>)

target_link_libraries(unit_tests_dispatch_settings PRIVATE test_metal_common_libs)

set_target_properties(
    unit_tests_dispatch_settings
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${PROJECT_BINARY_DIR}/test/tt_metal
)

TT_ENABLE_UNITY_BUILD(unit_tests_dispatch_settings)
