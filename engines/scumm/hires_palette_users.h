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

#ifndef SCUMM_HIRES_PALETTE_USERS_H
#define SCUMM_HIRES_PALETTE_USERS_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/util.h"
#include "scumm/hires_composite.h"

namespace Scumm {

/**
 * Whether any of @p n bytes at @p p is marked in @p changed, where @p cov
 * (if not null) is nonzero and the byte is not @p key (if @p useKey). Runs
 * of four keyed or uncovered bytes, most of either plane, go a word at a time.
 */
static inline bool anyChangedIndex(const byte *p, const byte *cov, int n,
								   bool useKey, byte key, const bool *changed) {
	const uint32 keyWord = key * 0x01010101u;
	int i = 0;
	while (i < n) {
		if (i + 4 <= n) {
			uint32 w;
			memcpy(&w, cov ? cov + i : p + i, 4);
			if ((cov && w == 0) || (!cov && useKey && w == keyWord)) {
				i += 4;
				continue;
			}
		}
		const int end = MIN(i + 4, n);
		for (; i < end; ++i) {
			if (cov && !cov[i])
				continue;
			if (useKey && p[i] == key)
				continue;
			if (changed[p[i]])
				return true;
		}
	}
	return false;
}

/**
 * Find where a palette change shows on a true-colour screen.
 *
 * With blended hi-res text the colours are resolved when the composite is
 * built, so a pixel drawn with a palette entry that has changed has to be
 * composited again; nothing else does. Recompositing the whole screen on
 * every change is correct but, in a room that cycles a few colours, costs a
 * full 640x400 true-colour redraw every other loop (MI2 room 33: from about
 * 10 loops/s down to 6 with ample memory, and to 1.2 on a 16 MB DOS machine,
 * where the redraw also pages).
 *
 * For each 8-pixel strip of a @p width x @p height region of the game's
 * picture this gives the rows [top, bottom) that hold an entry marked in
 * @p changed: in the picture itself, in the text plane (at @p m times the
 * size, the transparent key excluded) or, where it has coverage, in the
 * decoration plane. A strip with none gets top >= bottom.
 *
 * Pitches are row strides here (not the compositor's skips). @p under and
 * @p underCov may be null; they share @p underPitch. @p top and @p bottom
 * hold (width + 7) / 8 entries.
 */
static inline void findPaletteUsersRows(const byte *src, int srcPitch,
										const byte *text, int textPitch,
										const CompositeRows &under, const CompositeRows &underCov,
										int width, int height, int m, const bool *changed,
										int *top, int *bottom) {
	const int strips = (width + 7) / 8;
	for (int s = 0; s < strips; ++s) {
		top[s] = height;
		bottom[s] = 0;
	}
	const bool withUnder = under.present() && underCov.present();
	// The decoration's m rows of one game row, read (or unpacked) once each.
	const int uw = MAX(1, MAX(under.scratchBytes(), underCov.scratchBytes()));
	Common::Array<byte> scratch, zeroRow;
	Common::Array<const byte *> uRow, ucRow;
	if (withUnder) {
		scratch.resize(2 * m * uw);
		zeroRow.resize(width * m);
		memset(zeroRow.begin(), 0, width * m);
		uRow.resize(m);
		ucRow.resize(m);
	}

	for (int y = 0; y < height; ++y) {
		const byte *srcRow = src + y * srcPitch;
		bool anyUnder = false;
		for (int r = 0; r < m && withUnder; ++r) {
			ucRow[r] = underCov.row(y * m + r, &scratch[(2 * r) * uw]);
			uRow[r] = nullptr;
			if (ucRow[r]) {
				uRow[r] = under.row(y * m + r, &scratch[(2 * r + 1) * uw]);
				if (!uRow[r])
					uRow[r] = zeroRow.begin();
				anyUnder = true;
			}
		}
		for (int s = 0; s < strips; ++s) {
			const int x0 = s * 8;
			const int x1 = MIN(x0 + 8, width);
			bool hit = false;

			for (int x = x0; x < x1 && !hit; ++x)
				hit = changed[srcRow[x]];

			for (int r = 0; r < m && !hit && text; ++r)
				hit = anyChangedIndex(text + (y * m + r) * textPitch + x0 * m, nullptr,
									  (x1 - x0) * m, true, kHiResTextTransparent, changed);

			for (int r = 0; r < m && !hit && anyUnder; ++r) {
				if (ucRow[r])
					hit = anyChangedIndex(uRow[r] + x0 * m, ucRow[r] + x0 * m, (x1 - x0) * m, false, 0, changed);
			}

			if (hit) {
				if (y < top[s])
					top[s] = y;
				bottom[s] = y + 1;
			}
		}
	}
}

/// The same, with the decoration planes as bytes in memory (@p underPitch apart).
static inline void findPaletteUsers(const byte *src, int srcPitch,
									const byte *text, int textPitch,
									const byte *under, const byte *underCov, int underPitch,
									int width, int height, int m, const bool *changed,
									int *top, int *bottom) {
	if (!under || !underCov)
		under = underCov = nullptr;
	findPaletteUsersRows(src, srcPitch, text, textPitch,
						 under ? CompositeRows(under, underPitch) : CompositeRows(),
						 underCov ? CompositeRows(underCov, underPitch) : CompositeRows(),
						 width, height, m, changed, top, bottom);
}

} // End of namespace Scumm

#endif
