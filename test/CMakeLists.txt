# Function remade from GTEST_ADD_TESTS
# https://github.com/Kitware/CMake/blob/master/Modules/FindGTest.cmake
function(GTEST_ADD_MPI_TESTS mpiexec mpi_params executable extra_args)
  # obtain sources used for building that executable
  get_property(ARGN TARGET ${executable} PROPERTY SOURCES)

  set(gtest_case_name_regex ".*\\( *([A-Za-z_0-9]+) *, *([A-Za-z_0-9]+) *\\).*")
  set(gtest_test_type_regex "(TYPED_TEST|TEST_?[FP]?)")
  foreach(source ${ARGN})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
    file(READ "${source}" contents)
    string(REGEX MATCHALL "${gtest_test_type_regex} *\\(([A-Za-z_0-9 ,]+)\\)" found_tests ${contents})
    foreach(hit ${found_tests})
      string(REGEX MATCH "${gtest_test_type_regex}" test_type ${hit})

      # Parameterized tests have a different signature for the filter
      if("x${test_type}" STREQUAL "xTEST_P")
        string(REGEX REPLACE ${gtest_case_name_regex}  "*/\\1.\\2/*" test_name ${hit})
      elseif("x${test_type}" STREQUAL "xTEST_F" OR "x${test_type}" STREQUAL "xTEST")
        string(REGEX REPLACE ${gtest_case_name_regex} "\\1.\\2" test_name ${hit})
      elseif("x${test_type}" STREQUAL "xTYPED_TEST")
        string(REGEX REPLACE ${gtest_case_name_regex} "\\1/*.\\2" test_name ${hit})
      else()
        message(WARNING "Could not parse GTest ${hit} for adding to CTest.")
        continue()
      endif()
      add_test(NAME ${test_name} COMMAND ${mpiexec} ${mpi_params} --gtest_filter=${test_name})
    endforeach()
  endforeach()
endfunction()

function(add_mpi_test name no_mpi_proc)
  # My test are all called name_test.cpp
  add_executable(${name} ${name}_test.cpp)
  target_compile_features(${name}
    PRIVATE
      cxx_nullptr
    )
  target_link_libraries(${name} GTest::GTest GTest::Main lining)
  target_link_libraries(${name} ${MPI_LIBRARIES})
  # Make sure to link MPI here too:
  set(test_parameters ${MPIEXEC_NUMPROC_FLAG} ${no_mpi_proc} "./${name}")

  if(MPI_COMPILE_FLAGS)
    set_target_properties(${name} PROPERTIES
      COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
  endif()
  if(MPI_LINK_FLAGS)
    set_target_properties(${name} PROPERTIES
      LINK_FLAGS "${MPI_LINK_FLAGS}")
  endif()
  # add_test(NAME ${name} COMMAND ${MPIEXEC} ${test_parameters})
  GTEST_ADD_MPI_TESTS(${MPIEXEC} "${test_parameters}" ${name} "")
endfunction(add_mpi_test)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

add_executable(cpu_list_test
  cpu_list_test.cpp
  ${CMAKE_SOURCE_DIR}/env.cpp)
target_link_libraries(cpu_list_test ${GTEST_BOTH_LIBRARIES})
target_link_libraries(cpu_list_test ${Boost_LIBRARIES} rt)
GTEST_ADD_TESTS(cpu_list_test ""
  cpu_list_test.cpp
  ${CMAKE_SOURCE_DIR}/env.cpp)
#add_test(NAME cpu_list_test COMMAND cpu_list_test)
# set(test_np "4"  CACHE STRING "Parallelism level for testing.")

# add_mpi_test(init ${test_np})
# add_mpi_test(resource ${test_np})
# add_mpi_test(c_interface ${test_np})
# add_mpi_test(multiple ${test_np})
# add_mpi_test(submit ${test_np})
# add_mpi_test(new_color ${test_np})

# math(EXPR many_np "${test_np}*4")
# add_mpi_test(randomized ${many_np})
