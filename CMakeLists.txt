cmake_minimum_required(VERSION 3.10)
project(viper_scpi VERSION 0.6.2 LANGUAGES C)

# Keep the header's version macros in step with the project version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/viper_scpi/viper_scpi.h" _viper_ver
     REGEX "^#define VIPER_SCPI_VERSION_STRING ")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" _viper_ver "${_viper_ver}")
if(NOT _viper_ver STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "viper_scpi.h says ${_viper_ver}, CMakeLists.txt says ${PROJECT_VERSION}")
endif()

# Tests and examples are built only when viper-scpi is the top-level project,
# so add_subdirectory() users do not have to switch them off.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    set(_viper_top ${PROJECT_IS_TOP_LEVEL})
elseif(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_viper_top ON)
else()
    set(_viper_top OFF)
endif()

option(VIPER_SCPI_BUILD_TESTS "Build the unit tests" ${_viper_top})
option(VIPER_SCPI_BUILD_EXAMPLES "Build the examples" ${_viper_top})
option(VIPER_SCPI_WERROR "Treat compiler warnings as errors" OFF)
option(VIPER_SCPI_INSTALL "Generate install and export rules" ${_viper_top})

add_library(viper_scpi STATIC
    src/scpi_parser.c
    src/scpi_ieee488.c
    src/scpi_params.c
    src/scpi_error.c
    src/scpi_status.c
    src/scpi_units.c
    src/scpi_float.c)
add_library(viper_scpi::viper_scpi ALIAS viper_scpi)
target_include_directories(viper_scpi
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE src)
set_target_properties(viper_scpi PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(viper_scpi PRIVATE -Wall -Wextra -Wpedantic)
    if(VIPER_SCPI_WERROR)
        target_compile_options(viper_scpi PRIVATE -Werror)
    endif()
endif()

if(VIPER_SCPI_BUILD_EXAMPLES)
    add_executable(bare_metal_demo examples/bare_metal_demo.c)
    target_link_libraries(bare_metal_demo PRIVATE viper_scpi)
    set_target_properties(bare_metal_demo PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endif()

if(VIPER_SCPI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VIPER_SCPI_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    install(TARGETS viper_scpi EXPORT viper_scpi_targets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/viper_scpi DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT viper_scpi_targets NAMESPACE viper_scpi::
            FILE viper_scpiTargets.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/viper_scpi)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/viper_scpiConfig.cmake
         "include(\"\${CMAKE_CURRENT_LIST_DIR}/viper_scpiTargets.cmake\")\n")
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/viper_scpiConfigVersion.cmake
                                     COMPATIBILITY SameMinorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/viper_scpiConfig.cmake
                  ${CMAKE_CURRENT_BINARY_DIR}/viper_scpiConfigVersion.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/viper_scpi)
endif()
