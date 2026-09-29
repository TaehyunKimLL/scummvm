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

#ifndef BACKENDS_PLATFORM_DOS_DOS_MEMORY_H
#define BACKENDS_PLATFORM_DOS_DOS_MEMORY_H

// Pure formatting, no <dpmi.h>: linked into the portable unit test build
// (test/backends/dos_memory.h) as well as the DOS backend.
#include "common/scummsys.h"
#include "common/str.h"

namespace DOS {

/** A DPMI memory snapshot, everything already converted to KB. */
struct MemInfo {
	uint32 freeKB;		///< Memory DPMI could still hand out.
	uint32 largestKB;	///< The largest single free block.
	uint32 physFreeKB;	///< Free physical (installed) memory.
	uint32 physTotalKB;	///< Total physical (installed) memory.
};

/**
 * Converts one __dpmi_get_free_memory_information() reading to KB.
 * @p freeBytes and @p largestBytes already come from the DPMI host in
 * bytes; @p physFreePages and @p physTotalPages come in the call's other
 * unit, 4 KB pages. A host that does not track a value returns DPMI's
 * "not supported" sentinel, all bits set (0xFFFFFFFF) -- that becomes 0
 * here in whichever field carries it, rather than reporting ~4 TB.
 */
inline MemInfo memFromPages(uint32 freeBytes, uint32 largestBytes, uint32 physFreePages, uint32 physTotalPages) {
	MemInfo m;
	m.freeKB = (freeBytes == 0xFFFFFFFF) ? 0 : freeBytes / 1024;
	m.largestKB = (largestBytes == 0xFFFFFFFF) ? 0 : largestBytes / 1024;
	m.physFreeKB = (physFreePages == 0xFFFFFFFF) ? 0 : physFreePages * 4;
	m.physTotalKB = (physTotalPages == 0xFFFFFFFF) ? 0 : physTotalPages * 4;
	return m;
}

/**
 * "DOS: memory <phase> dpmi_free=<KB> largest=<KB> phys_free=<KB> phys_total=<KB>"
 * @p phase is free text (dos.cpp logs "engine", "first-frame" and "quit";
 * the debug socket's `mem` command uses "now" and sends only the values,
 * see gui/debugsocket.cpp).
 */
inline Common::String formatMemInfo(const char *phase, const MemInfo &m) {
	return Common::String::format(
		"DOS: memory %s dpmi_free=%u largest=%u phys_free=%u phys_total=%u",
		phase, m.freeKB, m.largestKB, m.physFreeKB, m.physTotalKB);
}

}

#endif
