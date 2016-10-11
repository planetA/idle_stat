cmake_minimum_required(VERSION 3.0)

project(idle_stat)

if (CMAKE_VERSION VERSION_LESS "3.1")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set (CMAKE_CXX_FLAGS "--std=gnu++11 ${CMAKE_CXX_FLAGS}")
  endif ()
else ()
  set (CMAKE_CXX_STANDARD 11)
endif ()

set(idle_stat_SOURCES
  idle_stat.cpp)


add_executable(idle_stat ${idle_stat_SOURCES})

set_property(TARGET idle_stat PROPERTY CXX_STANDARD_REQUIRED 11)
