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

/*
 * MMXB.EXE: the MMX text composite against the scalar one, on this PC. Usage: MMXB [label [repetitions [outfile]]].
 * For RGB565 and RGB555: (1) a differential check, scalar composeSpan() against the MMX one over random rows (every
 * coverage, lengths 0..70, odd start offsets of the palette) and over a 400 x 128 box; (2) the time of the box both
 * ways. Results are appended to the out file (default MMXB.TXT) with the label, MMX presence by CPUID and equality.
 * Exit 0 when MMX was absent (scalar only, nothing to compare) or every comparison was equal, 1 on any difference.
 * Built by build-dos.sh mmxbench BUILD_DIR.
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace Graphics {
bool TextPlane::getRow(int, int, int, byte *, byte *, const byte *&, const byte *&) const {
	return false;
}
} // End of namespace Graphics

using Graphics::PixelFormat;
namespace TC = Graphics::TextCompose;

static const int W = 400, H = 128;
static byte idx[W * H], cov[W * H], pal[256 * 3];
static uint16 dstS[W * H], dstM[W * H], dstR[W * H], bg[W * H];
static uint32 seed = 4242;

static uint32 rnd() {
	seed = seed * 1103515245 + 12345;
	return seed >> 8;
}

static double now() {
	return (double)uclock() / UCLOCKS_PER_SEC;
}

static void boxPass(const PixelFormat &fmt, uint16 *dst, bool mmx, int mode) {
	TC::setMMXMode(mode);
	TC::setUseMMX(mmx);
	TC::Blend16 table(fmt);
	for (int y = 0; y < H; ++y)
		TC::composeSpan((byte *)(dst + y * W), fmt, idx + y * W, cov + y * W, W, pal, TC::kBlend, &table);
	TC::setUseMMX(false);
}

static int run(const char *label, bool is555, int reps, const char *outName, bool mmxPresent) {
	const PixelFormat fmt = is555 ? PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0) : PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
	for (int i = 0; i < 256 * 3; ++i)
		pal[i] = (byte)rnd();
	int covered = 0;
	for (int i = 0; i < W * H; ++i) {
		const int r = rnd() % 100;
		cov[i] = r < 70 ? 0 : r < 78 ? 85 : r < 85 ? 170 : 255;
		covered += cov[i] != 0;
		idx[i] = (i / W) < 96 ? 15 : 14;
		bg[i] = (uint16)rnd();
	}
	unsigned long diffs = 0, rows = 0;
	double tS = 0, tM = 0, tR = 0;
	if (mmxPresent) {
		// Random rows of every length 0..70 (tails of 0..3), all coverages, one mixing zero/255/partial blocks.
		for (int rep = 0; rep < 2000; ++rep) {
			uint16 a[80], b[80], b0[80];
			byte ix[80], cv[80];
			const int n = rep % 71;
			for (int k = 0; k < 80; ++k) {
				a[k] = b0[k] = (uint16)rnd();
				ix[k] = (byte)rnd();
				const uint32 r = rnd();
				cv[k] = (rep & 1) ? (byte)r : ((r >> 9) & 3) == 0 ? 0 : ((r >> 9) & 3) == 1 ? 255 : (byte)r;
			}
			TC::setUseMMX(false);
			TC::composeSpan((byte *)a, fmt, ix, cv, n, pal, TC::kBlend);
			for (int mode = 0; mode < 2; ++mode) {
				memcpy(b, b0, sizeof b);
				TC::setMMXMode(mode);
				TC::setUseMMX(true);
				TC::composeSpan((byte *)b, fmt, ix, cv, n, pal, TC::kBlend);
				TC::setUseMMX(false);
				++rows;
				if (memcmp(a, b, sizeof a))
					++diffs;
			}
			TC::setMMXMode(1);
		}
	}
	for (int rep = 0; rep < reps; ++rep) {
		memcpy(dstS, bg, sizeof bg);
		double t0 = now();
		boxPass(fmt, dstS, false);
		tS += now() - t0;
		if (mmxPresent) {
			memcpy(dstM, bg, sizeof bg);
			t0 = now();
			boxPass(fmt, dstM, true, 0);
			tM += now() - t0;
			if (memcmp(dstS, dstM, sizeof dstS))
				++diffs;
			memcpy(dstR, bg, sizeof bg);
			t0 = now();
			boxPass(fmt, dstR, true, 1);
			tR += now() - t0;
			TC::setMMXMode(1);
			if (memcmp(dstS, dstR, sizeof dstS))
				++diffs;
		}
	}
	FILE *f = fopen(outName, "a");
	if (!f)
		return 2;
	fprintf(f, "%s %s box %dx%d covered=%d reps=%d mmx_cpuid=%d mmx_used=%d rows_checked=%lu diffs=%lu: scalar %.2f ms", label,
	        is555 ? "rgb555" : "rgb565", W, H, covered, reps, (int)TC::mmxSupported(), (int)mmxPresent, rows, diffs, tS * 1000 / reps);
	if (mmxPresent)
		fprintf(f, "  mmx-arith %.2f ms  mmx-runs %.2f ms", tM * 1000 / reps, tR * 1000 / reps);
	fprintf(f, "  equal=%d\n", diffs == 0);
	fclose(f);
	return diffs ? 1 : 0;
}

int main(int argc, char **argv) {
	const char *label = argc > 1 ? argv[1] : "-";
	const int reps = argc > 2 ? atoi(argv[2]) : 10;
	const char *out = argc > 3 ? argv[3] : "MMXB.TXT";
	const bool present = TC::mmxSupported();
	int rc = run(label, false, reps, out, present);
	rc |= run(label, true, reps, out, present);
	return rc;
}
