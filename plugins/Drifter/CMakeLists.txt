cmake_minimum_required(VERSION 3.25)
project(Drifter VERSION 0.1.0)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")
set(SDK "${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK")

# Hosting VST3s (libs/vst3host): the VST3 SDK's hosting code, beyond what iPlug2's VST3 build
# already compiles.
set(HOSTING_SOURCES
  ${SDK}/pluginterfaces/base/conststringtable.cpp
  ${SDK}/base/source/fbuffer.cpp
  ${SDK}/public.sdk/source/common/memorystream.cpp
  ${SDK}/public.sdk/source/common/commonstringconvert.cpp
  ${SDK}/public.sdk/source/vst/utility/stringconvert.cpp
  ${SDK}/public.sdk/source/vst/hosting/module.cpp
  ${SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
  ${SDK}/public.sdk/source/vst/hosting/plugprovider.cpp
  ${SDK}/public.sdk/source/vst/hosting/connectionproxy.cpp
  ${SDK}/public.sdk/source/vst/hosting/eventlist.cpp
  ${SDK}/public.sdk/source/vst/hosting/parameterchanges.cpp
  ${SDK}/public.sdk/source/vst/hosting/processdata.cpp
  ${SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
)
if(APPLE)
  set(HOSTING_PLATFORM ${SDK}/public.sdk/source/vst/hosting/module_mac.mm ${SDK}/public.sdk/source/common/threadchecker_mac.mm
    EditorContainer_mac.mm)
  set_source_files_properties(${HOSTING_PLATFORM} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif(WIN32)
  set(HOSTING_PLATFORM ${SDK}/public.sdk/source/vst/hosting/module_win32.cpp ${SDK}/public.sdk/source/common/threadchecker_win32.cpp
    EditorContainer_win.cpp)
endif()

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    Drifter.cpp
    Drifter.h
    EditorContainer.h
    ${LIBS}/drifter-core/Drifter.h
    ${LIBS}/drifter-core/DriftLanes.h
    ${LIBS}/vst3host/HostedInstrument.cpp
    ${LIBS}/vst3host/HostedInstrument.h
    ${LIBS}/ui/UnderheardUI.h
    ${HOSTING_SOURCES}
    ${HOSTING_PLATFORM}
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS
    VST3
)

if(TARGET ${PROJECT_NAME}-vst3)
  target_include_directories(${PROJECT_NAME}-vst3 PRIVATE ${LIBS}/vst3host ${LIBS}/drifter-core ${LIBS}/ui ${SDK})
  if(APPLE)
    target_link_libraries(${PROJECT_NAME}-vst3 PRIVATE "-framework Foundation" "-framework Cocoa")
  endif()
endif()

# Seal the bundle's signature (built copy, then the deployed copy hosts load).
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UnderheardSign.cmake)
set(_out "${CMAKE_BINARY_DIR}/out")
underheard_adhoc_sign(${PROJECT_NAME}-vst3 "${_out}/${PROJECT_NAME}.vst3" "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/${PROJECT_NAME}.vst3")
