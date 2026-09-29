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
#define FORBIDDEN_SYMBOL_EXCEPTION_fwrite
#define FORBIDDEN_SYMBOL_EXCEPTION_exit
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv
#define FORBIDDEN_SYMBOL_EXCEPTION_setenv

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <crt0.h>
#include <sys/nearptr.h>
#include <sys/farptr.h>
#include <go32.h>
#include <dpmi.h>
#include <pc.h>
#include <SDL3/SDL.h>

#include "backends/platform/dos/dos.h"
#include "backends/platform/dos/dos-heap.h"
#include "backends/platform/dos/dos-loading.h"
#include "backends/platform/dos/dos-silence.h"
#include "backends/platform/dos/blaster.h"
#include "common/textconsole.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/mutex/dos/dos-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/dos/dos-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/events/dos/dos-events.h"
#include "backends/mixer/dos/dos-mixer.h"
#include "backends/mixer/null/null-mixer.h"
#include "audio/audiostream.h"
#include "audio/mixer.h"
#include "backends/graphics/dos/dos-graphics.h"
#include "common/config-manager.h"
#include "common/stream.h"
#include "common/fs.h"
#include "base/main.h"
#include "base/version.h"
#include "common/language.h"
#include "common/translation.h"

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

static void flushDeferredLog();

OSystem_DOS::OSystem_DOS() : _eventSource(nullptr), _nullMixer(nullptr) {
	// Runs after the timer's teardown (registered later, run earlier).
	atexit(flushDeferredLog);
	_fsFactory = new POSIXFilesystemFactory();
}

OSystem_DOS::~OSystem_DOS() {
	SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);	// logMessage() goes with us
	// The timer first: its interrupt handler runs timer procs that may
	// use the mixer, which ModularMixerBackend's destructor deletes.
	delete _timerManager;
	_timerManager = nullptr;
	delete _eventSource;
}

// Set once the splash screen has been shown (engines/engine.cpp).
extern bool splash;

// SDL_Log() writes to stderr, which is the screen: SDL3's Sound Blaster
// driver says which card it opened, lines that scroll a text screen and
// land on a graphics one. To the log instead.
static void SDLCALL sdlLog(void *, int, SDL_LogPriority, const char *message) {
	g_system->logMessage(LogMessageType::kInfo, Common::String::format("SDL: %s\n", message).c_str());
}

void OSystem_DOS::initBackend() {
	SDL_SetLogOutputFunction(sdlLog, nullptr);
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
	ConfMan.registerDefault("dos_mixer_selftest", false);
	// dos_loading_screen=false: no loading screen (DOS::Loading), the
	// launcher's mode set at once as before.
	ConfMan.registerDefault("dos_loading_screen", true);

	// ScummVM's splash goes to the overlay, which this backend does not
	// show yet (it draws nothing), and deciding whether to show it made
	// GUI::GuiManager parse the builtin theme and scale its fonts: about a
	// second, then a fixed 0.6 s wait, on the way to every game's first
	// frame. Engine::initGraphics() skips both once the splash has been
	// shown. Revisit with the GUI overlay (M4).
	splash = true;

	// Seed the time a timer proc gets (getTimeAndDate() with interrupts
	// off) before any timer runs.
	TimeDate now;
	getTimeAndDate(now);

	DosGraphicsManager *gfx = new DosGraphicsManager();
	_graphicsManager = gfx;
	_eventSource = new DosEventSource(gfx);
	// After SDL_Init(): that makes uclock()'s first call, which reprograms
	// the PIT, before the timer sets it to 1 kHz.
	_timerManager = new DosTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = new DefaultSaveFileManager("SAVES");
	_mixerManager = new DosMixerManager();
	_mixerManager->init();
	if (!_mixerManager->getMixer()) {
		// No Sound Blaster: a mixer that plays nothing, mixed from
		// pollEvent() so sounds still run their course.
		delete _mixerManager;
		_nullMixer = new NullMixerManager();
		_nullMixer->init();
		_mixerManager = _nullMixer;
	}

	BaseBackend::initBackend();

	// The command line's target is the active domain by now; without one
	// (the launcher) there is no game to load.
	const Common::String target = ConfMan.getActiveDomainName();
	Common::String title = ConfMan.get("description");
	if (title.empty())
		title = target;
	DOS::Loading::backendReady(_timerManager, title,
							   Common::parseLanguage(ConfMan.get("language")) == Common::KO_KOR,
							   ConfMan.getBool("dos_loading_screen") && !target.empty());
	gfx->setDeferModes(DOS::Loading::stage() == DOS::Loading::kStageText);

	if (ConfMan.getBool("dos_timer_selftest"))
		timerSelftest();
	if (ConfMan.getBool("dos_mixer_selftest"))
		mixerSelftest();
}

