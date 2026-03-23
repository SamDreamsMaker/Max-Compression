#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "MaxCompression::maxcomp_static" for configuration "Debug"
set_property(TARGET MaxCompression::maxcomp_static APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(MaxCompression::maxcomp_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libmaxcomp.a"
  )

list(APPEND _cmake_import_check_targets MaxCompression::maxcomp_static )
list(APPEND _cmake_import_check_files_for_MaxCompression::maxcomp_static "${_IMPORT_PREFIX}/lib/libmaxcomp.a" )

# Import target "MaxCompression::maxcomp_shared" for configuration "Debug"
set_property(TARGET MaxCompression::maxcomp_shared APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(MaxCompression::maxcomp_shared PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libmaxcomp.so"
  IMPORTED_SONAME_DEBUG "libmaxcomp.so"
  )

list(APPEND _cmake_import_check_targets MaxCompression::maxcomp_shared )
list(APPEND _cmake_import_check_files_for_MaxCompression::maxcomp_shared "${_IMPORT_PREFIX}/lib/libmaxcomp.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
