cmake_minimum_required(VERSION 3.10)

# Make sure our local CMake Modules path comes first
list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_SOURCE_DIR}/cmake/Modules)

#
# Set default build type
#
# See:
#   https://cliutils.gitlab.io/modern-cmake/chapters/features.html
set(default_build_type "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_CXX_FLAGS_INIT "-Wall -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g3")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/extern/Catch2/contrib")

include(GitVersion)

project(dragonradio
  VERSION ${GIT_CMAKE_VERSION}
  LANGUAGES C CXX)

find_package(Cap REQUIRED)
find_package(Firpm REQUIRED)
find_package(FLAC REQUIRED)
find_package(LiquidDSP REQUIRED)
find_package(FFTW3 REQUIRED)
find_package(UHD REQUIRED)
find_package(HDF5 REQUIRED COMPONENTS CXX)
find_package(Python3 EXACT 3.8 REQUIRED COMPONENTS Interpreter Development)
find_package(Protobuf)

add_subdirectory(extern/Catch2)
add_subdirectory(extern/pybind11)
add_subdirectory(extern/rapidcheck)
add_subdirectory(extern/xsimd)

add_subdirectory(src)

add_executable(dragonradio
  src/main.cc
  src/python/Python.cc
)
target_link_libraries(dragonradio PRIVATE DragonRadio)

add_custom_target(python ALL
                  DEPENDS
                  ${CMAKE_CURRENT_SOURCE_DIR}/python/dragonradio/sc2/cil_pb2.py
                  ${CMAKE_CURRENT_SOURCE_DIR}/python/dragonradio/sc2/registration_pb2.py
                  ${CMAKE_CURRENT_SOURCE_DIR}/python/dragonradio/dragonradio/internal_pb2.py
                  ${CMAKE_CURRENT_SOURCE_DIR}/python/dragonradio/dragonradio/remote_pb2.py)

function(generate_python_proto OUTPUT PROTO)
  get_filename_component(OUTPUTDIR ${OUTPUT} DIRECTORY)

  add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${OUTPUT}
                     COMMAND ${Protobuf_PROTOC_EXECUTABLE} --python_out=${CMAKE_CURRENT_SOURCE_DIR}/${OUTPUTDIR} -I${CMAKE_CURRENT_SOURCE_DIR}/proto ${PROTO}
                     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/${PROTO}
                     COMMENT "Generating protobuf file ${OUTPUT}")
endfunction()

generate_python_proto(python/dragonradio/sc2/cil_pb2.py cil.proto)
generate_python_proto(python/dragonradio/sc2/registration_pb2.py registration.proto)
generate_python_proto(python/dragonradio/dragonradio/internal_pb2.py internal.proto)
generate_python_proto(python/dragonradio/dragonradio/remote_pb2.py remote.proto)

add_custom_target(requirements.txt
                  DEPENDS
                  ${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/venv/bin/pip freeze --exclude dragonradio >${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt
  COMMENT "Generating requirements.txt")

add_executable(flexframedemod EXCLUDE_FROM_ALL util/flexframedemod.cc)
target_include_directories(flexframedemod PRIVATE ${LIQUIDDSP_INCLUDE_DIRS})
target_link_libraries(flexframedemod PRIVATE ${LIQUIDDSP_LIBRARIES})

enable_testing()

add_subdirectory(test)

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND}
                  DEPENDS tests)
