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

	void quit() override;
	void fatalError() override;

	void logMessage(LogMessageType::Type type, const char *message) override;
	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

private:
	/** dos_timer_selftest=true: checks the preemptive timer (see dos.cpp). */
	void timerSelftest();
	/** dos_mixer_selftest=true: checks the Sound Blaster mixer (see dos.cpp). */
	void mixerSelftest();

	Common::EventSource *_eventSource;	///< a DosEventSource
	NullMixerManager *_nullMixer;	///< _mixerManager, when there is no audio device
};

#endif
