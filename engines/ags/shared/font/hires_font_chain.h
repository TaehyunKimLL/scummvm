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

#ifndef AGS_SHARED_FONT_HIRES_FONT_CHAIN_H
#define AGS_SHARED_FONT_HIRES_FONT_CHAIN_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "ags/shared/font/hires_font_config.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {
class UnicodeGlyphSource;
}

namespace AGS3 {

/** Opens a face file (Graphics::openFontFace()'s signature). */
typedef Common::SeekableReadStream *(*HiResFaceOpenFn)(const Common::Path &path, int32 &faceIndex, Common::String &error);

/**
 * A plan's faces opened into one chain. Each file says what it is: the SVFN
 * magic is a baked bitmap font, anything else is opened as TrueType. Faces
 * of different cells or depths are brought to one cell at 8 bpp
 * (Graphics::layoutFaceChain(), NormalizedGlyphSource), so every face that
 * opens joins the chain; only an SVFN font whose cell height differs from
 * the first SVFN font's is refused.
 */
struct HiResFontChain {
	HiResFontChain();

	/// Owned (free()): the FallbackGlyphSource over the joined faces, or the
	/// one face itself.
	Graphics::UnicodeGlyphSource *source;
	/// The faces that joined, as opened (owned through @ref source), in order.
	Common::Array<Graphics::UnicodeGlyphSource *> faces;
	Common::Array<Common::String> names;	///< their file names
	int size;								///< the pixel size the TrueType faces opened at
	int pixelPpem;							///< the pixel face's ppem, 0 when the chain has none
	bool bitmap;							///< an SVFN font joined
	bool trueType;							///< a TrueType face joined
	/// One line per problem (a face that cannot be opened or join), for the
	/// caller to print.
	Common::Array<Common::String> warnings;

	/** Delete the sources and forget the faces (the warnings stay). */
	void free();
};

/**
 * Open @p plan's faces for font @p fontNumber. TrueType faces open at
 * @p size (brought into TtfGlyphSource's range, with a warning, when it is
 * outside it), the plan's first face at its pixel= when that is set, and
 * fitted to @p fitProbes. False when no face could be used.
 */
bool openHiResFontChain(const HiResFontPlan &plan, int fontNumber, int size, const Common::Array<uint32> &fitProbes,
						HiResFontChain &out, HiResFaceOpenFn open);

} // namespace AGS3

#endif
