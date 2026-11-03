add_library(test_fixture STATIC test_fixture.c)
target_link_libraries(test_fixture PUBLIC viper_scpi)
target_include_directories(test_fixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_fixture PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

foreach(name test_parser test_params test_ieee488 test_error test_extras test_patterns test_units test_message test_overlapped test_status test_stream test_options test_fuzz)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE test_fixture)
    set_target_properties(${name} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
        if(VIPER_SCPI_WERROR)
            target_compile_options(${name} PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME ${name} COMMAND ${name})
endforeach()

# The largest line buffer: the library is compiled again with it.
add_executable(test_bigbuf test_bigbuf.c test_fixture.c
    ${PROJECT_SOURCE_DIR}/src/scpi_parser.c ${PROJECT_SOURCE_DIR}/src/scpi_ieee488.c
    ${PROJECT_SOURCE_DIR}/src/scpi_params.c ${PROJECT_SOURCE_DIR}/src/scpi_error.c
    ${PROJECT_SOURCE_DIR}/src/scpi_status.c ${PROJECT_SOURCE_DIR}/src/scpi_units.c
    ${PROJECT_SOURCE_DIR}/src/scpi_float.c)
target_compile_definitions(test_bigbuf PRIVATE VIPER_SCPI_BUFFER_SIZE=65535)
target_include_directories(test_bigbuf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
set_target_properties(test_bigbuf PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_bigbuf PRIVATE -Wall -Wextra -Wpedantic)
    if(VIPER_SCPI_WERROR)
        target_compile_options(test_bigbuf PRIVATE -Werror)
    endif()
endif()
add_test(NAME test_bigbuf COMMAND test_bigbuf)

if(VIPER_SCPI_BUILD_EXAMPLES)
    add_test(NAME bare_metal_demo COMMAND bare_metal_demo)
endif()
