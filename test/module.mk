######################################################################
# Unit/regression tests, based on CxxTest.
# Use the 'test' target to run them.
# Edit TESTS and TESTLIBS to add more tests.
#
######################################################################

TESTS        := $(srcdir)/test/common/*.h \
	$(srcdir)/test/common/compression/*.h \
	$(srcdir)/test/common/formats/*.h \
	$(srcdir)/test/audio/*.h \
	$(srcdir)/test/math/*.h \
	$(srcdir)/test/graphics/hires_text*.h \
	$(srcdir)/test/image/*.h
TEST_LIBS    :=

ifdef POSIX
TEST_LIBS += test/system/null_osystem.o \
	backends/fs/posix/posix-fs-factory.o \
	backends/fs/posix/posix-fs.o \
	backends/fs/posix/posix-iostream.o \
	backends/fs/abstract-fs.o \
	backends/fs/stdiostream.o \
	backends/modular-backend.o
endif

ifdef WIN32
TEST_LIBS += test/system/null_osystem.o \
	backends/fs/windows/windows-fs-factory.o \
	backends/fs/windows/windows-fs.o \
	backends/fs/abstract-fs.o \
	backends/fs/stdiostream.o \
	backends/modular-backend.o \
	backends/platform/sdl/win32/win32_wrapper.o
endif

ifdef USE_TINYGL
TESTS += $(srcdir)/test/graphics/tinygl*.h
endif

# libcommon needs libformats and libformats needs libcommon: so libcommon is put twice
TEST_LIBS +=	audio/libaudio.a math/libmath.a common/libcommon.a common/formats/libformats.a common/compression/libcompression.a common/libcommon.a image/libimage.a graphics/libgraphics.a

ifeq ($(ENABLE_SCUMM), STATIC_PLUGIN)
	TESTS += $(srcdir)/test/engines/scumm/*.h
	# The engine libraries come after the general ones and pull more of them
	# in: hires_text.o wants the font baker in libgraphics, whose TTF support
	# in turn wants the zip reader in libcompression. The linker resolves left
	# to right, so both are repeated here - the same reason libcommon is
	# listed twice above.
	TEST_LIBS += engines/scumm/libscumm.a graphics/libgraphics.a \
		common/compression/libcompression.a common/libcommon.a
	# The hi-res hook census reads charset.h/charset.cpp back out of the tree
	# the runner was built from, so it needs to know where that tree is.
	SCUMM_TEST_DEFINES := -DSCUMM_HIRES_CENSUS_SRCDIR=\"$(srcdir)\"
endif

ifeq ($(ENABLE_WINTERMUTE), STATIC_PLUGIN)
	TESTS += $(srcdir)/test/engines/wintermute/*.h
	TEST_LIBS += engines/wintermute/libwintermute.a
endif

ifeq ($(ENABLE_ULTIMA), STATIC_PLUGIN)
ifdef ENABLE_ULTIMA8
	TESTS += $(srcdir)/test/engines/ultima/ultima8/*/*.h
endif
	TEST_LIBS += engines/ultima/libultima.a
endif

ifeq ($(ENABLE_TWINE), STATIC_PLUGIN)
	TESTS += $(srcdir)/test/engines/twine/*.h
	TEST_LIBS += engines/twine/libtwine.a
endif

#
TEST_FLAGS   := --runner=StdioPrinter --no-std --no-eh
TEST_CFLAGS  := $(CFLAGS) -I$(srcdir)/test/cxxtest
TEST_LDFLAGS := $(LDFLAGS) $(LIBS)
TEST_CXXFLAGS  := $(filter-out -Wglobal-constructors,$(CXXFLAGS))
TEST_CXXFLAGS += -Wno-self-assign-overloaded
TEST_CXXFLAGS += $(SCUMM_TEST_DEFINES)

ifdef WIN32
TEST_LDFLAGS := $(filter-out -mwindows,$(TEST_LDFLAGS))
endif

ifdef N64
TEST_LDFLAGS := $(filter-out -mno-crt0,$(TEST_LDFLAGS))
endif

ifdef PSP
TEST_LIBS += backends/platform/psp/memory.o \
	backends/platform/psp/mp3.o \
	backends/platform/psp/trace.o
endif

# Enable this to get an X11 GUI for the error reporter.
#TEST_FLAGS   += --gui=X11Gui
#TEST_LDFLAGS += -L/usr/X11R6/lib -lX11


test: test/runner
	./test/runner
test/runner: test/runner.cpp $(TEST_LIBS) copy-dat
	+$(QUIET_CXX)$(LD) $(TEST_CXXFLAGS) $(CPPFLAGS) $(TEST_CFLAGS) -o $@ test/runner.cpp $(TEST_LIBS) $(TEST_LDFLAGS)
test/runner.cpp: $(TESTS) $(srcdir)/test/module.mk
	@mkdir -p test
	$(srcdir)/test/cxxtest/bin/cxxtestgen $(TEST_FLAGS) -o $@ $+

clean: clean-test
clean-test:
	-$(RM) test/runner.cpp test/runner test/engine-data/encoding.dat test/system/null_osystem.o
	-rmdir test/engine-data

test/engine-data/encoding.dat: $(srcdir)/dists/engine-data/encoding.dat
	$(MKDIR) test/engine-data
	$(CP) $(srcdir)/dists/engine-data/encoding.dat test/engine-data/encoding.dat

copy-dat: test/engine-data/encoding.dat

.PHONY: test clean-test copy-dat
