/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose
#define FORBIDDEN_SYMBOL_EXCEPTION_stderr
#define FORBIDDEN_SYMBOL_EXCEPTION_fputs
#define FORBIDDEN_SYMBOL_EXCEPTION_exit
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <time.h>
#include <stdio.h>
#include <crt0.h>
#include <sys/nearptr.h>
#include <SDL3/SDL.h>

#include "backends/platform/dos/dos.h"
#include "common/textconsole.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/events/dos/dos-events.h"
#include "backends/mixer/null/null-mixer.h"
#include "backends/graphics/dos/dos-graphics.h"
#include "common/config-manager.h"
#include "common/fs.h"
#include "base/main.h"

// ScummVM's call depth is far past DJGPP's 256 KB default stack.
unsigned _stklen = 1024 * 1024;

// Our own main() does what SDL_RunApp() would, so SDL3's definition of this
// is not linked. NONMOVE_SBRK keeps the data segment's base fixed as the
// heap grows (SDL3 keeps near pointers into the framebuffer); LOCK_MEMORY
// locks code, data and stack at startup, for the interrupt handlers: the
// debug socket's COM receive ISR (M1) and M3's timer.
// main() clears it again, as SDL_RunApp() does, so later malloc()s are not
// locked.
int _crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_LOCK_MEMORY;

OSystem_DOS::OSystem_DOS() : _eventSource(nullptr) {
	_fsFactory = new POSIXFilesystemFactory();
}

OSystem_DOS::~OSystem_DOS() {
	delete _eventSource;
}

void OSystem_DOS::initBackend() {
	SDL_SetHint(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
		error("SDL_Init: %s", SDL_GetError());

	// dos_truecolor=auto|off: off advertises CLUT8 only. dos_vsync=off|wait:
	// wait sends each frame in the vertical retrace. dos_force_fallback=true
	// uses the 640x480 line-repeat mode even when 640x400 exists (testing).
	ConfMan.registerDefault("dos_truecolor", "auto");
	ConfMan.registerDefault("dos_vsync", "off");
	ConfMan.registerDefault("dos_force_fallback", false);

	DosGraphicsManager *gfx = new DosGraphicsManager();
	_graphicsManager = gfx;
	_eventSource = new DosEventSource(gfx);
	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = new DefaultSaveFileManager("SAVES");
	_mixerManager = new NullMixerManager();
	_mixerManager->init();

	BaseBackend::initBackend();
}

bool OSystem_DOS::pollEvent(Common::Event &event) {
	((DefaultTimerManager *)getTimerManager())->checkTimers();
	((NullMixerManager *)_mixerManager)->update(1);
	return _eventSource->pollEvent(event);
}

Common::MutexInternal *OSystem_DOS::createMutex() {
	// No preemption yet: SDL3's threads only switch inside SDL calls.
	return new NullMutexInternal();
}

uint32 OSystem_DOS::getMillis(bool skipRecord) {
	return (uint32)SDL_GetTicks();
}

void OSystem_DOS::delayMillis(uint msecs) {
	SDL_Delay(msecs);	// also yields to SDL3's cooperative threads
}

void OSystem_DOS::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	time_t curTime = time(0);
	struct tm t = *localtime(&curTime);
	td.tm_sec = t.tm_sec;
	td.tm_min = t.tm_min;
	td.tm_hour = t.tm_hour;
	td.tm_mday = t.tm_mday;
	td.tm_mon = t.tm_mon;
	td.tm_year = t.tm_year;
	td.tm_wday = t.tm_wday;
}

void OSystem_DOS::quit() {
	SDL_Quit();	// text mode back, keyboard interrupt unhooked
	exit(0);
}

void OSystem_DOS::fatalError() {
	SDL_Quit();
	exit(1);
}

void OSystem_DOS::logMessage(LogMessageType::Type type, const char *message) {
	// The screen is in a graphics mode; the log is the only place output can go.
	FILE *f = fopen("SCUMMVM.LOG", "a");
	if (f) {
		fputs(message, f);
		fclose(f);
	}
}

void OSystem_DOS::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	Common::FSNode data("DATA");
	if (data.isDirectory())
		s.add("DATA", new Common::FSDirectory(data, 4), priority);
}

int main(int argc, char *argv[]) {
	_crt0_startup_flags &= ~_CRT0_FLAG_LOCK_MEMORY;

	// SDL3's VESA driver maps the framebuffer through the "fat DS" pointer.
	if (!__djgpp_nearptr_enable()) {
		fputs("__djgpp_nearptr_enable failed (needs a DPMI host that allows it)\n", stderr);
		return 1;
	}
	g_system = new OSystem_DOS();
	int res = scummvm_main(argc, argv);
	g_system->destroy();	// deletes the graphics manager, and with it the window
	SDL_Quit();	// text mode back, keyboard interrupt unhooked
	return res;
}

#endif
