# Extra target for sanitizer builds (grid/build-sanitized.sh): link the
# production objects of $(OBJDIR) into dragonradio-$(SANITIZER) instead of
# ./dragonradio, so the optimized build is untouched.
dragonradio-$(SANITIZER) : $(OBJECTS)
	$(LINKER) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@
