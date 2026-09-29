MODULE := backends/platform/dos

MODULE_OBJS := \
	dos.o \
	dos-heap.o \
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

# We don't use rules.mk but rather manually update OBJS and MODULE_DIRS.
MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))
