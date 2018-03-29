# to edit Makefiles (if you have spaces set in vimrx)
# do :set noet (switch to TAB)
# do :set et (switch back to spaces)

test_cconv: test_cconv.o
	g++ -I /usr/local/include/liquid/ -std=c++0x -o $@ $@.o -lliquid

cconv_2files: cconv_2files.o
	g++ -I /usr/local/include/liquid/ -std=c++0x -o $@ $@.o -lliquid

cconv_2files.o:
	g++ -I /usr/local/include/liquid/ -std=c++0x -c cconv_2files.cc

test_cconv.o:
	g++ -I /usr/local/include/liquid/ -std=c++0x -c test_cconv.cc

.PHONY : clean
clean:
	rm *.o test_cconv cconv_2files