static volatile uint32 g_selftestCalls = 0;

static void selftestProc(void *) {
	g_selftestCalls++;
}

// A real-mode INT 1Ch hook for the self-test: the BIOS's INT 8 calls it
// from inside the chain timerIsr() makes, with interrupts on. It counts
// whether IRQ0 is masked at the PIC then, as it must be.
static volatile uint32 g_int1cCalls = 0;
static volatile uint32 g_int1cMasked = 0;

static void int1cProbe(_go32_dpmi_registers *) {
	g_int1cCalls++;
	if (inportb(0x21) & 0x01)
		g_int1cMasked++;
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
	_go32_dpmi_seginfo old1c, probe1c;
	static _go32_dpmi_registers probeRegs;
	probe1c.pm_offset = (unsigned long)int1cProbe;
	probe1c.pm_selector = _go32_my_cs();
	const bool hooked1c = DosTimerManager::installed() &&
		_go32_dpmi_get_real_mode_interrupt_vector(0x1C, &old1c) == 0 &&
		_go32_dpmi_allocate_real_mode_callback_iret(&probe1c, &probeRegs) == 0;
	if (hooked1c)
		_go32_dpmi_set_real_mode_interrupt_vector(0x1C, &probe1c);
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
	if (hooked1c) {
		_go32_dpmi_set_real_mode_interrupt_vector(0x1C, &old1c);
		_go32_dpmi_free_real_mode_callback(&probe1c);
	}
	const bool haveFile = f != nullptr;
	delete f;
	logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: timer selftest 60Hz calls=%u expect=180 getMillis=%u bios=%u%s\n",
		(uint)calls, (uint)(m1 - m0), (uint)(b1 - b0), haveFile ? "" : " (no file)").c_str());
	logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: timer selftest chain int1c=%u irq0masked=%u%s\n",
		(uint)g_int1cCalls, (uint)g_int1cMasked, hooked1c ? "" : " (no hook)").c_str());

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

namespace {

// A square wave, mono, at 22050 Hz (so the mixer converts it, as it does
// a game's sounds): 441 Hz, endless.
class ToneStream : public Audio::AudioStream {
public:
	ToneStream() : _phase(0) {}
	int readBuffer(int16 *buffer, const int numSamples) override {
		for (int i = 0; i < numSamples; ++i) {
			buffer[i] = (_phase < 25) ? 4000 : -4000;
			if (++_phase == 50)
				_phase = 0;
		}
		return numSamples;
	}
	bool isStereo() const override { return false; }
	int getRate() const override { return 22050; }
	bool endOfData() const override { return false; }
private:
	int _phase;
};

} // End of anonymous namespace

// The Sound Blaster's interrupts, counted by a handler chained in front of
// SDL3's for the self-test.
static volatile uint32 g_sbIrqs = 0;
static void sbIrqCount() {
	g_sbIrqs++;
}

// A timer proc that waits once, as SCI's MT-32 driver does after a SysEx.
static const uint kIsrDelayMs = 200;
static volatile int g_isrDelayState = 0;	// 1 armed, 2 done
static volatile uint32 g_isrDelayIrqs = 0;
static volatile uint32 g_isrDelayMillis = 0;
static void isrDelayProc(void *) {
	if (g_isrDelayState != 1)
		return;
	const uint32 i0 = g_sbIrqs;
	const uint32 m0 = g_system->getMillis();
	g_system->delayMillis(kIsrDelayMs);
	g_isrDelayIrqs = g_sbIrqs - i0;
	g_isrDelayMillis = g_system->getMillis() - m0;
	g_isrDelayState = 2;
}

