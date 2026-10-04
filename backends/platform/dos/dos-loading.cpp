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

// stdout is pointed at NUL while the loading screen is up.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <sys/movedata.h>

#include "backends/platform/dos/dos-loading.h"
#include "backends/platform/dos/loading-screen.h"
#include "backends/timer/dos/dos-timer.h"
#include "common/system.h"
#include "common/timer.h"

namespace DOS {
namespace Loading {

static Stage g_stage = kStageOff;
static uint32 g_startMs = 0;
static uint32 g_graphicsMs = 0;
static LoadProgress g_progress;
static Common::String g_title;
static bool g_korean = false;
static bool g_haveFont = false;
static byte g_font[256 * kLoadGlyphH];
static Common::TimerManager *g_timers = nullptr;
static bool g_timerOn = false;
static volatile bool g_halted = false;
static bool g_sdlOwnsScreen = false;	// SDL_Quit() will set text mode back (and clear it)
static int g_savedStdout = -1;
static void (*g_hook)(void *) = nullptr;
static void *g_hookParam = nullptr;
static bool g_inHook = false;

// The text screen: 80x25, colour text memory at B800h, page 0.
static const int kCols = 80;
static const int kRowTitle = 7;
static const int kRowName = 9;
static const int kRowBar = 12;
static const int kRowLabel = 14;
static const int kRowHalt = 18;
static const int kBarCol = 10;
static const int kBarCells = 60;
static const int kPercentCol = kBarCol + kBarCells + 1;

// Shown bar state, so the timer proc writes only what changed.
static int g_textHalves = -1;
static int g_textPercent = -1;
static bool g_asciiBar = false;	// a double-byte screen: no CP437 block characters

static uint32 clockMs() {
	if (DosTimerManager::installed())
		return DosTimerManager::millis();
	// getMillis()'s own fallback (dos.cpp): the same clock, the same scale.
	return (uint32)((uint64)uclock() * 1000 / UCLOCKS_PER_SEC);
}

uint32 now() {
	return clockMs() - g_startMs;
}

// One character cell. Saves and restores FS, so it is safe from the IRQ0
// handler whatever the main thread was doing with FS.
static inline void poke(int row, int col, byte ch, byte attr) {
	const uint32 addr = 0xB8000 + (row * kCols + col) * 2;
	const uint16 v = (uint16)(ch | (attr << 8));
	__asm__ __volatile__(
		"pushw %%fs\n\t"
		"movw %w0, %%fs\n\t"
		"movw %w1, %%fs:(%k2)\n\t"
		"popw %%fs"
		: : "r"((uint16)_dos_ds), "r"(v), "r"(addr) : "memory");
}

static void textRow(int row, const char *s, byte attr) {
	const int len = MIN<int>(strlen(s), kCols);
	const int col = (kCols - len) / 2;
	for (int c = 0; c < kCols; ++c)
		poke(row, c, (c >= col && c < col + len) ? (byte)s[c - col] : ' ', attr);
}

// The bar and the percentage: 60 cells of two halves each (CP437 full
// block, left half block, light shade; ASCII on a double-byte screen).
// Called from the timer proc, or
// from the main thread with interrupts off.
static void drawTextBar() {
	const uint16 v = g_progress.value(now());
	const int halves = v * kBarCells * 2 / 1000;
	const int percent = v / 10;
	if (halves != g_textHalves) {
		for (int i = 0; i < kBarCells; ++i) {
			byte ch, attr;
			textBarCell(CLIP(halves - i * 2, 0, 2), g_asciiBar, ch, attr);
			poke(kRowBar, kBarCol + i, ch, attr);
		}
		g_textHalves = halves;
	}
	if (percent != g_textPercent) {
		// By hand: no libc formatting in an interrupt handler.
		const char buf[4] = {
			(char)(percent >= 100 ? '1' : ' '),
			(char)(percent >= 10 ? '0' + percent / 10 % 10 : ' '),
			(char)('0' + percent % 10), '%'
		};
		for (int i = 0; i < 4; ++i)
			poke(kRowBar, kPercentCol + i, buf[i], 0x07);
		g_textPercent = percent;
	}
}

// A double-byte screen (a Korean, Japanese or Chinese DOS): the active code
// page is one, or a DBCS lead-byte table is set.
static bool doubleByteScreen() {
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x6601;	// get the active code page (DOS 3.3+)
	if (__dpmi_int(0x21, &r) == 0 && !(r.x.flags & 1) && isDbcsCodePage(r.x.bx))
		return true;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x6300;	// DS:SI -> the DBCS lead-byte table (DOS 4.0+)
	if (__dpmi_int(0x21, &r) == 0 && !(r.x.flags & 1) && (r.x.ds || r.x.si)) {
		byte t[2];
		dosmemget(r.x.ds * 16 + r.x.si, 2, t);
		return dbcsTableHasLeadBytes(t);
	}
	return false;
}

static void textTick(void *) {
	if (g_stage == kStageText && !g_halted)
		drawTextBar();
}

static inline uint32 cli() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}

static inline void restoreFlags(uint32 flags) {
	if (flags & 0x200)
		__asm__ __volatile__("sti" : : : "memory");
}

static void drawTextName() {
	const Common::String name = asciiTitle(g_title, kCols - 10);
	const Common::String line = "Loading " + name;
	const int col = (kCols - (int)line.size()) / 2;
	for (int c = 0; c < kCols; ++c) {
		const int i = c - col;
		const bool in = i >= 0 && i < (int)line.size();
		poke(kRowName, c, in ? (byte)line[i] : ' ', in && i >= 8 ? 0x0E : 0x07);
	}
}

static void drawTextLabel() {
	Common::String label = Common::String(kLoadPhases[g_progress.phase()].label) + "...";
	textRow(kRowLabel, label.c_str(), 0x07);
}

static void drawTextScreen() {
	const uint32 flags = cli();
	for (int r = 0; r < 25; ++r)
		for (int c = 0; c < kCols; ++c)
			poke(r, c, ' ', 0x07);
	textRow(kRowTitle, "ScummVM for DOS", 0x0F);
	drawTextName();
	g_textHalves = g_textPercent = -1;
	drawTextBar();
	drawTextLabel();
	restoreFlags(flags);
}

// Whether argv starts a game: a word that is not an option, and none of
// the commands that print something and exit (base/commandLine.cpp).
static bool startsGame(int argc, char *argv[], Common::String &target) {
	static const char *const kCommands[] = {
		"help", "version", "dump-all-detection-entries", "stats", "add", "detect", "auto-detect",
		"md5", "md5mac", "test-detector", "upgrade-targets", nullptr
	};
	bool game = false;
	for (int i = 1; i < argc; ++i) {
		const char *a = argv[i];
		if (a[0] != '-') {
			if (!game)
				target = a;
			game = true;
			continue;
		}
		if (a[1] == '-') {
			Common::String name(a + 2);
			const char *eq = strchr(a + 2, '=');
			if (eq)
				name = Common::String(a + 2, eq);
			if (name.hasPrefix("list-"))
				return false;
			for (int k = 0; kCommands[k]; ++k)
				if (name == kCommands[k])
					return false;
		} else if (a[1] && strchr("hvtza", a[1]) && a[2] == '\0') {
			return false;
		}
	}
	return game;
}

void start(int argc, char *argv[]) {
	g_startMs = clockMs();
	Common::String target;
	if (!startsGame(argc, argv, target))
		return;
	g_title = target;
	g_progress.reset(0);

	// Mode 3 (80x25 colour text, page 0), unless the screen is in it already.
	__dpmi_regs r;
	const byte mode = _farpeekb(_dos_ds, 0x449) & 0x7F;
	const uint16 cols = _farpeekw(_dos_ds, 0x44A);
	const byte rows = _farpeekb(_dos_ds, 0x484);
	const byte page = _farpeekb(_dos_ds, 0x462);
	if (mode != 3 || cols != 80 || (rows != 24 && rows != 0) || page != 0) {
		memset(&r, 0, sizeof(r));
		r.x.ax = 0x0003;
		__dpmi_int(0x10, &r);
	}
	// No cursor.
	memset(&r, 0, sizeof(r));
	r.h.ah = 0x01;
	r.x.cx = 0x2000;
	__dpmi_int(0x10, &r);
	// Anything printed before stdout is moved away (a usage() message, say)
	// lands under the screen's content rather than on it.
	memset(&r, 0, sizeof(r));
	r.h.ah = 0x02;
	r.h.dh = kRowHalt + 2;
	__dpmi_int(0x10, &r);

	// The VGA BIOS's 8x16 font, for the graphics stage (INT 10h AX=1130h
	// BH=06h: ES:BP).
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x1130;
	r.h.bh = 0x06;
	__dpmi_int(0x10, &r);
	if (r.x.es || r.x.bp) {
		dosmemget(r.x.es * 16 + r.x.bp, sizeof(g_font), g_font);
		g_haveFont = true;
	}

	g_asciiBar = doubleByteScreen();
	g_stage = kStageText;
	drawTextScreen();
}

Stage stage() {
	return g_stage;
}

void backendReady(Common::TimerManager *timers, const Common::String &title, bool korean, bool enabled) {
	g_sdlOwnsScreen = true;
	if (g_stage != kStageText)
		return;
	if (!enabled) {
		// As if it had never started: a clear screen, the cursor back.
		g_stage = kStageOff;
		__dpmi_regs r;
		memset(&r, 0, sizeof(r));
		r.x.ax = 0x0003;
		__dpmi_int(0x10, &r);
		return;
	}
	g_title = title;
	g_korean = korean;
	// printf() output (base/main.cpp says which target it runs) would land
	// on the text screen, and later on the graphics one: to NUL until exit.
	fflush(stdout);
	const int nul = open("NUL", O_WRONLY);
	if (nul >= 0) {
		g_savedStdout = dup(1);
		dup2(nul, 1);
		close(nul);
	}
	g_progress.enter(kLoadDetect, now());
	const uint32 flags = cli();
	drawTextName();
	drawTextLabel();
	drawTextBar();
	restoreFlags(flags);
	g_timers = timers;
	g_timerOn = timers && timers->installTimerProc(textTick, 50000, nullptr, "dosLoadingText");
}

static void stopTimer() {
	if (g_timerOn) {
		g_timers->removeTimerProc(textTick);
		g_timerOn = false;
	}
}

void enter(LoadPhase phase) {
	if (g_stage == kStageOff)
		return;
	const uint32 flags = cli();
	const LoadPhase before = g_progress.phase();
	g_progress.enter(phase, now());
	if (g_stage == kStageText && !g_halted && g_progress.phase() != before) {
		drawTextLabel();
		drawTextBar();
	}
	restoreFlags(flags);
}

void setTitle(const Common::String &title) {
	if (g_stage == kStageOff || title.empty())
		return;
	g_title = title;
	if (g_stage == kStageText && !g_halted) {
		const uint32 flags = cli();
		drawTextName();
		restoreFlags(flags);
	}
}

void addBytes(uint32 n) {
	if (g_stage == kStageOff)
		return;
	g_progress.addBytes(n);
	if (g_stage == kStageGraphics && g_hook && !g_inHook && !DosTimerManager::inHandler() &&
		DosTimerManager::interruptsEnabled()) {
		g_inHook = true;
		g_hook(g_hookParam);
		g_inHook = false;
	}
}

void enterGraphics() {
	if (g_stage != kStageText)
		return;
	stopTimer();
	g_stage = kStageGraphics;
	g_graphicsMs = now();
	enter(kLoadInit);
}

void finish(const char *why) {
	if (g_stage == kStageOff)
		return;
	stopTimer();
	const uint32 t = now();
	const bool graphics = g_stage == kStageGraphics;
	g_progress.enter(kLoadDone, t);
	g_stage = kStageOff;
	g_halted = false;
	g_hook = nullptr;
	if (g_system)
		g_system->logMessage(LogMessageType::kInfo, Common::String::format(
			"DOS: loading screen: game frame %u ms after main (text %u ms, graphics %u ms, %s)\n",
			(uint)t, (uint)(graphics ? g_graphicsMs : t), (uint)(graphics ? t - g_graphicsMs : 0), why).c_str());
}

static char g_lastWarning[80];
static char g_haltReason[80];
static const char kHaltWhere[] = "See SCUMMVM.LOG. Enter continues.";

void noteWarning(const char *message) {
	// The GUI's own complaints come as a dialog is built, just before it
	// fails to show; they would hide the warning that says what went wrong.
	static const char *const kNoise[] = { "theme", "gui-icons", "translations.dat", "GUI overlay", "hardware input", nullptr };
	if (g_stage == kStageOff)
		return;
	for (int i = 0; kNoise[i]; ++i)
		if (strstr(message, kNoise[i]))
			return;
	if (!strncmp(message, "WARNING: ", 9))
		message += 9;
	uint n = 0;
	for (; message[n] && message[n] != '\n' && n < sizeof(g_lastWarning) - 1; ++n) {
		const byte c = (byte)message[n];
		g_lastWarning[n] = (c < 0x20 || c >= 0x7F) ? '?' : (char)c;
	}
	g_lastWarning[n] = '\0';
}

void halt(const char *reason) {
	if (g_stage == kStageOff || g_halted)
		return;
	const char *why = reason ? reason : g_lastWarning[0] ? g_lastWarning : "A message could not be shown (no GUI yet).";
	snprintf(g_haltReason, sizeof(g_haltReason), "Stopped: %s", why);
	g_halted = true;
	if (g_stage != kStageText)
		return;
	const uint32 flags = cli();
	textRow(kRowLabel, "Stopped.", 0x0C);
	textRow(kRowHalt, g_haltReason, 0x0C);
	textRow(kRowHalt + 1, kHaltWhere, 0x0C);
	restoreFlags(flags);
}

bool halted() {
	return g_stage != kStageOff && g_halted;
}

const char *haltLine(int line) {
	return line == 0 ? g_haltReason : kHaltWhere;
}

void stopTextUpdates() {
	stopTimer();
}

void teardown() {
	// Not removeTimerProc(): on the way out the timer manager may be gone
	// already (OSystem_DOS's destructor). With the stage off the proc
	// draws nothing, should it still run.
	const bool text = g_stage == kStageText;
	g_stage = kStageOff;
	g_timerOn = false;
	if (g_savedStdout >= 0) {
		fflush(stdout);
		dup2(g_savedStdout, 1);
		close(g_savedStdout);
		g_savedStdout = -1;
	}
	if (text && !g_sdlOwnsScreen) {
		// Out before SDL came up: nothing will set the text mode again. The
		// cursor back, under what was shown.
		__dpmi_regs r;
		memset(&r, 0, sizeof(r));
		r.h.ah = 0x01;
		r.x.cx = 0x0607;
		__dpmi_int(0x10, &r);
	}
}

uint16 permille() {
	return g_progress.value(now());
}

const char *phaseLabel() {
	return kLoadPhases[g_progress.phase()].label;
}

const Common::String &title() {
	return g_title;
}

bool korean() {
	return g_korean;
}

const byte *romFont() {
	return g_haveFont ? g_font : nullptr;
}

void setTickHook(void (*hook)(void *), void *param) {
	g_hook = hook;
	g_hookParam = param;
}

} // End of namespace Loading
} // End of namespace DOS

// Every read from a file goes through DJGPP's _read() (fread() fills its
// buffer with it): linked with --wrap=_read (module.mk), the loading
// screen counts the bytes and gets a chance to move its bar during long
// reads.
extern "C" ssize_t __real__read(int fd, void *buf, size_t n);
extern "C" ssize_t __wrap__read(int fd, void *buf, size_t n) {
	const ssize_t got = __real__read(fd, buf, n);
	if (got > 0 && DOS::Loading::g_stage != DOS::Loading::kStageOff)
		DOS::Loading::addBytes((uint32)got);
	return got;
}

#endif
