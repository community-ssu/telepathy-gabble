# Included by debian/rules.

# Use Maemo's Python, not the ancient Python provided by Scratchbox
DEB_CONFIGURE_EXTRA_FLAGS += PYTHON=/usr/bin/python2.5 --enable-is-a-phone

# Use soft-float and thumb mode if it enabled.
ifneq (,$(findstring thumb,$(DEB_BUILD_OPTIONS)))
	CFLAGS += -mthumb
endif

ifneq (,$(findstring coverage,$(DEB_BUILD_OPTIONS)))
	SBOX_USE_CCACHE := no
	DEB_CONFIGURE_EXTRA_FLAGS += --enable-coverage
endif

# Use parallel jobs if possible
ifneq (,$(findstring parallel,$(DEB_BUILD_OPTIONS)))
	PARALLEL_JOBS := $(shell echo $(DEB_BUILD_OPTIONS) | \
		sed -e 's/.*parallel=\([0-9]\+\).*/\1/')
	ifeq ($(DEB_BUILD_OPTIONS),$(PARALLEL_JOBS))
		PARALLEL_JOBS := $(shell if [ -f /proc/cpuinfo ]; \
			then echo `cat /proc/cpuinfo | grep 'processor' | wc -l`; \
			else echo 1; fi)
	endif
	NJOBS := -j$(PARALLEL_JOBS)
endif
DEB_MAKE_ENVVARS := MAKEFLAGS=$(NJOBS)

pre-build:: configure

configure:
	ACLOCAL=aclocal-1.9 AUTOMAKE=automake-1.9 \
		sh ./autogen.sh --no-configure
