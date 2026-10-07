cmake_minimum_required(VERSION 3.25)
project(RoomBleed VERSION 0.1.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")
set(WDL "${IPLUG2_DIR}/WDL")

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    RoomBleed.cpp
    RoomBleed.h
    ${LIBS}/ui/RoomView.h
    ${LIBS}/fx/Warmth.h
    ${LIBS}/room/Convolver.cpp
    ${LIBS}/room/Convolver.h
    ${LIBS}/room/ImpulsePrep.h
    ${LIBS}/room/MicModel.cpp
    ${LIBS}/room/MicModel.h
    ${LIBS}/room/Occlusion.cpp
    ${LIBS}/room/Occlusion.h
    ${LIBS}/room/RoomModel.cpp
    ${LIBS}/room/RoomTone.cpp
    ${LIBS}/room/RoomTone.h
    ${LIBS}/room/RoomModel.h
    ${LIBS}/tape-core/AudioFile.cpp
    ${LIBS}/tape-core/AudioFile.h
    ${LIBS}/tape-core/TapeFiles.cpp
    ${LIBS}/tape-core/TapeFiles.h
    ${WDL}/convoengine.cpp
    ${WDL}/fft.c
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS
    APP VST3 AU
)

foreach(_target ${PROJECT_NAME}-app ${PROJECT_NAME}-vst3 ${PROJECT_NAME}-au)
  if(TARGET ${_target})
    target_include_directories(${_target} PRIVATE ${LIBS}/room ${LIBS}/tape-core ${LIBS}/ui ${LIBS}/fx ${WDL})
  endif()
endforeach()

# Seal each bundle's signature (built copy, then the deployed copy hosts load).
# Without this GarageBand silently hides the AU even though auval passes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UnderheardSign.cmake)
set(_out "${CMAKE_BINARY_DIR}/out")
underheard_adhoc_sign(${PROJECT_NAME}-au   "${_out}/${PROJECT_NAME}.component" "$ENV{HOME}/Library/Audio/Plug-Ins/Components/${PROJECT_NAME}.component")
underheard_adhoc_sign(${PROJECT_NAME}-vst3 "${_out}/${PROJECT_NAME}.vst3"      "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/${PROJECT_NAME}.vst3")
underheard_adhoc_sign(${PROJECT_NAME}-app  "${_out}/${PROJECT_NAME}.app"       "$ENV{HOME}/Applications/${PROJECT_NAME}.app")
