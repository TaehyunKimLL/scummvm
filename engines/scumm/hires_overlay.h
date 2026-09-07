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

#ifndef SCUMM_HIRES_OVERLAY_H
#define SCUMM_HIRES_OVERLAY_H

#include "common/rect.h"
#include "graphics/surface.h"

namespace Scumm {

/**
 * The two planes text is drawn into, kept in step.
 *
 * Text is composited from an **index** plane - the game's own palette indices,
 * which `printCharIntern` writes into directly - and an optional **coverage**
 * plane holding an antialiasing value per pixel. The two must agree on size,
 * lifetime and contents; nothing in the code used to make them.
 *
 * They were created, freed and cleared by calls written next to each other,
 * and a path that touched one without the other was a silent corruption. One
 * such bug lost every glyph on the boot menu, because the overlay was wiped
 * on a path where the game's own buffer was not.
 *
 * @par Indices, not colours
 * The index plane stores palette indices and the coverage plane stores alpha,
 * so a later palette change - cycling, a room fade - recolours text drawn long
 * before. Nothing here resolves a colour; that happens per frame in the
 * compositor.
 *
 * @par The transparent value is the caller's
 * FM-Towns clears the plane to CHARSET_MASK_TRANSPARENCY_TOWNS and reads that
 * back as transparent; every other platform uses CHARSET_MASK_TRANSPARENCY.
 * The overlay does not know which, so clear() takes the key.
 */
class HiResOverlay {
public:
	/**
	 * Allocate both planes at the same size.
	 *
	 * @param withCoverage  false for a stencil-only overlay, which is what a
	 *                      platform that keys text in rather than blending
	 *                      needs; coverage() then stays null.
	 */
	void create(int w, int h, bool withCoverage);

	/// Release both planes. Safe to call when nothing is allocated.
	void free();

	/**
	 * Clear a band of both planes.
	 *
	 * @param transparent  the value that means 'no text here' on this
	 *                     platform - CHARSET_MASK_TRANSPARENCY_TOWNS on
	 *                     FM-Towns, CHARSET_MASK_TRANSPARENCY elsewhere
	 *
	 * Coverage is cleared to zero regardless: left behind, it would blend the
	 * shape of the previous frame's glyphs into whatever is drawn next.
	 */
	void clear(int top, int height, byte transparent);

	/// The same, for a rectangle. Used by the platforms that stamp a text box.
	void clear(const Common::Rect &r, byte transparent);

	/**
	 * The index plane.
	 *
	 * Deliberately public: the engine's charset renderers draw into it, and
	 * hiding it behind accessors would only move the aliasing somewhere less
	 * obvious.
	 */
	Graphics::Surface &index() { return _index; }
	const Graphics::Surface &index() const { return _index; }

	/// The coverage plane, or null when this overlay carries none.
	Graphics::Surface *coverage() { return _coverage.getPixels() ? &_coverage : nullptr; }
	const Graphics::Surface *coverage() const { return _coverage.getPixels() ? &_coverage : nullptr; }

	/**
	 * Add a coverage plane to an overlay that has none.
	 *
	 * Antialiasing is decided after the index plane is already up - a map is
	 * read, a screen format is negotiated - so the second plane arrives late.
	 * It is sized from the index plane rather than from its own arguments
	 * where they disagree, because two planes of different sizes is the
	 * failure this class exists to prevent.
	 */
	void createCoverage(int w, int h);

	/// Drop the coverage plane, leaving the index plane alone.
	void freeCoverage();

	/// Zero a band of coverage without touching the index plane.
	void clearCoverage(int top, int height);

	bool created() const { return _index.getPixels() != nullptr; }

	/**
	 * Take a copy of both planes, for a caller that is about to overwrite
	 * them and wants the previous contents back.
	 *
	 * The GUI does this around a menu. It used to copy the index plane only,
	 * so turning the menu off left the two planes disagreeing.
	 */
	void saveState();

	/// Put back what saveState() took, if anything.
	void restoreState();

	/// Discard a saved state without applying it.
	void dropState();

private:
	Graphics::Surface _index;
	Graphics::Surface _coverage;
	Graphics::Surface _savedIndex;
	Graphics::Surface _savedCoverage;
};

} // End of namespace Scumm

#endif
