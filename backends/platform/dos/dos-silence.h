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

#ifndef BACKENDS_PLATFORM_DOS_DOS_SILENCE_H
#define BACKENDS_PLATFORM_DOS_DOS_SILENCE_H

namespace DOS {

/**
 * Silences a synthesizer the program drives directly (an OPL chip, a
 * MIDI device on the MPU-401). Called with interrupts possibly off, after
 * the timer is out: port I/O only.
 */
typedef void (*SilenceProc)(void *param);

/**
 * Registers @p proc to run when the program exits through quit() or
 * fatalError(), which skip the engine's own shutdown and would leave notes
 * sounding. A driver registers on open and unregisters on close. Main
 * thread only. At most a few at a time; extra ones are ignored.
 */
void addSilencer(SilenceProc proc, void *param);

/** Undoes addSilencer(); nothing happens if the pair is not registered. */
void removeSilencer(SilenceProc proc, void *param);

/** Runs every registered silencer once and forgets it (idempotent). */
void silenceAll();

} // End of namespace DOS

#endif
