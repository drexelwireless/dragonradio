# DWSL - Full Radio Stack
# Note: install all dependencies system-wide for this makefile to work
##################################################

TARGET = full-radio

RM       = rm -f
RMRF     = rm -rf
CXX      = g++
LINKER   = g++
CPPFLAGS = -Isrc -I/usr/local/include/
CXXFLAGS = -O2 -g3 -Wall -pedantic -ansi -fPIC -std=c++17 
LDFLAGS  = -lc -lconfig -lfftw3f -lliquid -lliquidusrp -lm -lpthread -luhd

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
	$(RMRF) docs/html

$(TARGET) : $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LDFLAGS) -o $@

-include $(SOURCES:$(SRCDIR)/%.cc=$(OBJDIR)/%.dep)

.PHONY : html
html : docs/html/index.html

docs/html/index.html : $(SOURCES) $(INCLUDES)
	doxygen docs/Doxyfile

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