// Percent of @p msecs spent in SDL_Delay(0), between spans of busy work.
static uint32 yieldShare(uint32 msecs) {
	uint32 inYield = 0;
	const uint32 start = g_system->getMillis();
	while (g_system->getMillis() - start < msecs) {
		// Work that does not end on a tick, so the yields start at random
		// phases of the millisecond count.
		for (volatile uint32 i = 0; i < 20000; i = i + 1)
			;
		const uint32 t = g_system->getMillis();
		SDL_Delay(0);
		inYield += g_system->getMillis() - t;
	}
	return inYield * 100 / (g_system->getMillis() - start);
}

void OSystem_DOS::mixerSelftest() {
	// Three RTC seconds of a tone, the main thread yielding as a game's
	// would: the frames the Sound Blaster took, per second, must be the
	// mixer's output rate (expect=). getMillis() over the same seconds
	// shows whether the mixer's interrupts-off pieces cost the IRQ0 timer
	// any ticks.
	if (_nullMixer) {
		logMessage(LogMessageType::kInfo, "DOS: mixer selftest: no audio device\n");
		return;
	}
	DosMixerManager *mixerManager = (DosMixerManager *)_mixerManager;
	Audio::Mixer *mixer = mixerManager->getMixer();
	Audio::SoundHandle handle;
	mixer->playStream(Audio::Mixer::kPlainSoundType, &handle, new ToneStream());
	// Let the device's ring fill first.
	for (int i = 0; i < 50; ++i)
		delayMillis(10);
	// The SDL buffers the device takes are 2048 frames, too coarse for a
	// count over three seconds (+-3% at 22050 Hz): the rate is the frames
	// between the first and the last callback in the window over the time
	// between them.
	uint8 s = rtcSeconds();
	while (rtcSeconds() == s)
		delayMillis(1);
	s = rtcSeconds();
	const uint32 m0 = getMillis();
	uint32 f0, c0, f1, c1;
	const uint32 start = mixerManager->framesMixed();
	while (mixerManager->framesMixed() == start && getMillis() - m0 < 500)
		delayMillis(1);	// a card that never takes data gives rate=0
	mixerManager->lastCallback(f0, c0);
	for (int edges = 0; edges < 3;) {
		delayMillis(5);
		const uint8 now = rtcSeconds();
		if (now != s) {
			s = now;
			edges++;
		}
	}
	mixerManager->lastCallback(f1, c1);
	const uint32 frames = f1 - f0;
	const uint32 millis = getMillis() - m0;
	const uint32 rate = (c1 > c0) ? (uint32)((uint64)frames * 1000 / (c1 - c0)) : 0;

	// What the sound costs the game: a main thread that does a span of
	// busy work and then yields (SDL_Delay(0) runs SDL3's other threads once),
	// and the share of its time spent in the yields, with the sound
	// running and then with the device paused. The millisecond count is
	// coarse, but sampling it is unbiased.
	const uint32 busyPlaying = yieldShare(2000);
	mixerManager->suspendAudio();
	const uint32 busyPaused = yieldShare(2000);
	mixerManager->resumeAudio();

	// One interrupts-off piece of the callback, timed: 1000 x 256 frames.
	// The PIC holds back one IRQ0 while a piece runs and delivers it at
	// the end; a piece that took over 1 ms would lose the ticks after the
	// first, and the figure would read low. getMillis() over the RTC
	// seconds above (3000 +- a few) is the check that none were lost.
	static byte piece[256 * 4];
	const uint32 t0 = getMillis();
	for (int i = 0; i < 1000; ++i)
		((Audio::MixerImpl *)mixer)->mixCallback(piece, sizeof(piece));
	const uint32 mixMs = getMillis() - t0;

	// delayMillis() from a timer proc, with the tone playing: the Sound
	// Blaster's interrupts must go on being served (only IRQ0 is held
	// back), where under a mutex on the main thread -- interrupts off --
	// none may come in. Counted in front of SDL3's handler.
	const DOS::BlasterConfig blaster = DOS::parseBlaster(getenv("BLASTER"));
	const int sbVector = blaster.irq < 8 ? 8 + blaster.irq : 0x70 + blaster.irq - 8;
	_go32_dpmi_seginfo sbOld, sbChain;
	sbChain.pm_offset = (unsigned long)sbIrqCount;
	sbChain.pm_selector = _go32_my_cs();
	_go32_dpmi_lock_code((void *)sbIrqCount, 64);
	_go32_dpmi_get_protected_mode_interrupt_vector(sbVector, &sbOld);
	const bool chained = _go32_dpmi_chain_protected_mode_interrupt_vector(sbVector, &sbChain) == 0;
	uint32 isrIrqs = 0, isrMillis = 0, offIrqs = 0;
	if (chained) {
		g_isrDelayState = 1;
		getTimerManager()->installTimerProc(isrDelayProc, 10000, nullptr, "dosIsrDelaySelftest");
		const uint32 w0 = getMillis();
		while (g_isrDelayState != 2 && getMillis() - w0 < 2000)
			delayMillis(5);
		getTimerManager()->removeTimerProc(isrDelayProc);
		isrIrqs = g_isrDelayIrqs;
		isrMillis = g_isrDelayMillis;
		{
			Common::Mutex mutex;
			Common::StackLock lock(mutex);
			const uint32 i0 = g_sbIrqs;
			delayMillis(kIsrDelayMs);
			offIrqs = g_sbIrqs - i0;
		}
		_go32_dpmi_set_protected_mode_interrupt_vector(sbVector, &sbOld);
	}
	mixer->stopHandle(handle);
	logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: mixer selftest isr-delay %ums sbirq=%u getMillis=%u blocked=%u irqoff-sbirq=%u%s\n",
		kIsrDelayMs, (uint)isrIrqs, (uint)isrMillis, (uint)DosTimerManager::delaysBlocked(), (uint)offIrqs,
		chained ? "" : " (no chain)").c_str());

	logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: mixer selftest rate=%u expect=%u frames=%u in=%ums getMillis=%u mix256=%uus yield=%u%%/%u%%\n",
		(uint)rate, mixer->getOutputRate(), (uint)frames, (uint)(c1 - c0), (uint)millis, (uint)mixMs, (uint)busyPlaying, (uint)busyPaused).c_str());
}

