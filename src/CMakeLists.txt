configure_file(config.h.in config.h)

add_library(DragonRadio
  Clock.cc
  ExtensibleDataSet.cc
  IQCompression.cc
  IQCompression/FLAC.cc
  Logger.cc
  logging.cc
  Math.cc
  Packet.cc
  RadioNet.cc
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
  phy/FDChannelModulator.cc
  phy/FDChannelizer.cc
  phy/MultichannelSynthesizer.cc
  phy/OverlapTDChannelizer.cc
  phy/PHY.cc
  phy/RadioPacketQueue.cc
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
  python/Estimator.cc
  python/Filter.cc
  python/Flow.cc
  python/Header.cc
  python/IQBuffer.cc
  python/IQCompression.cc
  python/Liquid.cc
  python/Logger.cc
  python/MAC.cc
  python/Modem.cc
  python/NCO.cc
  python/Net.cc
  python/PHY.cc
  python/Packet.cc
  python/RadioNet.cc
  python/Resample.cc
  python/Snapshot.cc
  python/Synthesizer.cc
  python/USRP.cc
  python/WorkQueue.cc
  util/exec.cc
  util/net.cc
  util/threads.cc
  util/sprintf.cc
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

target_include_directories(DragonRadio PUBLIC ${FFTW3_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${FFTW3_LIBRARIES} ${FFTW3F_LIBRARIES})

target_include_directories(DragonRadio PUBLIC ${UHD_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${UHD_LIBRARIES})

target_compile_definitions(DragonRadio PUBLIC ${HDF5_DEFINITIONS})
target_include_directories(DragonRadio PUBLIC ${HDF5_INCLUDE_DIRS})
target_link_libraries(DragonRadio PUBLIC ${HDF5_LIBRARIES})

target_link_libraries(DragonRadio PUBLIC pybind11::embed)
