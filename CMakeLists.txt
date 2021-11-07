cmake_minimum_required(VERSION 3.18)
project(stacktrace-from-exception)
add_executable(stacktrace-from-exception stacktrace-from-exception.cpp)
set_target_properties(stacktrace-from-exception
  PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )
if (WIN32)
  target_link_libraries(stacktrace-from-exception PRIVATE dbghelp)
elseif(UNIX)
  find_library(DW dw)
  if (NOT DW STREQUAL DW-NOTFOUND)
    target_compile_definitions(stacktrace-from-exception PRIVATE HAVE_DW)
    target_link_libraries(stacktrace-from-exception PRIVATE ${DW})
  else()
    message(WARNING "libdw not found, install libdw-dev (or similar) for best results")
    target_link_options(stacktrace-from-exception PRIVATE -rdynamic)
  endif()
endif()

find_program(CURL curl)
if (CURL)
  add_custom_target(iso_paper
    COMMAND
      ${CURL} -k https://api.csswg.org/bikeshed/ -F file=@${CMAKE_CURRENT_SOURCE_DIR}/paper.md -F force=1 > P2490R0.html
        BYPRODUCTS
          P2490R0.html
        COMMENT
          "Generating ISO Paper P2490 revision 0"
  )
endif ()