bool OSystem_DOS::pollEvent(Common::Event &event) {
	flushDeferredLog();
	// The IRQ0 handler runs the timers; this is the fallback should it
	// not have gone in.
	if (!DosTimerManager::installed())
		((DefaultTimerManager *)getTimerManager())->checkTimers();
	if (_nullMixer)
		_nullMixer->update(1);
	const bool got = _eventSource->pollEvent(event);
	// A key or click that skips the loading screen is not the game's.
	if (((DosGraphicsManager *)_graphicsManager)->loadingPoll(got, event))
		return false;
	return got;
}

void OSystem_DOS::engineInit() {
	DOS::Loading::enter(DOS::kLoadData);
	((DosGraphicsManager *)_graphicsManager)->engineStarted();
}

void OSystem_DOS::engineDone() {
	// An engine that stops before its first frame has failed (no game
	// data, say), and base/main.cpp's error dialog cannot be shown yet:
	// the loading screen stops and says so, and stays.
	if (DOS::Loading::stage() != DOS::Loading::kStageOff)
		DOS::Loading::halt(nullptr);
	((DosGraphicsManager *)_graphicsManager)->engineStopped();
}

void OSystem_DOS::setWindowCaption(const Common::U32String &caption) {
	// base/main.cpp's runGame() names the game once its engine exists
	// (setupGraphics() names ScummVM before that). GUIErrorMessage()
	// (engines/engine.cpp) names the window "Error" and shows a dialog
	// that cannot be shown yet: the loading screen stops and says so.
	const Common::String name = caption.encode();
	if (name == gScummVMFullVersion)
		return;
	if (caption == _("Error")) {
		DOS::Loading::halt(nullptr);
		((DosGraphicsManager *)_graphicsManager)->loadingHalted();
		return;
	}
	DOS::Loading::setTitle(name);
	DOS::Loading::enter(DOS::kLoadEngine);
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
	// Before the handler goes in (or if it could not), and after it comes
	// out: DJGPP's uclock(), which only reads the PIT and the BIOS tick
	// count. Not SDL_GetTicks(): this may run under a Common::Mutex --
	// from a mixer channel inside MixerImpl::mixCallback(), say -- and no
	// SDL call may, as SDL3's DOS mutex turns interrupts on.
	// DosTimerManager starts its tick count from the same clock, scaled
	// the same way: UCLOCKS_PER_SEC (1193180) is not a multiple of 1000,
	// and dividing by 1193 would run 0.015% fast.
	return (uint32)((uint64)uclock() * 1000 / UCLOCKS_PER_SEC);
}

