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

#ifndef AGS_ENGINE_AC_HIRES_TEXT_TWIN_H
#define AGS_ENGINE_AC_HIRES_TEXT_TWIN_H

#include "common/std/map.h"
#include "ags/shared/core/types.h"
#include "ags/shared/font/text_twin.h"
#include "ags/shared/util/geometry.h"

namespace AGS3 {
namespace AGS {
namespace Shared {
class Bitmap;
} // namespace Shared
namespace Engine {
class IDriverDependantBitmap;
} // namespace Engine
} // namespace AGS

/**
 * ScummVM (C23): the text twins of AGS_HIRES_TEXT_DESIGN.md section 4.4.
 * While a HiResTextScope is open (and the hi-res text scale is 2 or more),
 * wouttext_outline() records each line a map font draws into whichever
 * bitmap it draws, keyed by that bitmap. When the bitmap becomes a DDB
 * (sync_object_texture(), the dialog options), attach() checks the
 * records against the bitmap as it is then and gives the DDB an N x twin:
 * the bitmap without the valid records' text, upscaled, with that text
 * drawn at N x. The check against the bitmap's current pixels makes a
 * stale entry (a bitmap freed and another made at the same address)
 * harmless: its records simply do not match.
 */
class HiResTextTwins {
public:
	HiResTextTwins() : _depth(0), _generation(0), _clock(0) {}

	/** A scope opens and closes (nesting allowed). */
	void beginScope();
	void endScope();

	/** Whether a draw of font into ds is to be recorded. */
	bool wantsDraw(const AGS::Shared::Bitmap *ds, int font) const;
	void beginDraw(AGS::Shared::Bitmap *ds, const Common::Rect &band);
	void endDraw(AGS::Shared::Bitmap *ds, const TextDraw &draw);

	/** to is a copy of from's area at offset: move the records there. */
	void derive(const AGS::Shared::Bitmap *from, const AGS::Shared::Bitmap *to, const Point &offset);

	/** Give ddb the twin of bmp (drawn with has_alpha), or none. */
	void attach(AGS::Engine::IDriverDependantBitmap *ddb, AGS::Shared::Bitmap *bmp, bool hasAlpha);

	/** C23: twins built and the time it took, for ags_frame_times */
	uint32 StatBuilds = 0, StatBuildMs = 0;

	/** Forget everything (a game is restored, restarted or unloaded, or the
	 *  scale drops to 1); open scopes end. */
	void clear();

private:
	struct Entry {
		Entry() : generation(0), lastUse(0) {}
		TextCapture capture;
		uint32 generation;
		uint32 lastUse;
	};
	Entry &entryFor(const AGS::Shared::Bitmap *ds);
	void attachBuild(AGS::Engine::IDriverDependantBitmap *ddb, AGS::Shared::Bitmap *bmp, bool hasAlpha,
					 std::map<const void *, Entry>::iterator it);
	void trim();

	std::map<const void *, Entry> _entries;
	int _depth;
	uint32 _generation;		///< one per outermost scope
	uint32 _clock;
};

/** RAII: a capture scope (a no-op when the scale is 1). */
class HiResTextScope {
public:
	explicit HiResTextScope(bool enable = true);
	~HiResTextScope();
private:
	bool _open;
};

/** The registry, or nullptr when the hi-res text scale is 1. */
HiResTextTwins *hires_text_twins();
/** Forget all records (a game is restored, restarted or unloaded). */
void hires_text_twins_reset();

} // namespace AGS3

#endif
