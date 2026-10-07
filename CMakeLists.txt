cmake_minimum_required(VERSION 3.25)
project(Underheard)

# iPlug2 framework: the in-tree submodule by default, or -DIPLUG2_DIR=/path.
set(IPLUG2_DIR "${CMAKE_SOURCE_DIR}/iPlug2" CACHE PATH "Path to the iPlug2 framework root")
include(${IPLUG2_DIR}/iPlug2.cmake)

find_package(iPlug2 REQUIRED)

add_subdirectory(plugins/Splicer)
add_subdirectory(plugins/RoomBleed)
add_subdirectory(plugins/UnderheardDelay)
add_subdirectory(plugins/UnderheardChorus)
add_subdirectory(plugins/UnderheardReverb)
add_subdirectory(plugins/Section)
