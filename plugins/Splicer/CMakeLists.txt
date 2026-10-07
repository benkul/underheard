cmake_minimum_required(VERSION 3.25)
project(Splicer VERSION 0.1.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

set(TAPE_CORE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/tape-core")
set(FX "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/fx")
set(DRIFTER "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/drifter-core")

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    Splicer.cpp
    Splicer.h
    ui/TapeView.h
    ${TAPE_CORE}/AudioFile.cpp
    ${TAPE_CORE}/AudioFile.h
    ${TAPE_CORE}/CaptureBuffer.h
    ${TAPE_CORE}/Filters.h
    ${TAPE_CORE}/TapeEdit.cpp
    ${TAPE_CORE}/TapeEdit.h
    ${TAPE_CORE}/TapeFiles.cpp
    ${TAPE_CORE}/TapeFiles.h
    ${TAPE_CORE}/TapeLoop.cpp
    ${TAPE_CORE}/TapeLoop.h
    ${TAPE_CORE}/TapeStorage.h
    ${DRIFTER}/Drifter.h
    ${FX}/AlgoReverb.cpp
    ${FX}/AlgoReverb.h
    ${FX}/DubDelay.cpp
    ${FX}/DubDelay.h
    ${FX}/FxChain.h
    ${FX}/FxCommon.h
    ${FX}/MultiChorus.cpp
    ${FX}/MultiChorus.h
    ${FX}/NoteValues.h
    ${FX}/Warmth.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS
    APP VST3 AU
)

foreach(_target ${PROJECT_NAME}-app ${PROJECT_NAME}-vst3 ${PROJECT_NAME}-au)
  if(TARGET ${_target})
    target_include_directories(${_target} PRIVATE ${TAPE_CORE} ${FX} ${DRIFTER})
  endif()
endforeach()

# Seal each bundle's signature (built copy, then the deployed copy hosts load).
# Without this GarageBand silently hides the AU even though auval passes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UnderheardSign.cmake)
set(_out "${CMAKE_BINARY_DIR}/out")
underheard_adhoc_sign(${PROJECT_NAME}-au   "${_out}/${PROJECT_NAME}.component" "$ENV{HOME}/Library/Audio/Plug-Ins/Components/${PROJECT_NAME}.component")
underheard_adhoc_sign(${PROJECT_NAME}-vst3 "${_out}/${PROJECT_NAME}.vst3"      "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/${PROJECT_NAME}.vst3")
underheard_adhoc_sign(${PROJECT_NAME}-app  "${_out}/${PROJECT_NAME}.app"       "$ENV{HOME}/Applications/${PROJECT_NAME}.app")
