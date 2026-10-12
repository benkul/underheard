# Windows: a plugin's font lives in its own DLL, compiled in from resources/main.rc. iPlug2's
# CMake only adds main.rc to the standalone app, so without this a VST3 can't find its font
# and draws no text.
function(underheard_win_resources target)
  if(NOT WIN32 OR NOT TARGET ${target})
    return()
  endif()
  set(_res "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  target_sources(${target} PRIVATE "${_res}/main.rc")
  # main.rc names its files without a path (same flags as iPlug2's app target).
  set_source_files_properties("${_res}/main.rc" PROPERTIES
    COMPILE_FLAGS "/I\"${_res}/fonts\" /I\"${_res}/img\" /I\"${_res}\""
  )
endfunction()
