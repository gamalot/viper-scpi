# CI check (item 4.2): an installed viper-scpi is found with find_package().
cmake_minimum_required(VERSION 3.10)
project(consumer_check C)

# Ask for the version of this source tree, read from its header, so that the
# check exercises the installed version file without being edited on every
# release (SameMinorVersion: 0.3.x accepts a request for 0.3).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../../include/viper_scpi/viper_scpi.h" _ver
     REGEX "^#define VIPER_SCPI_VERSION_(MAJOR|MINOR) ")
string(REGEX REPLACE ".*MAJOR ([0-9]+).*" "\\1" _major "${_ver}")
string(REGEX REPLACE ".*MINOR ([0-9]+).*" "\\1" _minor "${_ver}")

find_package(viper_scpi ${_major}.${_minor} REQUIRED)
add_executable(app ../embed/main.c)
target_link_libraries(app PRIVATE viper_scpi::viper_scpi)
