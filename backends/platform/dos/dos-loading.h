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

#ifndef BACKENDS_PLATFORM_DOS_DOS_LOADING_H
#define BACKENDS_PLATFORM_DOS_DOS_LOADING_H

#include "backends/platform/dos/loading-progress.h"
#include "common/str.h"

namespace Common {
class TimerManager;
}

namespace DOS {

/**
 * The loading screen, from main() to the game's first frame, in two
 * stages. kLoadText: an 80x25 text screen (written straight to text
 * memory at B800h, English only) with the game's name, a bar and the
 * phase, redrawn from a 1 kHz IRQ0 timer proc so the bar moves while the
 * main thread detects the game and starts the engine. The graphics mode
 * waits for the engine's own (DosGraphicsManager). kLoadGraphics: from
 * that mode on, DosGraphicsManager draws the same in the game's mode (with
 * a Korean caption for a Korean game) and shows it instead of the game's
 * frames until one of them has something on it.
 *
 * Only for a run that starts a game (a target on the command line); off
 * with dos_loading_screen=false. Main thread only, except where noted.
 */
namespace Loading {

enum Stage {
	kStageOff,
	kStageText,
	kStageGraphics
};

/** From main(), first thing: shows the text screen when @p argv names a game. */
void start(int argc, char *argv[]);

Stage stage();

/** Milliseconds since start(), from the same clock as getMillis(). ISR-safe. */
uint32 now();

/**
 * From initBackend(), once the config is read and the timer is in: the
 * game's name, output from printf() away from the screen, the IRQ0
 * redraws on. @p enabled false (dos_loading_screen=false) takes the text
 * screen down instead.
 */
void backendReady(Common::TimerManager *timers, const Common::String &title, bool korean, bool enabled);

/** A milestone (see LoadPhase). */
void enter(LoadPhase phase);

/** A better name for the game, e.g. from the window caption. */
void setTitle(const Common::String &title);

/** @p n bytes read from a file (the backend's read hook). Any context. */
void addBytes(uint32 n);

/**
 * The graphics stage starts: the IRQ0 redraws stop (text memory is about
 * to go away with the mode change).
 */
void enterGraphics();

/** The game's first frame is up: logs how long it all took. */
void finish(const char *why);

/**
 * The text screen stops where it is and says why, e.g. a GUI dialog that
 * cannot be shown before the overlay works (M4).
 */
void halt(const char *message);

/** Puts back what start() and backendReady() took (stdout, the cursor). Idempotent. */
void teardown();

/** The bar now, per mille. */
uint16 permille();
const char *phaseLabel();
const Common::String &title();
bool korean();
/** The VGA BIOS's 8x16 font (256 x 16 bytes), read at start(); nullptr without one. */
const byte *romFont();

/**
 * Called from addBytes() (with interrupts on, outside the IRQ0 handler,
 * not recursively) in the graphics stage, so the bar moves during long
 * reads: DosGraphicsManager redraws it there.
 */
void setTickHook(void (*hook)(void *), void *param);

} // End of namespace Loading

} // End of namespace DOS

#endif
