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
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <crt0.h>
#include <dpmi.h>
#include <go32.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/nearptr.h>
#include <unistd.h>

#include "backends/platform/dos/dos-heap.h"
#include "backends/platform/dos/dos-irq.h"
#include "common/debug.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/util.h"

// The end of .text (the link script's etext): code and read-only data.
extern "C" char etext[];

// SDL3's Sound Blaster and keyboard handlers must lock their own code the
// same way, check that it lies in its range and that the locks took
// (DOS_IRQ_CODE and DOS_LockIRQCode in SDL_dos.h: sdl3-irq-code.patch on
// SDL3 1ce4c5b); without it the Sound Blaster's is not locked at all.
// Referencing this fails the link against an SDL3 that lacks it
// (build-dos.sh says so before it gets that far).
extern "C" const int DOS_IRQCodeChecked;
// Likewise sdl3-cpuid.patch's mark: an SDL3 without it runs CPUID on a 486
// that has none (SDL_Init() dies with an invalid opcode).
extern "C" const int DOS_CPUIDChecked;
// And sdl3-sb-shutdown.patch's: without it a Sound Blaster whose interrupts
// never come (a wrong DMA channel in BLASTER) hangs the exit.
extern "C" const int DOS_SBShutdownChecked;
// And sdl3-sb-open-fail.patch's: without it a Sound Blaster that cannot be
// opened (an SB16 whose BLASTER has no H) kills the program with an exception.
extern "C" const int DOS_SBOpenFailChecked;

