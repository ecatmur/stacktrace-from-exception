cmake_minimum_required(VERSION 3.19)
project(stacktrace-from-exception)
add_executable(stacktrace-from-exception stacktrace-from-exception.cpp)
set_target_properties(stacktrace-from-exception
  PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )
if (WIN32)
  target_link_libraries(stacktrace-from-exception PRIVATE dbghelp)
endif()
