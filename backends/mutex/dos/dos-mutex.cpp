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

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include "backends/mutex/dos/dos-mutex.h"

// Shared by every DosMutexInternal (see the header). Only touched with
// interrupts off, so neither needs to be volatile or atomic.
static uint32 g_lockDepth = 0;
static uint32 g_savedFlags = 0;

bool DosMutexInternal::lock() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	if (g_lockDepth++ == 0)
		g_savedFlags = flags;
	return true;
}

bool DosMutexInternal::unlock() {
	if (g_lockDepth == 0)
		return false;
	if (--g_lockDepth == 0) {
		// popfl cannot set IF under a DPMI host that runs us with IOPL < CPL (a Windows 9x DOS box, NTVDM, EMM386):
		// the instruction is ignored there and interrupts stayed off for good. sti is the way the other IRQ locks
		// (dos-timer.cpp) turn them on again, and only when the caller had them on.
		if (g_savedFlags & 0x200)
			__asm__ __volatile__("sti" : : : "memory");
	}
	return true;
}

#endif
