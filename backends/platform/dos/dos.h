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

#ifndef BACKENDS_PLATFORM_DOS_DOS_H
#define BACKENDS_PLATFORM_DOS_DOS_H

#include "backends/modular-backend.h"
#include "common/events.h"

class NullMixerManager;

namespace DOS {

/**
 * From main(): records argv[0]'s base name (path and case stripped, e.g.
 * "SCUMM.EXE") for exeName(). Left at the default ("SCI.EXE") if
 * argv[0] is missing or empty.
 */
void setExeName(const char *argv0);

/** The name set by setExeName(), or "SCI.EXE" before main() calls it. */
const char *exeName();

/**
 * Reads __dpmi_get_free_memory_information() now and logs it as
 * formatMemInfo(phase, ...) (dos-memory.h). @p phase is free text: dos.cpp
 * uses "engine" and "quit", the graphics manager "first-frame".
 */
void logMemInfo(const char *phase);

/**
 * dos_pagefault_selftest (dos.cpp): a timer proc that touches a pageable
 * buffer of @p kb KB, a random page per call at @p hz, and counts the page faults
 * it takes by where it ran. Poll() from the event loop (logs every 10 s),
 * Log() with the memory lines, Stop() before SDL closes its audio.
 */
void pagefaultSelftestStart(int kb, int hz);
void pagefaultSelftestPoll();
void pagefaultSelftestLog();
void pagefaultSelftestStop();

}

/**
 * MS-DOS through DJGPP. SDL3 opens the hardware (VESA modes, keyboard,
 * mouse); everything above that is ScummVM's. There are no threads: timers
 * run from a 1 kHz IRQ0 handler (DosTimerManager), a mutex holds interrupts
 * off, and SDL3's cooperative scheduler runs in delayMillis().
 */
class OSystem_DOS : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_DOS();
	~OSystem_DOS() override;

	void initBackend() override;

	bool pollEvent(Common::Event &event) override;

	Common::MutexInternal *createMutex() override;
	uint32 getMillis(bool skipRecord = false) override;
	void delayMillis(uint msecs) override;
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	void engineInit() override;
	void engineDone() override;
	void setWindowCaption(const Common::U32String &caption) override;

	void quit() override;
	void fatalError() override;

	void logMessage(LogMessageType::Type type, const char *message) override;
	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

private:
	/** dos_timer_selftest=true: checks the preemptive timer (see dos.cpp). */
	void timerSelftest();
	/** dos_mixer_selftest=true: checks the Sound Blaster mixer (see dos.cpp). */
	void mixerSelftest();
	/** Logs the mixer's counters (the debug socket's `audio`) as "DOS: audio <phase> ..." */
	void logAudioStats(const char *phase);

	Common::EventSource *_eventSource;	///< a DosEventSource
	NullMixerManager *_nullMixer;	///< _mixerManager, when there is no audio device
	uint32 _statsLogMs;	///< dos_stats_log in ms; 0 logs only at quit
	uint32 _statsNextMs;	///< getMillis() of the next periodic reading
};

#endif
