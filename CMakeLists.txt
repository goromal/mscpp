cmake_minimum_required (VERSION 3.16)

set(PROJ_NAME mscpp)
project(${PROJ_NAME})

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_TESTS "Build Tests" ON)
option(SECONDARY_SANITIZERS "Run Secondary Sanitizers" OFF)

# message("Your C++ compiler supports these C++ features:")
# foreach(i ${CMAKE_CXX_COMPILE_FEATURES})
#   message("${i}")
# endforeach()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
                             -pthread")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (SECONDARY_SANITIZERS)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
  else()
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
  endif()
  if(MSVC)
    add_compile_options(/W4 /WX)
  else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
  endif()
endif()

add_library(${PROJ_NAME} INTERFACE)
target_include_directories(${PROJ_NAME} INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if (BUILD_TESTS)
    set(UNIT_TEST unit-tests)
    add_executable(${UNIT_TEST}
        tests/example-services/Inputs.cpp
        tests/example-services/ServiceA.cpp
        tests/example-services/ServiceB.cpp
        tests/MainTest.cpp
        tests/ServiceTest.cpp
    )
    target_include_directories(${UNIT_TEST} PRIVATE
        tests/example-services
    )
    target_link_libraries(${UNIT_TEST}
        ${PROJ_NAME}
        boost_unit_test_framework
    )
    add_test(NAME ${UNIT_TEST} COMMAND ${UNIT_TEST})

    add_custom_command(
        TARGET ${UNIT_TEST}
        COMMENT "Run unit tests"
        POST_BUILD
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMAND ${UNIT_TEST}
    )
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/${PROJ_NAME}ConfigVersion.cmake"
    VERSION 0.0
    COMPATIBILITY AnyNewerVersion
)

install(TARGETS ${PROJ_NAME}
    EXPORT ${PROJ_NAME}Targets
    LIBRARY DESTINATION lib COMPONENT Runtime
    ARCHIVE DESTINATION lib COMPONENT Development
    RUNTIME DESTINATION bin COMPONENT Runtime
    PUBLIC_HEADER DESTINATION include COMPONENT Development
    BUNDLE DESTINATION bin COMPONENT Runtime
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/${PROJ_NAME}Config.cmake.in"
    "${PROJECT_BINARY_DIR}/${PROJ_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJ_NAME}
)

install(EXPORT ${PROJ_NAME}Targets DESTINATION lib/cmake/${PROJ_NAME})
install(FILES "${PROJECT_BINARY_DIR}/${PROJ_NAME}ConfigVersion.cmake"
              "${PROJECT_BINARY_DIR}/${PROJ_NAME}Config.cmake"
        DESTINATION lib/cmake/${PROJ_NAME})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include)
