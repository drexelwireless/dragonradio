MAKEFLAGS	:= --no-print-directory

_QUIET = @

all :

#
# quiet and noquiet make commands
#
QUIET_MAKECMD := $(filter quiet, $(MAKECMDGOALS))
MAKECMDGOALS := $(filter-out quiet, $(MAKECMDGOALS))

NOQUIET_MAKECMD := $(filter noquiet, $(MAKECMDGOALS))
MAKECMDGOALS := $(filter-out noquiet, $(MAKECMDGOALS))

ifeq ($(QUIET_MAKECMD), quiet)
VIRTUAL_GOALS += quiet

QUIET_MAKECMD := 1
QUIET := 1
else
QUIET_MAKECMD := 0
QUIET := 0
endif

ifeq ($(NOQUIET_MAKECMD), noquiet)
VIRTUAL_GOALS += noquiet

NOQUIET_MAKECMD := 1
NOQUIET := 1
endif

ifeq ($(QUIET), 1)
_QUIET = @
endif

ifeq ($(NOQUIET), 1)
_QUIET =
endif
