# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\tds_plus_qt_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\tds_plus_qt_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\tds_plus_ui_test_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\tds_plus_ui_test_autogen.dir\\ParseCache.txt"
  "tds_plus_qt_autogen"
  "tds_plus_ui_test_autogen"
  )
endif()
