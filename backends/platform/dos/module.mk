MODULE := backends/platform/dos

MODULE_OBJS := \
	dos.o \
	dos-exit.o \
	dos-heap.o \
	dos-irq.o \
	dos-loading.o \
	../../graphics/dos/dos-graphics.o \
	../../mixer/dos/dos-mixer.o \
	../../events/dos/dos-events.o \
	../../mutex/dos/dos-mutex.o \
	../../timer/dos/dos-timer.o

# dos-heap.cpp: the heap functions run with interrupts off.
LDFLAGS += -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc,--wrap=memalign
# dos-loading.cpp: counts the bytes files give, for the loading screen.
LDFLAGS += -Wl,--wrap=_read
# dos-timer.cpp: runs the timer procs a real-mode call held back.
LDFLAGS += -Wl,--wrap=__dpmi_int
# dos-exit.cpp: the last step of every exit (timer out, PIT mode, trace).
LDFLAGS += -Wl,--wrap=_exit
# DJGPP's x87 emulator, linked in: libemu's _npxsetup() replaces libc's
# (crt1.o, scanned last, asks for it: -u pulls it from libemu first). With
# an FPU it does what libc's does; without one (a 486SX) it has the DPMI
# host trap FPU instructions to the emulator, where libc's would look for
# EMU387.DXE beside the program.
LDFLAGS += -Wl,-u,__npxsetup
LIBS += -lemu

# We don't use rules.mk but rather manually update OBJS and MODULE_DIRS.
MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))
