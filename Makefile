# DWSL - Full Radio Stack
# Note: install all dependencies system-wide for this makefile to work
##################################################

TARGETS = dragonradio

RM       = rm -f
RMRF     = rm -rf
CXX      = g++
LINKER   = g++
CPPFLAGS = -Isrc -I/usr/local/include/
CXXFLAGS = -Ofast -march=native -g3 -Wall -pedantic -ansi -std=c++17
LDFLAGS  = -rdynamic
LIBS     = -lc -lfftw3f -lliquid -lm -lpthread -luhd

# Needed for HDF5
CPPFLAGS += -I/usr/include/hdf5/serial

LIBS += \
	/usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5_hl_cpp.a \
	/usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5_cpp.a \
	/usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5_hl.a \
	/usr/lib/x86_64-linux-gnu/hdf5/serial/libhdf5.a \
	-lsz -lz -ldl

# Needed for Python
PYTHONVER = 3.8

CPPFLAGS += -I/usr/include/python$(PYTHONVER) -Idependencies/pybind11/include
LIBS += -lpython$(PYTHONVER)

# Needed for FLAC
LIBS += -lFLAC++ -lFLAC

# Needed for FFTW
LIBS += -lfftw3

# Needed for xsimd
CPPFLAGS += -Idependencies/xsimd/include

# Needed for firpm
LIBS += -lfirpm

# Needed for capabilities
LIBS += -lcap

SRCDIR = src
OBJDIR = obj

ALLSOURCES := $(shell find $(SRCDIR) -name '*.cc')

ALLINCLUDES := $(shell find  $(SRCDIR) -name '*.hh')

GENERATED += \
	python/dragonradio/dragonradio/internal_pb2.py \
	python/dragonradio/dragonradio/remote_pb2.py \
	python/dragonradio/sc2/cil_pb2.py \
	python/dragonradio/sc2/registration_pb2.py

SOURCES := \
    Clock.cc \
    ExtensibleDataSet.cc \
    IQCompression.cc \
    IQCompression/FLAC.cc \
    Logger.cc \
    logging.cc \
    main.cc \
    Math.cc \
    Packet.cc \
    TimerQueue.cc \
    USRP.cc \
    Util.cc \
    WorkQueue.cc \
    cil/Scorer.cc \
    dsp/FFTW.cc \
    dsp/FIRDesign.cc \
    dsp/TableNCO.cc \
    liquid/Filter.cc \
    liquid/Modem.cc \
    liquid/Mutex.cc \
    liquid/OFDM.cc \
    liquid/PHY.cc \
    liquid/Resample.cc \
    phy/AutoGain.cc \
    phy/FDChannelModulator.cc \
    phy/FDChannelizer.cc \
    phy/MultichannelSynthesizer.cc \
    phy/OverlapTDChannelizer.cc \
    phy/PHY.cc \
    phy/RadioPacketQueue.cc \
    phy/TDChannelModulator.cc \
    phy/TDChannelizer.cc \
    llc/DummyController.cc \
    llc/SmartController.cc \
    mac/FDMA.cc \
    mac/MAC.cc \
    mac/SlottedALOHA.cc \
    mac/SlottedMAC.cc \
    mac/Snapshot.cc \
    mac/TDMA.cc \
    net/FlowPerformance.cc \
    net/Net.cc \
    net/NetFilter.cc \
    net/PacketCompressor.cc \
    net/TunTap.cc \
    python/CIL.cc \
    python/Channelizer.cc \
    python/Channels.cc \
    python/Clock.cc \
    python/Controller.cc \
    python/Estimator.cc \
    python/Filter.cc \
    python/Flow.cc \
    python/Header.cc \
    python/IQBuffer.cc \
    python/IQCompression.cc \
    python/Liquid.cc \
    python/Logger.cc \
    python/MAC.cc \
    python/Modem.cc \
    python/NCO.cc \
    python/Net.cc \
    python/PHY.cc \
    python/Packet.cc \
    python/Python.cc \
    python/RadioNet.cc \
    python/Resample.cc \
    python/Snapshot.cc \
    python/Synthesizer.cc \
    python/USRP.cc \
    python/WorkQueue.cc

OBJECTS := $(patsubst %.cc,$(OBJDIR)/%.o,$(SOURCES))

include mk/common.mk
include mk/cc.mk

.PHONY : all
all : $(TARGETS) $(GENERATED)

.PHONY : clean
clean :
	$(RM) $(OBJECTS) $(TARGETS)

.PHONY : distclean
distclean : clean
	$(RM) $(GENERATED)

dragonradio : $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@
	sudo chown root $@
	sudo chmod u+s $@
	# If file capabilities are supported, do this instead
	#sudo setcap cap_sys_nice,cap_net_admin+p $@

flexframedemod : util/flexframedemod.cc
	$(CXX) $< $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@

-include $(patsubst %.cc,$(OBJDIR)/%.dep,$(SOURCES))

python/dragonradio/sc2/%_pb2.py : proto/%.proto
	protoc -I proto --python_out=$(dir $@) $(notdir $<)

python/dragonradio/dragonradio/%_pb2.py : proto/%.proto
	protoc -I proto --python_out=$(dir $@) $(notdir $<)

#
# Print an arbitrary makefile variable
#
print-% :
	@echo $* = $($*)

#
# Rules for virtual goals
#
ifeq ($(MAKECMDGOALS),)
$(VIRTUAL_GOALS) : all
	@true
else
$(VIRTUAL_GOALS) :
	@true
endif
