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

add_subdirectory(extern/pybind11)

configure_file(src/config.h.in src/config.h)

add_executable(dragonradio
  src/Clock.cc
  src/ExtensibleDataSet.cc
  src/IQCompression.cc
  src/IQCompression/FLAC.cc
  src/Logger.cc
  src/logging.cc
  src/main.cc
  src/Math.cc
  src/Packet.cc
  src/RadioNet.cc
  src/TimerQueue.cc
  src/USRP.cc
  src/WorkQueue.cc
  src/cil/Scorer.cc
  src/dsp/FFTW.cc
  src/dsp/FIRDesign.cc
  src/dsp/TableNCO.cc
  src/liquid/Filter.cc
  src/liquid/Modem.cc
  src/liquid/Mutex.cc
  src/liquid/OFDM.cc
  src/liquid/PHY.cc
  src/liquid/Resample.cc
  src/phy/AutoGain.cc
  src/phy/FDChannelModulator.cc
  src/phy/FDChannelizer.cc
  src/phy/MultichannelSynthesizer.cc
  src/phy/OverlapTDChannelizer.cc
  src/phy/PHY.cc
  src/phy/RadioPacketQueue.cc
  src/phy/TDChannelModulator.cc
  src/phy/TDChannelizer.cc
  src/llc/DummyController.cc
  src/llc/SmartController.cc
  src/mac/FDMA.cc
  src/mac/MAC.cc
  src/mac/SlottedALOHA.cc
  src/mac/SlottedMAC.cc
  src/mac/Snapshot.cc
  src/mac/TDMA.cc
  src/net/FlowPerformance.cc
  src/net/NetFilter.cc
  src/net/PacketCompressor.cc
  src/net/TunTap.cc
  src/python/CIL.cc
  src/python/Channelizer.cc
  src/python/Channels.cc
  src/python/Clock.cc
  src/python/Controller.cc
  src/python/Estimator.cc
  src/python/Filter.cc
  src/python/Flow.cc
  src/python/Header.cc
  src/python/IQBuffer.cc
  src/python/IQCompression.cc
  src/python/Liquid.cc
  src/python/Logger.cc
  src/python/MAC.cc
  src/python/Modem.cc
  src/python/NCO.cc
  src/python/Net.cc
  src/python/PHY.cc
  src/python/Packet.cc
  src/python/Python.cc
  src/python/RadioNet.cc
  src/python/Resample.cc
  src/python/Snapshot.cc
  src/python/Synthesizer.cc
  src/python/USRP.cc
  src/python/WorkQueue.cc
  src/util/exec.cc
  src/util/net.cc
  src/util/threads.cc
  src/util/sprintf.cc
)

target_include_directories(dragonradio PRIVATE src)
target_include_directories(dragonradio PRIVATE extern/xsimd/include)

target_link_libraries(dragonradio PRIVATE pybind11::embed)

target_include_directories(dragonradio PRIVATE ${xsimd_INCLUDE_DIRS})

target_include_directories(dragonradio PRIVATE ${CAP_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${CAP_LIBRARIES})

target_include_directories(dragonradio PRIVATE ${FIRPM_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${FIRPM_LIBRARIES})

target_include_directories(dragonradio PRIVATE ${FLAC_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${FLAC_LIBRARIES})

target_include_directories(dragonradio PRIVATE ${FLACXX_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${FLACXX_LIBRARIES})

target_include_directories(dragonradio PRIVATE ${LIQUIDDSP_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${LIQUIDDSP_LIBRARIES})

target_include_directories(dragonradio PRIVATE ${FFTW3_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${FFTW3_LIBRARIES} ${FFTW3F_LIBRARIES})

target_include_directories(dragonradio PRIVATE ${UHD_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${UHD_LIBRARIES})

target_compile_definitions(dragonradio PUBLIC ${HDF5_DEFINITIONS})
target_include_directories(dragonradio PRIVATE ${HDF5_INCLUDE_DIRS})
target_link_libraries(dragonradio PRIVATE ${HDF5_LIBRARIES})

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