void OSystem_DOS::delayMillis(uint msecs) {
	// With interrupts off -- in a timer proc, or under a mutex -- the tick
	// count stands still, and SDL_Delay() (which waits on it) would never
	// return; nor may a timer proc switch SDL3's threads.
	if (DosTimerManager::installed() && !DosTimerManager::interruptsEnabled()) {
		// A timer proc (SCI's MT-32 driver waits ~46 ms after a SysEx in
		// one) waits with only IRQ0 held back, so the other interrupts are
		// served; under a mutex on the main thread interrupts stay off,
		// which is what the mutex is for.
		if (DosTimerManager::inHandler())
			DosTimerManager::delayInHandler(msecs);
		else
			DosTimerManager::spinMillis(msecs);
		return;
	}
	SDL_Delay(msecs);	// also yields to SDL3's cooperative threads
}

// The last time of day read from DOS, for a caller with interrupts off.
static TimeDate g_lastTime;

void OSystem_DOS::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	// DOS's own local time (INT 21h 2Ah/2Ch), not time()/localtime():
	// DJGPP's tz code keeps its zone state in malloc'd memory and, with no
	// zoneinfo installed, never sets that state's leap-second count, so
	// once the heap has been used localtime() applies garbage corrections
	// and mktime() (and with it time()) returns -1. In ScummVM the clock
	// then stood still at a made-up time of day - KQ1's title waits for
	// the seconds to change and never showed its menu. DOS time has no
	// zone to convert anyway. Read the date on both sides of the time so
	// a midnight in between is not missed.
	//
	// A timer proc (interrupts off) must not call DOS: it gets the last
	// time read on the main thread (seeded in initBackend()), which is
	// copied with interrupts off so it is never seen half-written.
	uint32 flags;
	if (!DosTimerManager::interruptsEnabled()) {
		td = g_lastTime;
		return;
	}
	__dpmi_regs d1, t, d2;
	do {
		d1.h.ah = 0x2A;
		__dpmi_int(0x21, &d1);
		t.h.ah = 0x2C;
		__dpmi_int(0x21, &t);
		d2.h.ah = 0x2A;
		__dpmi_int(0x21, &d2);
	} while (d1.h.dl != d2.h.dl);
	td.tm_sec = t.h.dh;
	td.tm_min = t.h.cl;
	td.tm_hour = t.h.ch;
	td.tm_mday = d2.h.dl;
	td.tm_mon = d2.h.dh - 1;
	td.tm_year = d2.x.cx - 1900;
	td.tm_wday = d2.h.al;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	g_lastTime = td;
	if (flags & 0x200)
		__asm__ __volatile__("sti" : : : "memory");
}

// The synthesizers' silencers (dos-silence.h). Main thread and exit only.
static const int kMaxSilencers = 4;
static struct {
	DOS::SilenceProc proc;
	void *param;
} g_silencers[kMaxSilencers];

void DOS::addSilencer(SilenceProc proc, void *param) {
	for (int i = 0; i < kMaxSilencers; ++i) {
		if (!g_silencers[i].proc) {
			g_silencers[i].proc = proc;
			g_silencers[i].param = param;
			return;
		}
	}
}

void DOS::removeSilencer(SilenceProc proc, void *param) {
	for (int i = 0; i < kMaxSilencers; ++i) {
		if (g_silencers[i].proc == proc && g_silencers[i].param == param)
			g_silencers[i].proc = nullptr;
	}
}

void DOS::silenceAll() {
	for (int i = 0; i < kMaxSilencers; ++i) {
		const SilenceProc proc = g_silencers[i].proc;
		g_silencers[i].proc = nullptr;
		if (proc)
			proc(g_silencers[i].param);
	}
}

void OSystem_DOS::quit() {
	DosTimerManager::shutdown();	// no timer procs while SDL goes away
	// exit() skips the engine's shutdown, where the music drivers would
	// have stopped their notes.
	DOS::silenceAll();
	SDL_Quit();	// text mode back, keyboard interrupt unhooked
	DOS::Loading::teardown();
	exit(0);
}

// The last error() message, for the text screen fatalError() leaves.
static char g_lastError[256];

