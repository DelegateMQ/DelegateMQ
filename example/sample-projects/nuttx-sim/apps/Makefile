############################################################################
# example/sample-projects/nuttx-sim/apps/Makefile
#
# Hand-rolled NuttX "apps" replacement (see Documentation/guides/customapps.rst,
# method 1: "Replace the apps/ directory completely") -- this directory is
# pointed to directly via CONFIG_APPS_DIR, so it can't rely on
# $(APPDIR)/Application.mk or $(APPDIR)/Directory.mk (those live inside the
# real nuttx-apps checkout, which this replaces for this one board build).
# Builds a single libapps.a containing the DelegateMQ test suite; NuttX's own
# init calls dmq_test_main() directly (CONFIG_INIT_ENTRYPOINT), no NSH
# shell/command registration needed.
#
# DELEGATEMQ_ROOT must be set to this repository's root (see README.md) --
# e.g.: make DELEGATEMQ_ROOT=/path/to/DelegateMQ
############################################################################

APPDIR = ${shell pwd}

-include $(TOPDIR)/Make.defs

ifeq ($(DELEGATEMQ_ROOT),)
$(error DELEGATEMQ_ROOT is not set -- pass DELEGATEMQ_ROOT=/path/to/DelegateMQ)
endif

DMQ_SRC = $(DELEGATEMQ_ROOT)/src/delegate-mq

CXXFLAGS += -I$(DMQ_SRC)
# NuttXThread.cpp is copied here without its sibling headers (NuttXThread.h,
# NuttXDelegateQueue.h) -- its quoted #include "NuttXThread.h" only searches
# the including file's own directory first; these -I entries are the
# fallback search GCC uses next when that first lookup misses.
CXXFLAGS += -I$(DMQ_SRC)/port/os/nuttx -I$(DMQ_SRC)/extras/util -I$(DMQ_SRC)/port/fault
CXXFLAGS += -DDMQ_THREAD_NUTTX -DDMQ_SERIALIZE_NONE -DDMQ_TRANSPORT_NONE

# This sample's own sources, plus the DelegateMQ library files this
# configuration needs: the NuttX thread port implementation, Timer (used by
# Test 8/10), and the generic fault handler (Fault.cpp's #if defined(__linux__)
# branch is never taken here -- NuttX's sim board explicitly -U__linux__'s
# host OS macros in its own Make.defs, so this falls through to Fault.cpp's
# portable "print and halt" branch, same as every other RTOS sample here that
# doesn't supply its own FaultHandler).
#
# These 3 are copied into this directory at build time (see the pattern rule
# below) rather than referenced in place or found via VPATH:
#   - tools/mkdeps.c always concatenates a --dep-path entry with the bare
#     filename (it never treats an argument as already a complete path), so
#     passing an absolute path here makes it search for e.g.
#     "./ /home/.../NuttXThread.cpp" and fail with "not found at any location".
#   - VPATH looked like the fix (bare filenames + a search path), but GNU
#     Make's own pattern-rule + archive interaction then decided the "real"
#     location of NuttXThread.o was the VPATH-found source directory, not
#     here -- silently writing build artifacts into DelegateMQ's own source
#     tree, and leaving `ar` unable to find them here on the next build once
#     that stray .o looked "up to date".
# A plain local copy sidesteps both problems: every path from here on is a
# bare filename in this one directory, exactly like main.cpp.
CXXSRCS  = main.cpp DelegateThreadsTests.cpp TimerDelegateTests.cpp
CXXSRCS += NuttXThread.cpp Timer.cpp Fault.cpp

CXXOBJS = $(CXXSRCS:.cpp=$(OBJEXT))

ROOTDEPPATH = --dep-path .

all: libapps.a
.PHONY: dirlinks context preconfig depend clean clean_context distclean
.PRECIOUS: libapps$(LIBEXT)

# Copy the 3 DelegateMQ library sources this sample needs into this
# directory -- see the CXXSRCS comment above for why.

NuttXThread.cpp: $(DMQ_SRC)/port/os/nuttx/NuttXThread.cpp
	$(Q) cp -f $< $@

Timer.cpp: $(DMQ_SRC)/extras/util/Timer.cpp
	$(Q) cp -f $< $@

Fault.cpp: $(DMQ_SRC)/port/fault/Fault.cpp
	$(Q) cp -f $< $@

# Compile C++ Files

$(CXXOBJS): %$(OBJEXT): %.cpp
	$(call COMPILEXX, $<, $@)

# Add object files to the apps archive

libapps.a: $(CXXOBJS)
	$(call ARCHIVE, libapps.a, $(CXXOBJS))

# Create directory links

dirlinks:

# Setup any special pre-build context

context:

# Setup any special pre-configuration context

preconfig:

# Make the dependency file, Make.deps

depend: Makefile $(CXXSRCS)
	$(Q) $(MKDEP) $(ROOTDEPPATH) "$(CXX)" -- $(CXXFLAGS) -- $(CXXSRCS) > Make.dep

# Clean the results of the last build

clean:
	$(call CLEAN)
	$(Q) $(DELFILE) NuttXThread.cpp Timer.cpp Fault.cpp

# Remove the build context and directory links

clean_context:

# Restore the directory to its original state

distclean: clean clean_context
	$(call DELFILE, Make.dep)

# Include dependencies

-include Make.dep
