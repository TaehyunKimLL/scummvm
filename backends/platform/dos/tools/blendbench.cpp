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
 * BLEND.EXE: what the hi-res text composite costs on this PC. One 4-line dialogue box at 2x (400 x 128 pixels, the
 * coverage of a 2 bpp face: 70 % empty, 8 % at 85, 7 % at 170, 15 % at 255) is composited over random background
 * pixels at 5-6-5 and 5-5-5, as the SCI driver does for every updated rectangle that overlaps text, by
 *
 *   A  the loop composeSpan() had before Blend16: blendPixel() for every covered pixel (kept here, so the numbers stay
 *      comparable with the P7 plan's section 0.3 on any build),
 *   B  composeSpan() with one Blend16 kept across the rows of the box (what the driver does),
 *   C  composeSpan() without a table (one made for each call).
 *
 * Usage: BLEND [label [repetitions]]. One line per format is appended to BLEND.TXT (the label first, e.g. the CPU),
 * with the time of a box in milliseconds and whether B and C equal A (they must: Blend16 is exact). Built by
 * build-dos.sh bench BUILD_DIR, which links the text_compose.o of that build.
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// text_compose.o refers to TextPlane::getRow() from compositeRect(); this program composites rows itself.
namespace Graphics {
bool TextPlane::getRow(int, int, int, byte *, byte *, const byte *&, const byte *&) const {
	return false;
}
} // End of namespace Graphics

using Graphics::PixelFormat;

static const int W = 400, H = 128;
static byte idx[W * H], cov[W * H], pal[256 * 3];
static uint16 dstA[W * H], dstB[W * H], dstC[W * H], bg[W * H];

static double now() {
	return (double)uclock() / UCLOCKS_PER_SEC;
}

// composeSpan() as it was: blendPixel() for each covered pixel.
static void referenceSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n, const byte *p) {
	uint16 *d = (uint16 *)dst;
	for (int i = 0; i < n; ++i) {
		if (!coverage[i])
			continue;
		const byte *c = p + index[i] * 3;
		d[i] = (uint16)Graphics::TextCompose::blendPixel(fmt, d[i], c[0], c[1], c[2], coverage[i]);
	}
}

static int run(const char *label, bool is555, int reps) {
	const PixelFormat fmt = is555 ? PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0) : PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
	uint32 seed = 12345;
	for (int i = 0; i < 256 * 3; ++i) {
		seed = seed * 1103515245 + 12345;
		pal[i] = (byte)(seed >> 16);
	}
	int covered = 0;
	for (int i = 0; i < W * H; ++i) {
		seed = seed * 1103515245 + 12345;
		const int r = (seed >> 16) % 100;
		cov[i] = r < 70 ? 0 : r < 78 ? 85 : r < 85 ? 170 : 255;
		covered += cov[i] != 0;
		idx[i] = (i / W) < 96 ? 15 : 14;	// two text colours per box
		bg[i] = (uint16)(seed >> 8);
	}
	double tA = 0, tB = 0, tC = 0;
	for (int rep = 0; rep < reps; ++rep) {
		memcpy(dstA, bg, sizeof bg);
		memcpy(dstB, bg, sizeof bg);
		memcpy(dstC, bg, sizeof bg);
		const double t0 = now();
		for (int y = 0; y < H; ++y)
			referenceSpan((byte *)(dstA + y * W), fmt, idx + y * W, cov + y * W, W, pal);
		const double t1 = now();
		Graphics::TextCompose::Blend16 table(fmt);
		for (int y = 0; y < H; ++y)
			Graphics::TextCompose::composeSpan((byte *)(dstB + y * W), fmt, idx + y * W, cov + y * W, W, pal,
			                                   Graphics::TextCompose::kBlend, &table);
		const double t2 = now();
		for (int y = 0; y < H; ++y)
			Graphics::TextCompose::composeSpan((byte *)(dstC + y * W), fmt, idx + y * W, cov + y * W, W, pal,
			                                   Graphics::TextCompose::kBlend);
		const double t3 = now();
		tA += t1 - t0;
		tB += t2 - t1;
		tC += t3 - t2;
	}
	const bool same = !memcmp(dstA, dstB, sizeof dstA) && !memcmp(dstA, dstC, sizeof dstA);
	FILE *f = fopen("BLEND.TXT", "a");
	if (!f)
		return 2;
	fprintf(f, "%s %s box %dx%d covered=%d reps=%d: A(blendPixel loop) %.2f ms  B(Blend16 across rows) %.2f ms  "
	           "C(Blend16 per row) %.2f ms  equal=%d\n",
	        label, is555 ? "rgb555" : "rgb565", W, H, covered, reps, tA * 1000 / reps, tB * 1000 / reps, tC * 1000 / reps, same);
	fclose(f);
	return same ? 0 : 1;
}

int main(int argc, char **argv) {
	const char *label = argc > 1 ? argv[1] : "-";
	const int reps = argc > 2 ? atoi(argv[2]) : 10;
	int rc = run(label, false, reps);
	rc |= run(label, true, reps);
	return rc;
}
