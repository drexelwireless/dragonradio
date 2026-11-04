configure_file(config.h.in config.h)

add_library(DragonRadio
  Clock.cc
  ExtensibleDataSet.cc
  IQCompression.cc
  IQCompression/FLAC.cc
  Logger.cc
  logging.cc
  Math.cc
  Neighborhood.cc
  Packet.cc
  TimerQueue.cc
  USRP.cc
  WorkQueue.cc
  cil/Scorer.cc
  dsp/FFTW.cc
  dsp/FIRDesign.cc
  dsp/TableNCO.cc
  liquid/Filter.cc
  liquid/Modem.cc
  liquid/Mutex.cc
  liquid/OFDM.cc
  liquid/PHY.cc
  liquid/Resample.cc
  phy/AutoGain.cc
  phy/ChannelSynthesizer.inl
  phy/FDChannelModulator.cc
  phy/FDChannelizer.cc
  phy/MultichannelSynthesizer.cc
  phy/NativeOFDM.cc
  phy/NativeOFDMA.cc
  phy/PHY.cc
  phy/RadioPacketQueue.cc
  phy/Synthesizer.cc
  phy/TDChannelModulator.cc
  phy/TDChannelizer.cc
  llc/DummyController.cc
  llc/SmartController.cc
  mac/FDMA.cc
  mac/MAC.cc
  mac/SlottedALOHA.cc
  mac/SlottedMAC.cc
  mac/Snapshot.cc
  mac/TDMA.cc
  net/FlowPerformance.cc
  net/NetFilter.cc
  net/PacketCompressor.cc
  net/TunTap.cc
  python/CIL.cc
  python/Channelizer.cc
  python/Channels.cc
  python/Clock.cc
  python/Controller.cc
  python/Element.cc
  python/Estimator.cc
  python/FFTW.cc
  python/Filter.cc
  python/Flow.cc
  python/Header.cc
  python/IQBuffer.cc
  python/IQCompression.cc
  python/Liquid.cc
  python/Logger.cc
  python/MAC.cc
  python/Modem.cc
  python/NativePHY.cc
  python/NCO.cc
  python/Neighborhood.cc
  python/Net.cc
  python/Node.cc
  python/PHY.cc
  python/Packet.cc
  python/Radio.cc
  python/Resample.cc
  python/Sim.cc
  python/Snapshot.cc
  python/Synthesizer.cc
  python/USRP.cc
  python/WorkQueue.cc
  sim/SimTraffic.cc
  sim/SimUSRP.cc
  util/exec.cc
  util/net.cc
  util/ssprintf.cc
  util/threads.cc
)

target_include_directories(DragonRadio PUBLIC .)

target_include_directories(DragonRadio PUBLIC xsimd)
target_link_libraries(DragonRadio PUBLIC xsimd)

target_include_directories(DragonRadio PUBLIC ${CAP_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${CAP_LIBRARIES})

target_include_directories(DragonRadio PUBLIC ${FIRPM_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${FIRPM_LIBRARIES})

target_include_directories(DragonRadio PRIVATE ${FLAC_INCLUDE_DIRS})
target_link_libraries(DragonRadio PRIVATE ${FLAC_LIBRARIES})

target_include_directories(DragonRadio PRIVATE ${FLACXX_INCLUDE_DIRS})
target_link_libraries(DragonRadio PRIVATE ${FLACXX_LIBRARIES})

target_include_directories(DragonRadio PUBLIC ${LIQUIDDSP_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${LIQUIDDSP_LIBRARIES})

# The native PHYs check received samples for NaN/Inf (std::isfinite) and drop
# frames that contain them. -Ofast (CMAKE_CXX_FLAGS_RELEASE) implies
# -ffinite-math-only, under which those checks compile to "always finite"; the
# rest of -Ofast is kept, as for the liquid PHYs.
set_source_files_properties(phy/NativeOFDM.cc phy/NativeOFDMA.cc python/NativePHY.cc
                            PROPERTIES COMPILE_OPTIONS "-fno-finite-math-only")

# Native PHY FEC (src/simulation/NativeFEC.hh): the libfec API, from libfec or
# libcorrect's libfec-compatible shim, the library liquid-dsp's FEC also uses.
find_path(FEC_INCLUDE_DIR fec.h)
find_library(FEC_LIBRARY fec)
if(FEC_INCLUDE_DIR AND FEC_LIBRARY)
  target_compile_definitions(DragonRadio PUBLIC DRAGONRADIO_NATIVE_FEC)
  target_include_directories(DragonRadio PUBLIC ${FEC_INCLUDE_DIR})
  target_link_libraries(DragonRadio PUBLIC ${FEC_LIBRARY})
else()
  message(WARNING "libfec (fec.h) not found: the native PHY supports only FEC none")
endif()

target_include_directories(DragonRadio PUBLIC ${FFTW3_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${FFTW3_LIBRARIES} ${FFTW3F_LIBRARIES})

target_include_directories(DragonRadio PUBLIC ${UHD_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${UHD_LIBRARIES})

target_compile_definitions(DragonRadio PUBLIC ${HDF5_DEFINITIONS})
target_include_directories(DragonRadio PUBLIC ${HDF5_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${HDF5_LIBRARIES})

target_link_libraries(DragonRadio PUBLIC pybind11::embed)

target_link_libraries(DragonRadio PUBLIC Eigen3::Eigen)
