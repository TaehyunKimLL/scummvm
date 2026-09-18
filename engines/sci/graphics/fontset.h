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

#ifndef SCI_GRAPHICS_FONTSET_H
#define SCI_GRAPHICS_FONTSET_H

#include "common/array.h"
#include "common/str-enc.h"
#include "sci/graphics/scifont.h"

namespace Sci {

/**
 * Several faces behind one font id, chosen per character by coverage.
 *
 * A font id in SCI is a typeface the script picked - font 4 is the small one,
 * font 300 the large one - and scripts choose deliberately. It is NOT a
 * language. The engine nevertheless grew two ids that mean a language: 1001
 * for Korean and 900 for Shift-JIS, reached by GfxText16 calling SetFont()
 * when the bytes look like that script.
 *
 * Measured on KQ1, three runs differing only in which translation bundle is
 * present: English asks for fonts 4, 300 and 0 and never for 1001 or 900,
 * while Korean adds 52 requests for 1001 and Japanese 59 for 900 - every one
 * manufactured by SwitchToFont1001OnKorean / SwitchToFont900OnSjis, none by
 * the game. Each switch also discards the font the script chose, which is why
 * KQ1's four faces (heights 8, 9, 12 and 8) collapse to one once CJK text
 * appears.
 *
 * A set keeps the id the script asked for and picks the face per character:
 *
 *   font 4  ->  [ resource face font.004 ] [ DBCS face ] [ Unicode face ]
 *
 * Order matters and is not arbitrary. The resource face comes first so that
 * single-byte text is drawn by exactly the glyphs the game shipped - serving
 * Latin from a Unicode bundle instead changed the metrics of every English
 * string, making faces of height 12 and 9 both report 8 and pushing menu text
 * outside its button.
 *
 * What this class deliberately does NOT do: decide whether the glyphs bypass
 * the normal blit. That is GfxText16's `doubleByteMode`, which exists because
 * ScummVM does not emulate the PC-98 text mode layer - a hardware plane
 * composited above the graphics layer, which is why the original interpreter
 * needs no such flag. It belongs to the driver, not to a font, and no code
 * here should infer it from a font id.
 */
class GfxFontSet : public GfxFont {
public:
	/**
	 * @param resourceId  the id the caller asked for; reported unchanged, so
	 *                    a set is indistinguishable from the font it replaces.
	 */
	enum FaceKind {
		kFaceResource,	///< the game's own font; single-byte only
		kFaceLegacyDbcs,	///< korean.fnt / SJIS.FNT, addressed by byte pair
		kFaceCodePoint	///< a SCVMUNI bundle, addressed by code point
	};

	GfxFontSet(GuiResourceId resourceId, Common::CodePage codePage);
	~GfxFontSet() override;

	/**
	 * Append a face. Faces are consulted in the order they were added, so the
	 * game's own resource face must be added first.
	 *
	 * @param kind         what the face is addressed by, and therefore how its
	 *                     coverage is decided. The resource face is never
	 *                     asked about double-byte characters: it has no such
	 *                     glyphs, but reports a width for them anyway, so
	 *                     letting it answer swallowed every Korean syllable
	 *                     before the Unicode face was reached.
	 * @param owned        true when the set should delete the face. The
	 *                     Unicode bundle is shared by every set - it is
	 *                     several hundred kilobytes - so it is passed
	 *                     unowned and outlives them.
	 * @param hiresPlane   true when the face draws on the hires text plane at
	 *                     twice the lowres coordinates. Its advance and height
	 *                     are then reported halved, which is what
	 *                     GfxFontKorean does below SCI2. Reporting the full
	 *                     width spaces the glyphs apart and pushes the tail of
	 *                     a menu entry outside its button - measured.
	 */
	void addFace(GfxFont *face, FaceKind kind, bool owned = true, bool hiresPlane = false);

	bool isEmpty() const { return _faces.empty(); }
	uint faceCount() const { return _faces.size(); }

	GuiResourceId getResourceId() override { return _resourceId; }
	byte getHeight() override;
	bool isDoubleByte(uint32 chr) override;
	byte getCharWidth(uint32 chr) override;
	byte getCharHeight(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput,
	                  byte *buffer, int16 width, int16 height) override;

private:
	struct Face {
		GfxFont *font;
		FaceKind kind;
		bool owned;
		bool hiresPlane;
	};

	/**
	 * The face that should draw @p chr, and the value to pass it.
	 *
	 * Single-byte characters always resolve to the first face, which keeps
	 * their rendering byte-identical to the unmodified engine.
	 */
	const Face *faceFor(uint32 chr, uint32 &outChr) const;

	/** Identity now that GfxText16 decodes; kept as the single seam. */
	uint32 toCodePoint(uint32 chr) const;

	/** Re-encode a code point to the byte pair a legacy face indexes by. */
	uint32 toEncodedPair(uint32 codePoint) const;

	/** Halve a hires-plane face's metric into lowres coordinates. */
	byte toLowres(const Face &f, byte v) const;

	/** Whether the legacy double-byte face for this code page covers @p cp. */
	bool legacyCovers(uint32 codePoint) const;

	Common::Array<Face> _faces;
	GuiResourceId _resourceId;
	Common::CodePage _codePage;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_FONTSET_H
