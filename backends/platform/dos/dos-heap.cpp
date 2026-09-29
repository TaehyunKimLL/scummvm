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

#include <dpmi.h>
#include <string.h>
#include <sys/nearptr.h>

#include "backends/platform/dos/dos-heap.h"

/*
 * The heap, for interrupt handlers.
 *
 * The IRQ0 timer runs ScummVM's timer procs preemptively. They touch the
 * heap (SCI's music callback reads song data and empties its command
 * queue, freeing memory), so:
 *
 * - The heap must be locked: CWSDPMI cannot page in during a hardware
 *   interrupt. main() leaves _CRT0_FLAG_LOCK_MEMORY set, so all that
 *   sbrk() hands malloc() is locked.
 *
 * - malloc() must not be reentered: DJGPP's is not reentrant, and the code
 *   the interrupt came in on may be inside it. The link wraps malloc, free,
 *   realloc, calloc and memalign (-Wl,--wrap, see module.mk) so each runs
 *   with interrupts off.
 *
 * - Locked memory cannot exceed what the machine has. The hi-res text
 *   fonts alone cache several MB of glyphs, which on a 16 MB machine does
 *   not fit locked. Blocks of kLargeBlock bytes or more therefore come
 *   from DPMI memory blocks of their own, outside sbrk(), which stay
 *   pageable. Nothing a timer proc touches is that large (sound resources
 *   are a few KB; the mixer's buffers are not touched in the handler).
 *   They need the near pointer (main() enables it before anything else),
 *   so until dosHeapEnableLargeBlocks() every block comes from malloc.
 */

extern "C" {

void *__real_malloc(size_t size);
void __real_free(void *ptr);
void *__real_realloc(void *ptr, size_t size);
void *__real_calloc(size_t n, size_t size);
void *__real_memalign(size_t alignment, size_t size);

}

namespace {

const size_t kLargeBlock = 256 * 1024;

// In front of every large block; its address is page aligned, so the
// pointer handed out is 16 bytes into a page -- a cheap first test.
struct LargeHeader {
	uint32 magic;
	uint32 handle;
	uint32 size;		// usable bytes after the header
	uint32 check;		// magic ^ handle ^ size
};
const uint32 kMagic = 0x4C524745;	// "LRGE"
const uint32 kPage = 4096;

bool g_largeOk = false;

inline uint32 heapLock() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
	return flags;
}

inline void heapUnlock(uint32 flags) {
	if (flags & 0x200)
		__asm__ __volatile__("sti" : : : "memory");
}

LargeHeader *largeHeader(void *ptr) {
	if (!ptr || ((uintptr)ptr & (kPage - 1)) != sizeof(LargeHeader))
		return nullptr;
	LargeHeader *h = (LargeHeader *)ptr - 1;
	if (h->magic != kMagic || h->check != (kMagic ^ h->handle ^ h->size))
		return nullptr;
	return h;
}

// Interrupts are off.
void *largeAlloc(size_t size) {
	__dpmi_meminfo mi;
	mi.handle = 0;
	mi.address = 0;
	mi.size = (size + sizeof(LargeHeader) + kPage - 1) & ~(kPage - 1);
	if (__dpmi_allocate_memory(&mi) != 0)
		return nullptr;
	LargeHeader *h = (LargeHeader *)(mi.address + __djgpp_conventional_base);
	h->magic = kMagic;
	h->handle = mi.handle;
	h->size = mi.size - sizeof(LargeHeader);
	h->check = kMagic ^ h->handle ^ h->size;
	return h + 1;
}

// Interrupts are off.
void largeFree(LargeHeader *h) {
	const uint32 handle = h->handle;
	h->magic = 0;
	__dpmi_free_memory(handle);
}

} // End of anonymous namespace

void dosHeapEnableLargeBlocks() {
	g_largeOk = true;
}

extern "C" {

void *__wrap_malloc(size_t size) {
	const uint32 f = heapLock();
	void *p = nullptr;
	if (g_largeOk && size >= kLargeBlock)
		p = largeAlloc(size);
	if (!p)
		p = __real_malloc(size);
	heapUnlock(f);
	return p;
}

void __wrap_free(void *ptr) {
	const uint32 f = heapLock();
	if (LargeHeader *h = largeHeader(ptr))
		largeFree(h);
	else
		__real_free(ptr);
	heapUnlock(f);
}

void *__wrap_realloc(void *ptr, size_t size) {
	const uint32 f = heapLock();
	void *p;
	if (LargeHeader *h = largeHeader(ptr)) {
		if (size == 0) {
			largeFree(h);
			p = nullptr;
		} else if (size <= h->size) {
			p = ptr;
		} else {
			p = largeAlloc(size);
			if (!p)
				p = __real_malloc(size);
			if (p) {
				memcpy(p, ptr, h->size);
				largeFree(h);
			}
		}
	} else {
		// A small block that grows stays in the locked heap: its old size
		// is malloc's business.
		p = __real_realloc(ptr, size);
	}
	heapUnlock(f);
	return p;
}

void *__wrap_calloc(size_t n, size_t size) {
	const uint32 f = heapLock();
	void *p = nullptr;
	if (g_largeOk && size && n >= kLargeBlock / size) {
		p = largeAlloc(n * size);
		if (p)
			memset(p, 0, n * size);
	}
	if (!p)
		p = __real_calloc(n, size);
	heapUnlock(f);
	return p;
}

void *__wrap_memalign(size_t alignment, size_t size) {
	const uint32 f = heapLock();
	void *p = nullptr;
	if (g_largeOk && size >= kLargeBlock && alignment <= sizeof(LargeHeader))
		p = largeAlloc(size);
	if (!p)
		p = __real_memalign(alignment, size);
	heapUnlock(f);
	return p;
}

} // extern "C"

#endif
