cmake_minimum_required(VERSION 3.10)
project(viper_scpi VERSION 0.1.0 LANGUAGES C)

option(VIPER_SCPI_BUILD_TESTS "Build the unit tests" ON)
option(VIPER_SCPI_BUILD_EXAMPLES "Build the examples" ON)
option(VIPER_SCPI_WERROR "Treat compiler warnings as errors" OFF)

add_library(viper_scpi STATIC
    src/scpi_parser.c
    src/scpi_ieee488.c
    src/scpi_params.c
    src/scpi_error.c)
target_include_directories(viper_scpi PUBLIC include PRIVATE src)
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
