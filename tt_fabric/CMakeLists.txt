#set(TT_FABRIC_OBJECTS
#    ${CMAKE_CURRENT_SOURCE_DIR}/control_plane.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/routing_table_generator.cpp
#    ${CMAKE_CURRENT_SOURCE_DIR}/mesh_graph.cpp
#)

#add_library(tt_fabric ${TT_FABRIC_OBJECTS})

add_library(tt_fabric)
add_library(Metalium::Fabric ALIAS tt_fabric)

target_sources(
    tt_fabric
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/control_plane.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/routing_table_generator.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mesh_graph.cpp
)

target_link_libraries(
    tt_fabric
    PRIVATE
        Metalium::Metal
        umd::device
        metal_common_libs
        magic_enum
        fmt::fmt-header-only
        yaml-cpp::yaml-cpp
)

target_precompile_headers(
    tt_fabric
    PRIVATE
        <functional>
        <map>
        <memory>
        <unordered_map>
        <variant>
        <vector>
)

target_include_directories(
    tt_fabric
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${UMD_HOME}
        ${PROJECT_SOURCE_DIR}/tt_metal
)

target_compile_options(tt_fabric PRIVATE -Wno-int-to-pointer-cast)

set_target_properties(
    tt_fabric
    PROPERTIES
        INSTALL_RPATH
            "${PROJECT_BINARY_DIR}/lib"
        ADDITIONAL_CLEAN_FILES
            "${PROJECT_BINARY_DIR}/lib;${PROJECT_BINARY_DIR}/obj"
)
