# DWSL - Full Radio Stack
# Note: install all dependencies system-wide for this makefile to work
##################################################

TARGETS = dragonradio flexframedemod

RM       = rm -f
RMRF     = rm -rf
CXX      = g++-8
LINKER   = g++-8
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
CPPFLAGS += -I/usr/include/python3.5 -Idependencies/pybind11/include

LIBS += -lpython3.5m

# Needed for FLAC
LIBS += -lFLAC++ -lFLAC

# Needed for xsimd
CPPFLAGS += -Idependencies/xsimd/include

# Version information
GIT_HASH=$(shell git rev-parse HEAD^{} | cut -c1-8)

GIT_REVNAME=$(shell git name-rev --name-only HEAD | grep -v "~")

DATE=$(shell date +%Y%m%d)

CPPFLAGS += -DVERSION=$(GIT_REVNAME)-$(DATE)-$(GIT_HASH)

SRCDIR = src
OBJDIR = obj

ALLSOURCES := $(shell find $(SRCDIR) -name '*.cc')

ALLINCLUDES := $(shell find  $(SRCDIR) -name '*.hh')

GENERATED += \
	python/dragon/internal_pb2.py \
	python/dragon/remote_pb2.py \
	python/sc2/cil_pb2.py \
	python/sc2/registration_pb2.py

SOURCES := \
    Clock.cc \
    ExtensibleDataSet.cc \
    IQCompression.cc \
    IQCompression/FLAC.cc \
    Logger.cc \
    main.cc \
    Math.cc \
    Packet.cc \
    RadioConfig.cc \
    TimerQueue.cc \
    USRP.cc \
    Util.cc \
    WorkQueue.cc \
    dsp/FFTW.cc \
    dsp/TableNCO.cc \
    liquid/Filter.cc \
    liquid/Mutex.cc \
    liquid/PHY.cc \
    liquid/Resample.cc \
    phy/ChannelDemodulator.cc \
    phy/ChannelModulator.cc \
    phy/TXParams.cc \
    phy/LiquidPHY.cc \
    phy/ParallelPacketDemodulator.cc \
    phy/ParallelPacketModulator.cc \
    phy/PerChannelDemodulator.cc \
    phy/RadioPacketQueue.cc \
    mac/Controller.cc \
    mac/DummyController.cc \
    mac/MAC.cc \
    mac/SmartController.cc \
    mac/SlottedALOHA.cc \
    mac/SlottedMAC.cc \
    mac/Snapshot.cc \
    mac/TDMA.cc \
    net/FlowPerformance.cc \
    net/Net.cc \
    net/NetFilter.cc \
    net/TunTap.cc \
    python/Clock.cc \
    python/Controller.cc \
    python/Estimator.cc \
    python/Filter.cc \
    python/Flow.cc \
    python/IQBuffer.cc \
    python/IQCompression.cc \
    python/LiquidEnum.cc \
    python/Logger.cc \
    python/MAC.cc \
    python/MCS.cc \
    python/NCO.cc \
    python/Net.cc \
    python/PHY.cc \
    python/PacketModulator.cc \
    python/Python.cc \
    python/RadioConfig.cc \
    python/RadioNet.cc \
    python/Resample.cc \
    python/Snapshot.cc \
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
	$(RMRF) docs/html

.PHONY : distclean
distclean : clean
	$(RM) $(GENERATED)

dragonradio : $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

flexframedemod : util/flexframedemod.cc
	$(CXX) $< $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@

-include $(patsubst %.cc,$(OBJDIR)/%.dep,$(SOURCES))

python/sc2/%_pb2.py : proto/%.proto
	protoc -I proto --python_out=$(dir $@) $(notdir $<)

python/dragon/%_pb2.py : proto/%.proto
	protoc -I proto --python_out=$(dir $@) $(notdir $<)

.PHONY : html
html : docs/doxygen/html/index.html

docs/doxygen/html/index.html : docs/doxygen/Doxyfile $(ALLSOURCES) $(ALLINCLUDES)
	doxygen $<

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