void OSystem_DOS::fatalError() {
	DosTimerManager::shutdown();
	DOS::silenceAll();
	SDL_Quit();	// text mode back (cleared), whether a game mode was set or not
	DOS::Loading::teardown();
	if (g_lastError[0]) {
		fputs("ScummVM: ", stderr);
		fputs(g_lastError, stderr);
		fputs("See SCUMMVM.LOG.\n", stderr);
	}
	exit(1);
}

// Log text that came in with interrupts off -- from a timer proc, which
// runs in the IRQ0 handler, or under a Common::Mutex. Writing the file
// there would be a DOS call, and DOS turns interrupts on: see timerIsr().
// Static, so it is locked; nothing here allocates.
static const uint kDeferredLogSize = 16384;	// a power of two
static char g_deferredLog[kDeferredLogSize];
static uint g_deferredHead = 0;		// interrupts off
static uint g_deferredTail = 0;		// interrupts off
static uint g_deferredDropped = 0;	// characters

static void appendLog(const char *text, uint len) {
	FILE *f = fopen("SCUMMVM.LOG", "a");
	if (f) {
		fwrite(text, 1, len, f);
		fclose(f);
	}
}

// Interrupts are off. A message that does not fit whole is dropped.
static void deferLog(const char *message) {
	const uint len = strlen(message);
	const uint used = (g_deferredHead - g_deferredTail) & (kDeferredLogSize - 1);
	if (len > kDeferredLogSize - 1 - used) {
		g_deferredDropped += len;
		return;
	}
	for (uint i = 0; i < len; ++i)
		g_deferredLog[(g_deferredHead + i) & (kDeferredLogSize - 1)] = message[i];
	g_deferredHead = (g_deferredHead + len) & (kDeferredLogSize - 1);
}

// Writes out whatever deferLog() kept, once interrupts are on.
static void flushDeferredLog() {
	if (!DosTimerManager::interruptsEnabled())
		return;
	char chunk[512];
	for (;;) {
		uint n = 0, dropped = 0;
		uint32 flags;
		__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
		while (n < sizeof(chunk) && g_deferredTail != g_deferredHead) {
			chunk[n++] = g_deferredLog[g_deferredTail];
			g_deferredTail = (g_deferredTail + 1) & (kDeferredLogSize - 1);
		}
		if (n == 0) {
			dropped = g_deferredDropped;
			g_deferredDropped = 0;
		}
		if (flags & 0x200)
			__asm__ __volatile__("sti" : : : "memory");
		if (n == 0) {
			if (dropped) {
				Common::String note = Common::String::format(
					"DOS: %u characters of log from interrupt time lost\n", dropped);
				appendLog(note.c_str(), note.size());
			}
			return;
		}
		appendLog(chunk, n);
	}
}

void OSystem_DOS::logMessage(LogMessageType::Type type, const char *message) {
	if (type == LogMessageType::kError)
		Common::strlcpy(g_lastError, message, sizeof(g_lastError));
	if (type == LogMessageType::kError || type == LogMessageType::kWarning)
		DOS::Loading::noteWarning(message);
	// The screen is in a graphics mode; the log is the only place output
	// can go.
	if (!DosTimerManager::interruptsEnabled()) {
		deferLog(message);
		return;
	}
	flushDeferredLog();
	appendLog(message, strlen(message));
}

void OSystem_DOS::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	Common::FSNode data("DATA");
	if (data.isDirectory())
		s.add("DATA", new Common::FSDirectory(data, 4), priority);
}

int main(int argc, char *argv[]) {
	// Before anything can call stat(), mktime() or localtime(): with TZ
	// unset and no zoneinfo, DJGPP's time-zone code uses a leap-second
	// count it never set (see getTimeAndDate()) and walks that many bogus
	// corrections in every conversion, some 200 ms each at 60000 cycles.
	// stat() converts file times with it: seconds in all before a game
	// starts. A zone given in TZ is parsed with no leap seconds. The
	// clock itself comes from DOS, which has no zone, so UTC changes no
	// time we show; a TZ the user set is kept.
	setenv("TZ", "UTC0", 0);
	tzset();

	// The text loading screen, when the command line starts a game; the
	// graphics mode waits for the game's (DOS::Loading).
	DOS::Loading::start(argc, argv);
	atexit(DOS::Loading::teardown);

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
