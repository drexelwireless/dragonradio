# DWSL - Full Radio Stack
# Note: install all dependencies system-wide for this makefile to work
##################################################

TARGET = full-radio

RM       = rm -f
CXX      = g++
LINKER   = g++
CPPFLAGS = -Isrc -I/usr/local/include/
CXXFLAGS = -O2 -g3 -Wall -pedantic -ansi -fPIC -std=c++0x
LDFLAGS  = -lc -lconfig -lfftw3f -lliquid -lm -lpthread -luhd -lliquidusrp

SRCDIR = src
OBJDIR = obj

SOURCES  := $(wildcard $(SRCDIR)/*.cc)
INCLUDES := $(wildcard $(SRCDIR)/*.hh)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.cc=$(OBJDIR)/%.o)

include mk/common.mk
include mk/cc.mk

.PHONY : all
all : $(TARGET)

.PHONY : clean
clean :
	$(RM) $(OBJECTS) $(TARGET)

$(TARGET) : $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LDFLAGS) -o $@

-include $(SOURCES:$(SRCDIR)/%.cc=$(OBJDIR)/%.dep)

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
