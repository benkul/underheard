cmake_minimum_required(VERSION 3.25)
project(Section VERSION 0.1.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")
set(WDL "${IPLUG2_DIR}/WDL")

# horsi-vst3's horse wavetables, compiled in (credits: assets/wavetables/horsi/CREDITS.md).
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UnderheardEmbed.cmake)
set(HORSI_TABLES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/wavetables/horsi")
underheard_embed_files(${CMAKE_CURRENT_BINARY_DIR}/HorsiTables.cpp underheard::section_assets kHorsiTables FILES
  ${HORSI_TABLES_DIR}/Horsi-Whinny.wav
  ${HORSI_TABLES_DIR}/Horsi-Stallion.wav
  ${HORSI_TABLES_DIR}/Horsi-Call.wav
  ${HORSI_TABLES_DIR}/Horsi-Breath.wav
  ${HORSI_TABLES_DIR}/Horsi-Huff.wav
  ${HORSI_TABLES_DIR}/Horsi-Rasp.wav
  ${HORSI_TABLES_DIR}/Horsi-Whinny-to-Breath.wav
  ${HORSI_TABLES_DIR}/Horsi-Human.wav
)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    Section.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HorsiTables.cpp
    Section.h
    ${LIBS}/section/JustTuning.h
    ${LIBS}/section/SectionEngine.cpp
    ${LIBS}/section/SectionEngine.h
    ${LIBS}/wavetable/FactoryTables.cpp
    ${LIBS}/wavetable/FactoryTables.h
    ${LIBS}/wavetable/WaveOscillator.h
    ${LIBS}/wavetable/WavetableData.cpp
    ${LIBS}/wavetable/WavetableData.h
    ${LIBS}/voicefx/NoiseLayer.h
    ${LIBS}/voicefx/Resonator.cpp
    ${LIBS}/voicefx/Resonator.h
    ${LIBS}/tape-core/TapeFiles.cpp
    ${LIBS}/tape-core/TapeFiles.h
    ${LIBS}/tape-core/AudioFile.cpp
    ${LIBS}/tape-core/AudioFile.h
    ${LIBS}/fx/FxCommon.h
    ${LIBS}/fx/Warmth.h
    ${LIBS}/room/Convolver.cpp
    ${LIBS}/room/Convolver.h
    ${LIBS}/room/MicModel.cpp
    ${LIBS}/room/MicModel.h
    ${LIBS}/room/RoomModel.cpp
    ${LIBS}/room/RoomModel.h
    ${LIBS}/ui/RoomView.h
    ${LIBS}/ui/UnderheardUI.h
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
    target_include_directories(${_target} PRIVATE ${LIBS}/section ${LIBS}/wavetable ${LIBS}/voicefx ${LIBS}/fx ${LIBS}/room ${LIBS}/tape-core ${LIBS}/ui ${WDL})
  endif()
endforeach()

# Seal each bundle's signature (built copy, then the deployed copy hosts load).
# Without this GarageBand silently hides the AU even though auval passes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UnderheardSign.cmake)
set(_out "${CMAKE_BINARY_DIR}/out")
underheard_adhoc_sign(${PROJECT_NAME}-au   "${_out}/${PROJECT_NAME}.component" "$ENV{HOME}/Library/Audio/Plug-Ins/Components/${PROJECT_NAME}.component")
underheard_adhoc_sign(${PROJECT_NAME}-vst3 "${_out}/${PROJECT_NAME}.vst3"      "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/${PROJECT_NAME}.vst3")
underheard_adhoc_sign(${PROJECT_NAME}-app  "${_out}/${PROJECT_NAME}.app"       "$ENV{HOME}/Applications/${PROJECT_NAME}.app")
