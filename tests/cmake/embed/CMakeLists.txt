# CI check (item 4.2): viper-scpi pulled in with add_subdirectory() builds
# only the library -- no tests, no examples -- without setting any option.
cmake_minimum_required(VERSION 3.10)
project(embed_check C)
add_subdirectory(../../.. viper_scpi)
foreach(t test_parser bare_metal_demo)
    if(TARGET ${t})
        message(FATAL_ERROR "target ${t} must not exist in an embedding project")
    endif()
endforeach()
add_executable(app main.c)
target_link_libraries(app PRIVATE viper_scpi::viper_scpi)
