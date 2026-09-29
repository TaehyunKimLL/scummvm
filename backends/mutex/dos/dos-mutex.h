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

#ifndef BACKENDS_MUTEX_DOS_H
#define BACKENDS_MUTEX_DOS_H

#include "common/mutex.h"

/**
 * DOS has one thread of control plus interrupt handlers (the timer's IRQ0
 * handler runs ScummVM's timer procs). Holding a mutex means holding
 * interrupts off: lock() is pushf + cli, the matching unlock() puts the
 * saved interrupt flag back (popf). A mutex is therefore never contended,
 * and lock() inside an interrupt handler, where IF is already 0, is free.
 *
 * Nesting is counted once for all mutexes, not per mutex: the first
 * lock() anywhere saves EFLAGS, the last unlock() restores it. That keeps
 * locks released out of order (A, B, unlock A, unlock B) from turning
 * interrupts back on while B is still held.
 */
class DosMutexInternal final : public Common::MutexInternal {
public:
	bool lock() override;
	bool unlock() override;
};

#endif
