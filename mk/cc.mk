#
# Global GCC invocation macro
#
# This ugly rule takes some explaining:
#
# 1) Get GCC to produce dependencies with the -Wp,-MD option. This causes GCC to
# pass the -MD flag to the preprocessor, which causes it to output dependency
# information.
#
# 2) The first sed line strips the target and replaces it with the true name of
# the object file. We do this because we can't trust the preprocessor to retain
# the path prefix of the file being preprocessed.
#
# 3) The second sed line makes all the dependencies depend on nothing: this
# prevents make from complaining if they don't exist, e.g., if a header file
# that is mentioned in the dependency information is moved. This is done by:
#   a) Stripping the target
#   b) Removing line continuations
#   c) Removing empty lines
#   d) Adding " : " to the end of the line
#
define compile-gcc
	set -e; \
	mkdir -p $(dir $@); \
	$(1) $(2) -Wp,-MD,$(basename $@).d -c $< -o $@ \
	|| (rm -f $@ $(basename $@).d $(basename $@).dep && exit 1); \
	[ -s $(basename $@).d ] \
	&& { \
		sed -e 's|^[^:]*: *|$(basename $@).o : |' \
		    < $(basename $@).d; \
		sed -e 's|^[^:]*: *||' \
		    -e :a -e '/ *\\$$/N; s|\\\n||; ta' \
		    -e '/^$$/ d' \
		    -e 's|$$| :|' \
		    < $(basename $@).d; \
	} >$(basename $@).dep \
	&& rm -f $(basename $@).d \
	|| (rm -f $@ $(basename $@).d $(basename $@).dep && exit 1);
endef

#
# C compilation macros
#
define compile-c
	@echo "Compiling C file" $(subst $(SRCDIR)/,,$(subst $(OBJDIR)/,,$<))
	$(_QUIET)$(call compile-gcc, $(CC),$(CFLAGS) $(CPPFLAGS))
endef

#
# C++ compilation macros
#
define compile-cpp
	@echo "Compiling C++ file" $(subst $(SRCDIR)/,,$(subst $(OBJDIR)/,,$<))
	$(_QUIET)$(call compile-gcc, $(CXX),$(CXXFLAGS) $(CPPFLAGS))
endef

#
# Dependency rules
#
# When dependency files are included, check to see if the corresponding .o file
# exists. If so, remove it to force the dependency file to be rebuilt. This
# avoids the problem where the .dep file is removed but the .o file isn't, and
# something the .o depends on is then modified. In this case the .o will not be
# rebuilt because the dependency information has been lost!
#
# We make .dep files depend on .c/.cpp files so this rule isn't used for other
# languages!
#

$(OBJDIR)/%.dep : $(SRCDIR)/%.c
	@if [ -f $(basename $@).o ]; then rm -f $(basename $@).o; fi

$(OBJDIR)/%.dep : $(SRCDIR)/%.cc
	@if [ -f $(basename $@).o ]; then rm -f $(basename $@).o; fi

#
# C rule
#

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	$(compile-c)

#
# C++ rules
#

$(OBJDIR)/%.o : $(SRCDIR)/%.cc
	$(compile-cpp)
