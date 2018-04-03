# DWSL - Full Radio Stack
# Note: install all dependencies system-wide for this makefile to work
##################################################

CXX 				:= g++
CXXFLAGS			:= -I./ -I/usr/local/include/ -O2 -g3 -Wall -pedantic -ansi  -fPIC  -std=c++0x
LIBS				:= -lc -lconfig -lfftw3f -lliquid -lm -lpthread -luhd -lliquidusrp
RM				:= rm -f
BINS				:= full-radio

CC_OBJS		:= main.o NET.o TunTap.o MACPHY.o

full-radio : $(CC_OBJS)
	$(CXX) $(CXXFLAGS) $(CC_OBJS) $(LIBS) -o $@

$(CC_OBJS) : %.o : %.cc


.PHONY : clean
clean :
	$(RM) *.o $(BINS)
