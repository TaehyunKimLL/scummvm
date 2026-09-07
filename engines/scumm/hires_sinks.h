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

#ifndef SCUMM_HIRES_SINKS_H
#define SCUMM_HIRES_SINKS_H

#include "common/endian.h"
#include "scumm/hires_sink.h"

namespace Scumm {

/**
 * A paletted destination: indices are written through unchanged.
 *
 * This is what the game's own picture is, so text keys in rather than
 * blending. Partial coverage has nowhere to go, and rounding it is the
 * honest answer - see writeBlended().
 */
class HiResIndexSink : public HiResSink {
public:
	explicit HiResIndexSink(byte *dst) : _dst(dst) {}

	void writeBackground(const byte *bg, int count) override {
		memcpy(_dst, bg, count);
		_dst += count;
	}

	void writeOpaque(const byte *fg, int count) override {
		memcpy(_dst, fg, count);
		_dst += count;
	}

	/**
	 * Rounded to whichever side covers more, because a palette index cannot
	 * hold a mixture. Antialiasing is lost; the glyph shape is not.
	 */
	void writeBlended(const byte *fg, const byte *bg,
					  const byte *coverage, int count) override {
		for (int i = 0; i < count; ++i)
			_dst[i] = (coverage[i] >= 128) ? fg[i] : bg[i];
		_dst += count;
	}

private:
	byte *_dst;
};

/**
 * A 16-bit destination reached through a lookup table.
 *
 * The table is the engine's palette already converted to the screen format,
 * so this resolves indices per call and holds no colours of its own.
 */
class HiResPalette16Sink : public HiResSink {
public:
	HiResPalette16Sink(byte *dst, const uint16 *palette)
		: _dst(dst), _pal(palette) {}

	void writeBackground(const byte *bg, int count) override {
		for (int i = 0; i < count; ++i) {
			WRITE_UINT16(_dst, _pal[bg[i]]);
			_dst += 2;
		}
	}

	void writeOpaque(const byte *fg, int count) override {
		for (int i = 0; i < count; ++i) {
			WRITE_UINT16(_dst, _pal[fg[i]]);
			_dst += 2;
		}
	}

	/// As for the paletted sink: a table entry cannot hold a mixture.
	void writeBlended(const byte *fg, const byte *bg,
					  const byte *coverage, int count) override {
		for (int i = 0; i < count; ++i) {
			WRITE_UINT16(_dst, _pal[(coverage[i] >= 128) ? fg[i] : bg[i]]);
			_dst += 2;
		}
	}

private:
	byte *_dst;
	const uint16 *_pal;
};

/**
 * A true-colour destination, which is the only one that can blend.
 *
 * Colours are resolved from @p palette on every call, so a palette change
 * between frames is picked up without anything being redrawn.
 */
class HiResTrueColorSink : public HiResSink {
public:
	HiResTrueColorSink(uint32 *dst, const uint32 *palette,
					   const Graphics::PixelFormat &format)
		: _dst(dst), _pal(palette), _format(format) {}

	void writeBackground(const byte *bg, int count) override {
		for (int i = 0; i < count; ++i)
			*_dst++ = _pal[bg[i]];
	}

	void writeOpaque(const byte *fg, int count) override {
		for (int i = 0; i < count; ++i)
			*_dst++ = _pal[fg[i]];
	}

	void writeBlended(const byte *fg, const byte *bg,
					  const byte *coverage, int count) override {
		for (int i = 0; i < count; ++i) {
			const uint32 f = _pal[fg[i]];
			const uint32 b = _pal[bg[i]];
			const byte a = coverage[i];

			uint8 fr, fg8, fb, br, bg8, bb;
			_format.colorToRGB(f, fr, fg8, fb);
			_format.colorToRGB(b, br, bg8, bb);

			*_dst++ = _format.RGBToColor((fr * a + br * (255 - a)) / 255,
										 (fg8 * a + bg8 * (255 - a)) / 255,
										 (fb * a + bb * (255 - a)) / 255);
		}
	}

private:
	uint32 *_dst;
	const uint32 *_pal;
	Graphics::PixelFormat _format;
};

} // End of namespace Scumm

#endif
