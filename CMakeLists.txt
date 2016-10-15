cmake_minimum_required(VERSION 3.0)

project(idle_stat)

if (CMAKE_VERSION VERSION_LESS "3.1")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set (CMAKE_CXX_FLAGS "--std=gnu++11 ${CMAKE_CXX_FLAGS}")
  endif ()
else ()
  set (CMAKE_CXX_STANDARD 11)
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")

set(Boost_USE_STATIC_LIBS   ON)
find_package(Boost 1.54.0
  COMPONENTS system thread program_options
  REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})


set(idle_stat_SOURCES
  env.cpp
  idle_stat.cpp
  measurement.cpp
  shmem.cpp)

add_executable(idle_stat ${idle_stat_SOURCES})

target_link_libraries(idle_stat ${Boost_LIBRARIES} rt)
target_link_libraries(idle_stat -static-libgcc -static-libstdc++)

set_property(TARGET idle_stat PROPERTY CXX_STANDARD_REQUIRED 11)
