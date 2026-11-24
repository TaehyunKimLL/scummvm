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


#ifndef BACKENDS_PLATFORM_DOS_EXIT_TRACE_H
#define BACKENDS_PLATFORM_DOS_EXIT_TRACE_H

#include "common/scummsys.h"

namespace DOS {

/**
 * The exit trace's line on the text screen (dos_exit_trace=true): the
 * last kMax step numbers in the order they were reached, each followed
 * by '.' once its line is in EXITLOG.TXT too. "X:" first, so it is told
 * apart from anything else on the screen.
 */
struct ExitTraceLine {
	static const int kCols = 80;
	static const int kMax = 19;		// "nn. " x 19 + "X:" fits 80 columns
	int steps[kMax];
	bool saved[kMax];
	int count;

	ExitTraceLine() : count(0) {}

	void add(int step) {
		if (count == kMax) {
			for (int i = 1; i < kMax; ++i) {
				steps[i - 1] = steps[i];
				saved[i - 1] = saved[i];
			}
			count--;
		}
		steps[count] = step;
		saved[count] = false;
		count++;
	}

	void markSaved() {
		if (count)
			saved[count - 1] = true;
	}

	/** Fills @p out (kCols characters, no terminator) padded with spaces. */
	void render(char *out) const {
		int col = 0;
		out[col++] = 'X';
		out[col++] = ':';
		for (int i = 0; i < count; ++i) {
			char digits[4];
			const int n = formatNumber(digits, steps[i]);
			if (col + n + 2 > kCols)
				break;
			for (int d = 0; d < n; ++d)
				out[col++] = digits[d];
			out[col++] = saved[i] ? '.' : ' ';
			out[col++] = ' ';
		}
		while (col < kCols)
			out[col++] = ' ';
	}

	/** @p v (0..999, clamped) in decimal; returns the digits written. */
	static int formatNumber(char *out, int v) {
		if (v < 0)
			v = 0;
		if (v > 999)
			v = 999;
		int n = 0;
		if (v >= 100)
			out[n++] = (char)('0' + v / 100);
		if (v >= 10)
			out[n++] = (char)('0' + v / 10 % 10);
		out[n++] = (char)('0' + v % 10);
		return n;
	}
};

/**
 * EXITLOG.TXT's line for @p step: "<step> <what>\r\n", cut to fit @p size
 * (the CR LF always ends it). Returns its length. @p size must be at
 * least 8.
 */
inline int formatExitLogLine(char *buf, int size, int step, const char *what) {
	int n = ExitTraceLine::formatNumber(buf, step);
	buf[n++] = ' ';
	for (const char *p = what; p && *p && n < size - 2; ++p)
		buf[n++] = *p;
	buf[n++] = '\r';
	buf[n++] = '\n';
	return n;
}

/**
 * The PIC mask bits of IRQ @p irq: its own bit on the master (IRQ 0-7)
 * or on the slave (8-15), and for a slave IRQ the cascade (IRQ 2) on the
 * master. Nothing for an IRQ out of range.
 */
inline void irqPicBits(int irq, uint8 &master, uint8 &slave) {
	master = slave = 0;
	if (irq >= 0 && irq < 8) {
		master = (uint8)(1 << irq);
	} else if (irq >= 8 && irq < 16) {
		slave = (uint8)(1 << (irq - 8));
		master = 0x04;
	}
}

/**
 * A PIC mask with @p bits put back as they were in @p saved and every
 * other bit as it is now in @p current.
 */
inline uint8 picMaskRestore(uint8 current, uint8 saved, uint8 bits) {
	return (uint8)((current & ~bits) | (saved & bits));
}

} // End of namespace DOS

#endif