namespace DOS {

namespace {

// The first page holds no code: CWSDPMI leaves page 0 uncommitted to
// catch null pointers, and .text starts in the next.
const uintptr kImageStart = 0x1000;
const uint32 kPage = 4096;

bool g_lockedAll = false;
bool g_lockedAllComplete = false;
const char *g_lockedAllWhy = "";
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

/*
 * Pageable memory is used only under CWSDPMI r7 or later at ring 3; the
 * safety argument rests on its source (csdpmi7s.zip, src/cwsdpmi/):
 *
 * - Which page faults it services: page_in_user() (exphdlr.c:337-344)
 *   refuses one only while in_rmcb is set, and in_rmcb is set exactly while
 *   a real-mode callback runs (rmcb_common, dpmisim.asm:203-206 and :259-261).
 *   A hardware interrupt that comes in real mode reaches a protected-mode
 *   handler through such a callback (exphdlr.c:44-58); one that comes in
 *   protected mode goes through irq_common (tables.asm:239-295) with no
 *   callback, so a page fault in its handler is serviced like any other.
 *
 * - CWSDPMI's own protected-mode code runs at ring 0 with interrupts off
 *   (exphdlr.c:36-41; irq_common relies on it, tables.asm:148-153). It
 *   services a page fault from its real-mode side (go_til_stop() and
 *   exception_handler(), utils.c:26-38), where the disk I/O may turn
 *   interrupts on; an interrupt then comes in real mode, through a
 *   callback, and DosTimerManager runs no timer procs in it.
 *
 * - The frame an interrupt from protected mode hands the handler is
 *   irq_common's: an IRET into _user_interrupt_return (EIP, CS g_pcode =
 *   0x2B, EFLAGS 0x3002: interrupts off) over the ring 3 frame the CPU
 *   pushed (tables.asm:266-276, gdt.inc:31). One through a callback has
 *   rmcb_task's far call and its saved registers there instead
 *   (dpmisim.asm:282-299). dos-timer.cpp reads and calibrates that frame.
 *
 * - DPMI 0x401 gives "\7\0CWSDPMI": version 7.0 (exphdlr.c:1155-1159).
 *   CWSDPR0, the ring 0 build of the same source, says the same
 *   (makefile:3-14) but neither pages nor switches stacks, so it is told
 *   apart by our code's privilege level. Earlier releases are not checked
 *   against; nor is any other host. All of those lock everything.
 */
void chooseLockRegime() {
	(void)*(const volatile int *)&DOS_IRQCodeChecked;
	(void)*(const volatile int *)&DOS_CPUIDChecked;
	(void)*(const volatile int *)&DOS_SBShutdownChecked;
	(void)*(const volatile int *)&DOS_SBOpenFailChecked;
	int flags = 0;
	char vendor[128];
	memset(vendor, 0, sizeof(vendor));
	const bool known = __dpmi_get_capabilities(&flags, vendor) == 0;
	if (known) {
		vendor[sizeof(vendor) - 1] = 0;
		Common::strlcpy(g_host, Common::String::format("%s %d.%d", vendor + 2, vendor[0], vendor[1]).c_str(), sizeof(g_host));
	}
	// SCUMMVM_DOS_LOCKTEST=all: as under any other host (to test that
	// regime under CWSDPMI); =calibration: see dos-timer.cpp.
	const char *test = getenv("SCUMMVM_DOS_LOCKTEST");
	if (test && strcmp(test, "all") == 0) {
		lockAll("SCUMMVM_DOS_LOCKTEST=all");
		return;
	}
	if (!known || strcmp(vendor + 2, "CWSDPMI") != 0) {
		lockAll("the DPMI host is not CWSDPMI");
		return;
	}
	if ((uint8)vendor[0] < 7) {
		lockAll("CWSDPMI before r7");
		return;
	}
	if ((_go32_my_cs() & 3) != 3) {
		lockAll("CWSDPMI at ring 0 (CWSDPR0)");
		return;
	}
}

bool lockAll(const char *why) {
	if (g_lockedAll)
		return g_lockedAllComplete;
	g_lockedAll = true;
	g_lockedAllWhy = why;
	// As crt0's _CRT0_FLAG_LOCK_MEMORY would: the image, the stack, the
	// heap so far (all below the break: NONMOVE_SBRK) and every later
	// sbrk(). The large blocks lie outside sbrk(): dos-heap.cpp locks
	// those there are and every later one.
	_crt0_startup_flags |= _CRT0_FLAG_LOCK_MEMORY;
	const uintptr top = (uintptr)sbrk(0);
	// Block by block, as crt0's non-move sbrk() got them from DPMI: the
	// heap's blocks may lie apart, and a lock across a gap fails under
	// most hosts -- and under CWSDPMI r7 stops at the gap and reports
	// success (paging.c lock_memory(): its loop ends at the first address
	// outside an area), leaving the rest unlocked.
	uint32 lockedBytes = 0, failedBytes = 0, blocks = 0;
	for (uint i = 0; i < ARRAYSIZE(__djgpp_memory_handle_list); ++i) {
		const uintptr a = __djgpp_memory_handle_list[i].address;
		const uintptr size = __djgpp_memory_handle_size[i];
		if ((i > 1 && !a) || !size)
			continue;
		const uintptr b = MAX<uintptr>(a, kImageStart), e = MIN<uintptr>(a + size, top);
		if (b >= e)
			continue;
		blocks++;
		if (lockLinear(b, e - b))
			lockedBytes += e - b;
		else
			failedBytes += e - b;
	}
	if (!blocks) {
		// No block list to go by: the whole range, else page by page,
		// where a page that is no one's fails as well as one that could
		// not be locked; neither is counted.
		if (lockLinear(kImageStart, top - kImageStart)) {
			lockedBytes = top - kImageStart;
		} else {
			for (uintptr a = kImageStart; a < top; a += kPage)
				if (lockLinear(a, kPage))
					lockedBytes += kPage;
		}
	}
	uint32 largeBlocks, largeBytes;
	const bool largeOk = dosHeapLockLargeBlocks(largeBlocks, largeBytes);
	debug(1, "DOS: all memory locked (%s): %u KB below the break in %u blocks, %u large blocks of %u KB",
		why, (uint)(lockedBytes / 1024), (uint)blocks, (uint)largeBlocks, (uint)(largeBytes / 1024));
	g_lockedAllComplete = failedBytes == 0 && largeOk;
	if (!g_lockedAllComplete) {
		// Nothing gained by locking later memory: it would only fail the
		// sbrk() or large block that needs it.
		_crt0_startup_flags &= ~_CRT0_FLAG_LOCK_MEMORY;
		dosHeapStopLockingLargeBlocks();
		warning("DOS: could not lock all memory (%u KB below the break%s); timer procs run on the main thread",
			(uint)(failedBytes / 1024), largeOk ? "" : ", some large blocks");
	}
	return g_lockedAllComplete;
}

bool lockedAll() {
	return g_lockedAll;
}

bool lockedAllComplete() {
	return g_lockedAll && g_lockedAllComplete;
}

bool haveTsc() {
	// CPUID (EFLAGS.ID toggles), then leaf 1's EDX bit 4.
	uint32 a, b;
	__asm__ __volatile__("pushfl; popl %0; movl %0, %1; xorl $0x200000, %0; pushl %0; popfl; pushfl; popl %0; pushl %1; popfl"
		: "=&r"(a), "=&r"(b) : : "cc");
	if (((a ^ b) & 0x200000) == 0)
		return false;
	uint32 eax = 1, ebx, ecx, edx;
	__asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
	return (edx & 0x10) != 0;
}

// crt0's FPU type, declared in <dos.h> (which clashes with forbidden.h).
extern "C" int _8087;

bool fpuEmulated() {
	// crt0's _npxsetup(): 3 with an x87, 0 without one (or with 387=N).
	return _8087 == 0;
}

bool fpuUsable() {
	if (!fpuEmulated())
		return true;
	// Without an FPU, _npxsetup() installs the emulator as the SIGNOFP
	// handler once the DPMI host has agreed to trap FPU instructions (and,
	// were libc's _npxsetup() linked instead of libemu's, once it had
	// loaded EMU387.DXE). Otherwise the handler stays the default, and the
	// first FPU instruction ends the program.
	void (*const handler)(int) = signal(SIGNOFP, SIG_DFL);
	signal(SIGNOFP, handler);
	return handler != SIG_DFL && handler != SIG_ERR;
}

const char *lockedAllWhy() {
	return g_lockedAllWhy;
}

const char *dpmiHost() {
	return g_host;
}

} // End of namespace DOS

#endif
