MODULE := engines

MODULE_OBJS := \
	achievements.o \
	advancedDetector.o \
	engine.o \
	game.o \
	metaengine.o \
	obsolete.o \
	savestate.o

# The global main menu (configure --disable-gui builds have none).
ifndef DISABLE_GUI
MODULE_OBJS += \
	dialogs.o
endif

# Include common rules
include $(srcdir)/rules.mk
