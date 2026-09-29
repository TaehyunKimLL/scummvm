MODULE := backends/platform/dos

MODULE_OBJS := \
	dos.o \
	dos-heap.o \
	../../graphics/dos/dos-graphics.o \
	../../events/dos/dos-events.o \
	../../mutex/dos/dos-mutex.o

# dos-heap.cpp: the heap functions run with interrupts off.
LDFLAGS += -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc,--wrap=memalign

# We don't use rules.mk but rather manually update OBJS and MODULE_DIRS.
MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))
