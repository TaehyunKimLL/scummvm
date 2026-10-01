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

#define FORBIDDEN_SYMBOL_EXCEPTION_unistd_h

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <crt0.h>
#include <dpmi.h>
#include <go32.h>
#include <string.h>
#include <sys/nearptr.h>
#include <unistd.h>

#include "backends/platform/dos/dos-irq.h"
#include "common/str.h"
#include "common/textconsole.h"

// The end of .text (the link script's etext): code and read-only data.
extern "C" char etext[];

// SDL3's Sound Blaster and keyboard handlers must lock their own code the
// same way (DOS_IRQ_CODE in SDL_dos.h, sdl3-irq-code.patch on SDL3
// 1ce4c5b); without it the Sound Blaster's is not locked at all.
// Referencing this fails the link against an SDL3 that lacks it.
extern "C" const int DOS_IRQCodeLocked;

namespace DOS {

namespace {

// The first page holds no code: CWSDPMI leaves page 0 uncommitted to
// catch null pointers, and .text starts in the next.
const uintptr kImageStart = 0x1000;
const uint32 kPage = 4096;

bool g_lockedAll = false;
char g_host[48] = "DPMI 0.9";

bool lockLinear(uintptr offset, uint32 size) {
	__dpmi_meminfo m;
	m.handle = 0;
	m.address = __djgpp_base_address + offset;
	m.size = size;
	return __dpmi_lock_linear_region(&m) == 0;
}

void lockText() {
	if (!lockLinear(kImageStart, (uintptr)etext - kImageStart))
		warning("DOS: could not lock the program's code");
}

} // End of anonymous namespace

void lockIrqCode(const char *begin, const char *end, const void *const *fns, uint count, const char *what) {
	bool inside = begin < end && end - begin < 64 * 1024;
	for (uint i = 0; inside && i < count; ++i)
		inside = (const char *)fns[i] > begin && (const char *)fns[i] < end;
	if (!inside) {
		warning("DOS: %s's interrupt code is not where it should be; locking all code", what);
		lockText();
		return;
	}
	if (_go32_dpmi_lock_code(const_cast<char *>(begin), end - begin + 1) != 0)
		warning("DOS: could not lock %s's interrupt code", what);
}

bool lockIrqData(const volatile void *p, uint32 size) {
	if (_go32_dpmi_lock_data(const_cast<void *>(p), size) == 0)
		return true;
	warning("DOS: could not lock %u bytes of interrupt data", (uint)size);
	return false;
}

void chooseLockRegime() {
	(void)*(const volatile int *)&DOS_IRQCodeLocked;
	int flags = 0;
	char vendor[128];
	memset(vendor, 0, sizeof(vendor));
	const bool known = __dpmi_get_capabilities(&flags, vendor) == 0;
	if (known) {
		vendor[sizeof(vendor) - 1] = 0;
		Common::strlcpy(g_host, Common::String::format("%s %d.%d", vendor + 2, vendor[0], vendor[1]).c_str(), sizeof(g_host));
	}
	// CWSDPMI (fact-checked on r7) services a page fault in a hardware
	// interrupt that came in protected mode; DosTimerManager runs the
	// timer procs only then. Other hosts are not known to: there, as
	// crt0's _CRT0_FLAG_LOCK_MEMORY would, lock the image, the stack, the
	// heap so far (all below the break: NONMOVE_SBRK) and every later
	// sbrk().
	if (known && strcmp(vendor + 2, "CWSDPMI") == 0)
		return;
	g_lockedAll = true;
	_crt0_startup_flags |= _CRT0_FLAG_LOCK_MEMORY;
	const uintptr top = (uintptr)sbrk(0);
	if (lockLinear(kImageStart, top - kImageStart))
		return;
	// Blocks of the heap may lie apart; lock what is there.
	for (uintptr a = kImageStart; a < top; a += kPage)
		lockLinear(a, kPage);
}

bool lockedAll() {
	return g_lockedAll;
}

const char *dpmiHost() {
	return g_host;
}

} // End of namespace DOS

#endif
