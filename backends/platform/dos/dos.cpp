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
#include <sys/farptr.h>
#include <go32.h>
#include <pc.h>
#include <SDL3/SDL.h>

#include "backends/platform/dos/dos.h"
#include "backends/platform/dos/dos-heap.h"
#include "common/textconsole.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/mutex/dos/dos-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/dos/dos-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/events/dos/dos-events.h"
#include "backends/mixer/null/null-mixer.h"
#include "backends/graphics/dos/dos-graphics.h"
#include "common/config-manager.h"
#include "common/stream.h"
#include "common/fs.h"
#include "base/main.h"

// ScummVM's call depth is far past DJGPP's 256 KB default stack.
unsigned _stklen = 1024 * 1024;

// Our own main() does what SDL_RunApp() would, so SDL3's definition of this
// is not linked. NONMOVE_SBRK keeps the data segment's base fixed as the
// heap grows (SDL3 keeps near pointers into the framebuffer); LOCK_MEMORY
// locks code, data and stack at startup and, left set, every later sbrk()
// too: the interrupt handlers (the debug socket's COM receive ISR, and the
// IRQ0 timer, whose timer procs are engine code touching the heap) must
// never page-fault. SDL_RunApp() would clear it before the app runs; our
// main() keeps it. Blocks of 256 KB and up come from pageable DPMI memory
// instead, so the locked part fits in 16 MB (dos-heap.cpp).
int _crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_LOCK_MEMORY;

OSystem_DOS::OSystem_DOS() : _eventSource(nullptr) {
	_fsFactory = new POSIXFilesystemFactory();
}

OSystem_DOS::~OSystem_DOS() {
	// The timer first: its interrupt handler runs timer procs that may
	// use the mixer, which ModularMixerBackend's destructor deletes.
	delete _timerManager;
	_timerManager = nullptr;
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
	ConfMan.registerDefault("dos_timer_selftest", false);

	DosGraphicsManager *gfx = new DosGraphicsManager();
	_graphicsManager = gfx;
	_eventSource = new DosEventSource(gfx);
	// After SDL_Init(): that makes uclock()'s first call, which reprograms
	// the PIT, before the timer sets it to 1 kHz.
	_timerManager = new DosTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = new DefaultSaveFileManager("SAVES");
	_mixerManager = new NullMixerManager();
	_mixerManager->init();

	BaseBackend::initBackend();

	if (ConfMan.getBool("dos_timer_selftest"))
		timerSelftest();
}

static volatile uint32 g_selftestCalls = 0;

static void selftestProc(void *) {
	g_selftestCalls++;
}

static uint8 cmosRead(uint8 reg) {
	outportb(0x70, reg);
	return inportb(0x71);
}

// The RTC's seconds register, once it is not mid-update: a clock that
// shares nothing with the PIT.
static uint8 rtcSeconds() {
	while (cmosRead(0x0A) & 0x80)
		;
	return cmosRead(0x00);
}

void OSystem_DOS::timerSelftest() {
	// Three RTC seconds of a 60 Hz timer proc, while the main thread never
	// yields: it reads a file and takes a mutex over and over. Only a
	// preemptive timer gets the 180 calls in; getMillis() and the BIOS
	// tick count must agree with the RTC.
	Common::SeekableReadStream *f = Common::FSNode("SCUMMVM.EXE").createReadStream();
	Common::Mutex mutex;
	byte buf[4096];
	uint8 s = rtcSeconds();
	while (rtcSeconds() == s)
		;
	s = rtcSeconds();
	g_selftestCalls = 0;
	getTimerManager()->installTimerProc(selftestProc, 1000000 / 60, nullptr, "dosTimerSelftest");
	const uint32 m0 = getMillis();
	const uint32 b0 = _farpeekl(_dos_ds, 0x46C);
	for (int edges = 0; edges < 3;) {
		if (f && f->read(buf, sizeof(buf)) < sizeof(buf))
			f->seek(0);
		for (int i = 0; i < 100; ++i) {
			Common::StackLock lock(mutex);
		}
		const uint8 now = rtcSeconds();
		if (now != s) {
			s = now;
			edges++;
		}
	}
	const uint32 m1 = getMillis();
	const uint32 b1 = _farpeekl(_dos_ds, 0x46C);
	const uint32 calls = g_selftestCalls;
	getTimerManager()->removeTimerProc(selftestProc);
	const bool haveFile = f != nullptr;
	delete f;
	logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: timer selftest 60Hz calls=%u expect=180 getMillis=%u bios=%u%s\n",
		(uint)calls, (uint)(m1 - m0), (uint)(b1 - b0), haveFile ? "" : " (no file)").c_str());

	// delayMillis() both ways: SDL_Delay() on uclock(), and (under a mutex,
	// interrupts off) polling the PIT. 20 x 10 ms each.
	uint32 t0 = getMillis();
	for (int i = 0; i < 20; ++i)
		delayMillis(10);
	const uint32 sdlDelay = getMillis() - t0;
	t0 = getMillis();
	for (int i = 0; i < 20; ++i) {
		Common::StackLock lock(mutex);
		delayMillis(10);
	}
	const uint32 spinDelay = getMillis() - t0;
	logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: timer selftest delayMillis(10)x20 sdl=%u irqoff=%u\n", (uint)sdlDelay, (uint)spinDelay).c_str());
}

bool OSystem_DOS::pollEvent(Common::Event &event) {
	// The IRQ0 handler runs the timers; this is the fallback should it
	// not have gone in.
	if (!DosTimerManager::installed())
		((DefaultTimerManager *)getTimerManager())->checkTimers();
	((NullMixerManager *)_mixerManager)->update(1);
	return _eventSource->pollEvent(event);
}

Common::MutexInternal *OSystem_DOS::createMutex() {
	// The IRQ0 handler preempts the main thread (SDL3's own threads only
	// switch inside SDL calls): a mutex holds interrupts off.
	return new DosMutexInternal();
}

uint32 OSystem_DOS::getMillis(bool skipRecord) {
	// Called from the timer's interrupt handler too (DefaultTimerManager::
	// handler()): the tick count is one aligned load.
	if (DosTimerManager::installed())
		return DosTimerManager::millis();
	return (uint32)SDL_GetTicks();
}

void OSystem_DOS::delayMillis(uint msecs) {
	// With interrupts off -- in a timer proc, or under a mutex -- the tick
	// count stands still, and SDL_Delay() (which waits on it) would never
	// return; nor may a timer proc switch SDL3's threads.
	if (DosTimerManager::installed() && !DosTimerManager::interruptsEnabled()) {
		DosTimerManager::spinMillis(msecs);
		return;
	}
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
	// SDL3's VESA driver maps the framebuffer through the "fat DS" pointer.
	if (!__djgpp_nearptr_enable()) {
		fputs("__djgpp_nearptr_enable failed (needs a DPMI host that allows it)\n", stderr);
		return 1;
	}
	dosHeapEnableLargeBlocks();
	g_system = new OSystem_DOS();
	int res = scummvm_main(argc, argv);
	g_system->destroy();	// deletes the graphics manager, and with it the window
	SDL_Quit();	// text mode back, keyboard interrupt unhooked
	return res;
}

#endif
