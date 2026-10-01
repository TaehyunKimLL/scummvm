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

#ifndef BACKENDS_PLATFORM_DOS_DOS_HEAP_H
#define BACKENDS_PLATFORM_DOS_DOS_HEAP_H

#include <stddef.h>

#include "common/scummsys.h"

/**
 * From now on, allocations of 256 KB or more come from DPMI blocks of
 * their own instead of the sbrk heap (see dos-heap.cpp). Needs the near
 * pointer enabled.
 */
void dosHeapEnableLargeBlocks();

/**
 * Whether @p size bytes at @p ptr lie inside one of those large blocks
 * (possibly at an offset into it, as SDL's aligned allocations are). Only
 * such memory may be locked and unlocked on its own: in the rest of the
 * heap an unlock of a region would also unlock the pages it shares with
 * its neighbours, which an interrupt handler may have locked (or, whenever
 * the heap is locked as a whole, DOS::lockAll(), everything is).
 */
bool dosHeapInLargeBlock(const void *ptr, size_t size);

/**
 * Locks every large block there is, and from now on each new one as it is
 * allocated: for DOS::lockAll(), which locks the sbrk heap as a whole.
 * Call it before any timer proc can run. Returns false if a lock failed;
 * @p blocks and @p bytes say what was locked.
 */
bool dosHeapLockLargeBlocks(uint32 &blocks, uint32 &bytes);

#endif
