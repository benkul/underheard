# Ad-hoc code signing for macOS bundles built outside Xcode.
#
# With the Ninja generator only the Mach-O binary gets a linker signature, so
# the bundle's Info.plist and resources aren't sealed. auval doesn't mind, but
# sandboxed/out-of-process hosts (GarageBand, Logic, AUHostingService) silently
# reject such a bundle. Signing the whole bundle ad-hoc fixes that for local use.
#
#   underheard_adhoc_sign(<target> <bundle_path> [<bundle_path>...])
#
# Call after iplug_add_plugin() so this runs after iPlug2's deploy (copy) step:
# POST_BUILD commands on a target run in the order they were added.

function(underheard_adhoc_sign target)
  if(NOT APPLE OR NOT TARGET ${target})
    return()
  endif()
  foreach(bundle IN LISTS ARGN)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND /bin/sh -c "if [ -e \"${bundle}\" ]; then /usr/bin/codesign --force --sign - --timestamp=none \"${bundle}\"; fi"
      COMMENT "Ad-hoc signing ${bundle}"
      VERBATIM
    )
  endforeach()
endfunction()
