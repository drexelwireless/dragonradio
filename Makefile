# DWSL - Full Radio Stack
# Note: install all dependencies system-wide for this makefile to work
##################################################

TARGETS = full-radio flexframedemod

RM       = rm -f
RMRF     = rm -rf
CXX      = g++
LINKER   = g++
CPPFLAGS = -Isrc -I/usr/local/include/
CXXFLAGS = -Ofast -march=native -g3 -Wall -pedantic -ansi -std=c++17
LDFLAGS  =
LIBS     = -lc -lfftw3f -lliquid -lliquidusrp -lm -lpthread -luhd

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

SRCDIR = src
OBJDIR = obj

ALLSOURCES := $(shell find $(SRCDIR) -name '*.cc')

ALLINCLUDES := $(shell find  $(SRCDIR) -name '*.hh')

SOURCES := \
    Clock.cc \
    ExtensibleDataSet.cc \
    Logger.cc \
    main.cc \
    Python.cc \
    RadioConfig.cc \
    USRP.cc \
    Util.cc \
    phy/FlexFrame.cc \
    phy/Gain.cc \
    phy/Liquid.cc \
    phy/MultiOFDM.cc \
    phy/OFDM.cc \
    phy/ParallelPacketDemodulator.cc \
    phy/ParallelPacketModulator.cc \
    phy/RadioPacketQueue.cc \
    mac/SlottedMAC.cc \
    mac/TDMA.cc \
    net/Net.cc \
    net/NetFilter.cc \
    net/TunTap.cc

OBJECTS := $(patsubst %.cc,$(OBJDIR)/%.o,$(SOURCES))

include mk/common.mk
include mk/cc.mk

.PHONY : all
all : $(TARGETS)

.PHONY : clean
clean :
	$(RM) $(OBJECTS) $(TARGETS)
	$(RMRF) docs/html

full-radio : $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

flexframedemod : util/flexframedemod.cc
	$(CXX) $< $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@

-include $(patsubst %.cc,$(OBJDIR)/%.dep,$(SOURCES))

.PHONY : html
html : docs/html/index.html

docs/html/index.html : docs/Doxyfile $(ALLSOURCES) $(ALLINCLUDES)
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
