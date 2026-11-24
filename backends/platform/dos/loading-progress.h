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

#ifndef BACKENDS_PLATFORM_DOS_LOADING_PROGRESS_H
#define BACKENDS_PLATFORM_DOS_LOADING_PROGRESS_H

#include "common/scummsys.h"
#include "common/util.h"

namespace DOS {

/**
 * The steps of a game's start the backend sees for itself, in order: main()
 * (kLoadStart), the end of initBackend() (kLoadDetect: base/main.cpp now
 * identifies the game), the game's window caption (kLoadEngine: runGame()
 * sets it once the engine is created), OSystem::engineInit() (kLoadData:
 * Engine::run() is next), the engine's first graphics mode (kLoadInit), the
 * first updateScreen() after that (kLoadFirstFrame) and the first frame
 * with something on it (kLoadDone).
 */
enum LoadPhase {
	kLoadStart,
	kLoadDetect,
	kLoadEngine,
	kLoadData,
	kLoadInit,
	kLoadFirstFrame,
	kLoadDone,
	kLoadPhaseCount
};

/**
 * Where each phase starts on the bar (per mille), and how long it usually
 * lasts (ms). Calibrated on the Task A profile (DOSBox-X, 60000 cycles,
 * main() to the first visible frame): KQ1 2.44 s, KQ1KOL 3.26 s, KQ1KO
 * 3.99 s; initBackend() ends at 2% of that, the engine exists at 10-13%,
 * the mode is set at 22-28%, the first updateScreen() comes at 59-67%.
 */
struct LoadPhaseSpec {
	uint16 from;	///< per mille of the bar where the phase starts
	uint16 expectMs;	///< its usual length
	const char *label;
};

static const LoadPhaseSpec kLoadPhases[kLoadPhaseCount] = {
	{    0,   50, "Starting" },
	{   20,  300, "Detecting the game" },
	{  120,   20, "Starting the engine" },
	{  130,  350, "Loading game data" },
	{  240, 1200, "Initializing the game" },
	{  620, 1200, "Drawing the first screen" },
	{ 1000,    0, "Done" }
};

/** Bytes read that count as one millisecond of a phase's work. */
static const uint32 kLoadBytesPerMs = 16384;

/** The highest the bar goes before the game's first frame. */
static const uint16 kLoadMaxBeforeDone = 990;

/**
 * The bar's position (per mille) @p elapsedMs into @p phase, with @p bytes
 * read in it: the phase's share of the bar times t / (t + tau), where t is
 * the elapsed time plus the bytes' worth and tau a third of the phase's
 * usual length -- three quarters of the way at the usual length, still
 * moving (ever slower) when a slow machine takes longer, never reaching
 * the next phase's start. The next milestone then takes the bar there.
 */
inline uint16 loadPermille(LoadPhase phase, uint32 elapsedMs, uint32 bytes) {
	if (phase >= kLoadDone)
		return 1000;
	const uint32 from = kLoadPhases[phase].from;
	const uint32 to = kLoadPhases[phase + 1].from;
	const uint32 tau = MAX<uint32>(1, kLoadPhases[phase].expectMs / 3);
	// Capped so the product below stays in 32 bits: at t = 1000 * tau
	// the bar is within 0.1% of the phase's end anyway.
	const uint32 t = MIN<uint32>(elapsedMs + bytes / kLoadBytesPerMs, 1000 * tau);
	const uint32 v = from + (to - from) * t / (t + tau);
	return (uint16)MIN<uint32>(v, kLoadMaxBeforeDone);
}

/**
 * The bar's state: the phase it is in, since when, and the bytes read in
 * it. Milestones only move forward; value() never goes back, whatever
 * order the calls come in.
 */
class LoadProgress {
public:
	LoadProgress() : _phase(kLoadStart), _since(0), _bytes(0), _shown(0) {}

	void reset(uint32 now) {
		_phase = kLoadStart;
		_since = now;
		_bytes = 0;
		_shown = 0;
	}

	/** Moves on to @p phase at @p now, unless the bar is there or past it already. */
	void enter(LoadPhase phase, uint32 now) {
		if (phase <= _phase)
			return;
		_phase = phase;
		_since = now;
		_bytes = 0;
	}

	void addBytes(uint32 n) { _bytes += n; }

	LoadPhase phase() const { return _phase; }
	uint32 since() const { return _since; }

	/** The bar at @p now, per mille; never less than it was last time. */
	uint16 value(uint32 now) {
		const uint16 v = loadPermille(_phase, now - _since, _bytes);
		if (v > _shown)
			_shown = v;
		return _shown;
	}

private:
	LoadPhase _phase;
	uint32 _since;
	uint32 _bytes;
	uint16 _shown;
};

} // End of namespace DOS

#endif
