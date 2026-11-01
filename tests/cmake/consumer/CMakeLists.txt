# CI check (item 4.2): an installed viper-scpi is found with find_package().
cmake_minimum_required(VERSION 3.10)
project(consumer_check C)
find_package(viper_scpi 0.2 REQUIRED)
add_executable(app ../embed/main.c)
target_link_libraries(app PRIVATE viper_scpi::viper_scpi)
