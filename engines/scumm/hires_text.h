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

#ifndef SCUMM_HIRES_TEXT_H
#define SCUMM_HIRES_TEXT_H

#include "common/language.h"
#include "common/path.h"
#include "common/rect.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

namespace Scumm {

/**
 * The engine's side of the hi-res text layer.
 *
 * Everything that knows about SCUMM lives here; everything that does not lives
 * in graphics/hires_text. The split is what lets a second engine reuse the
 * font handling without inheriting SCUMM's screen model.
 *
 * This is state and policy only. Loading fonts, decoding strings and drawing
 * are added in later steps; until then nothing here changes what reaches the
 * screen.
 */
struct ScummHiResText {
	ScummHiResText();

	/**
	 * Read the font map and the related config keys.
	 *
	 * @param gameDir     the game's own folder, for relative paths
	 * @param gameId      identifies the game, e.g. "monkey2"
	 * @param version     SCUMM version, for the "v5" style qualifier
	 * @param language    what the game was detected as
	 */
	void loadConfig(const Common::Path &gameDir, const Common::String &gameId,
					int version, Common::Language language);

	void reset();

	/**
	 * Whether the hi-res path should be used at all.
	 *
	 * False here has to leave the engine on exactly its original path: this
	 * is the switch that keeps every game we do not touch untouched.
	 */
	bool enabled() const { return _enabled; }

	int scale() const { return _config.scale; }
	bool wantsAlpha() const { return _config.alpha; }
	Common::CodePage encoding() const { return _config.encoding; }

	const Graphics::HiResTextConfig &config() const { return _config; }

	/// Font role for one of the game's own line heights.
	int roleForHeight(int height) const { return _config.roleForHeight(height); }

	/**
	 * The size to render a role at, resolved against the scale in force.
	 *
	 * A map may give a size in logical units that follow the scale, so this
	 * cannot be worked out while parsing - an explicit user scale outranks
	 * the map and is only known afterwards.
	 *
	 * @return 0 when the map did not pin a size, i.e. fit it to the line box
	 */
	int resolvedFontSize(int role) const;

	int supersample(int role) const;

	/**
	 * Decode the next character of a game string.
	 *
	 * This is the only place that knows how many bytes a character takes, so
	 * everything past it works on Unicode code points and no longer has to
	 * assume "one byte is Latin, two bytes are CJK". That assumption is false
	 * in both directions: an accented Latin letter is multi-byte in UTF-8,
	 * and half-width katakana is single-byte in Shift-JIS.
	 *
	 * The caller is expected to have dealt with the engine's own control
	 * codes already. This must not be handed a byte that SCUMM treats as an
	 * escape, because a trail byte can have the same value as one.
	 *
	 * @param p    read pointer, advanced past the character consumed
	 * @param end  one past the last readable byte
	 * @return the code point, or 0 when nothing could be decoded
	 */
	uint32 decodeNext(const byte *&p, const byte *end) const;

	/**
	 * How the engine's own strings are encoded.
	 *
	 * Set from the detected language unless a map overrides it. Invalid means
	 * the game's text is single byte in an encoding nothing has named, which
	 * is the safe assumption for the games we do not touch.
	 */
	void setEncoding(Common::CodePage page) { _config.encoding = page; }

private:
	bool _enabled;
	Graphics::HiResTextConfig _config;
};

} // End of namespace Scumm

#endif
