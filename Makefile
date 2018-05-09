# DWSL - Full Radio Stack
# Note: install all dependencies system-wide for this makefile to work
##################################################

TARGET = full-radio

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

SRCDIR = src
OBJDIR = obj

ALLSOURCES := $(shell find $(SRCDIR) -name '*.cc')

ALLINCLUDES := $(shell find  $(SRCDIR) -name '*.hh')

SOURCES := \
    Clock.cc \
    ExtensibleDataSet.cc \
    Liquid.cc \
    Logger.cc \
    main.cc \
    ParallelPacketDemodulator.cc \
    ParallelPacketModulator.cc \
    RadioPacketQueue.cc \
    USRP.cc \
    Util.cc \
    phy/FlexFrame.cc \
    phy/MultiOFDM.cc \
    phy/OFDM.cc \
    mac/TDMA.cc \
    net/Net.cc \
    net/TunTap.cc

OBJECTS := $(patsubst %.cc,$(OBJDIR)/%.o,$(SOURCES))

include mk/common.mk
include mk/cc.mk

.PHONY : all
all : $(TARGET)

.PHONY : clean
clean :
	$(RM) $(OBJECTS) $(TARGET)
	$(RMRF) docs/html

$(TARGET) : $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@

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
