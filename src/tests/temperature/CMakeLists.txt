#Set the package name
SET(PACKAGE_NAME "xolotl.tests.temperature")

#Set the description
SET(PACKAGE_DESCRIPTION "Xolotl Temperature tests")

#include directories from the source and boost binaries
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/xolotlCore)
include_directories(${CMAKE_SOURCE_DIR}/xolotlCore/reactants)
include_directories(${CMAKE_SOURCE_DIR}/xolotlCore/reactants/psiclusters)
include_directories(${CMAKE_SOURCE_DIR}/xolotlCore/temperature)
include_directories(${Boost_BINARY_DIRS})

#link the directories
link_directories(${CMAKE_SOURCE_DIR}/src/xolotlCore/temperature)

#Get the test files
file(GLOB tests *.cpp)

#If boost was found, create tests
if(Boost_FOUND)
    #Make executables and link libraries for testers
    foreach(test ${tests})
        message(STATUS "Making test ${test}")
        get_filename_component(testName ${test} NAME_WE)
        add_executable(${testName} ${test})
        target_link_libraries(${testName} xolotlReactants)
        add_test(${testName} ${testName}) 
        #add a label so the tests can be run separately
        set_property(TEST ${testName} PROPERTY LABELS ${PACKAGE_NAME})   
    endforeach(test ${tests})
endif(Boost_FOUND)