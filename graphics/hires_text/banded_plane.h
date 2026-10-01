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

#ifndef GRAPHICS_HIRES_TEXT_BANDED_PLANE_H
#define GRAPHICS_HIRES_TEXT_BANDED_PLANE_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

namespace Graphics {

struct Surface;

/**
 * A plane of one byte a pixel (coverage, a decoration's colour) the size of
 * a screen, kept in bands of kBandRows rows. A band exists only while
 * something non-zero is in it: a band that is not there reads as zeros, and
 * one is given back as soon as its last non-zero pixel is cleared. Text
 * covers a few rows of a screen, so most of the plane is never allocated.
 *
 * A plane made packable keeps its values at 4 bits, two pixels a byte,
 * while every value written is a multiple of 17 (what 1 and 2 bpp coverage
 * expands to: 0, 85, 170, 255; and 0xFF). The first value that is not one
 * (8-bit coverage, a soft outline's rim) turns it into a byte a pixel for
 * the rest of its life. Either way a value reads back as it was written.
 */
class BandedPlane {
public:
	enum { kBandShift = 4, kBandRows = 1 << kBandShift };

	BandedPlane() : _w(0), _h(0), _packedPitch(0), _packed(false), _bandFailed(false) {}
	~BandedPlane() { free(); }

	/** A @p w x @p h plane of zeros, holding no band yet. */
	void create(int w, int h, bool packable);
	/** No plane. */
	void free();
	bool exists() const { return _w > 0 && _h > 0; }

	int width() const { return _w; }
	int height() const { return _h; }
	/// Whether the values are kept at 4 bits.
	bool packed() const { return _packed; }

	/** The value at (@p x, @p y), which must be inside the plane. */
	byte get(int x, int y) const {
		const Band &b = _bands[y >> kBandShift];
		if (!b.data)
			return 0;
		const int r = y & (kBandRows - 1);
		if (_packed) {
			const byte v = b.data[r * _packedPitch + (x >> 1)];
			return (byte)(((x & 1) ? (v >> 4) : (v & 15)) * 17);
		}
		return b.data[r * _w + x];
	}

	/** Write @p v at (@p x, @p y), which must be inside the plane. */
	void set(int x, int y, byte v);

	/**
	 * Row @p y as one byte a pixel, @p width() of them: the band's own row,
	 * or @p scratch (width() bytes) filled from a packed one. Null when no
	 * band holds the row: it is all zeros.
	 */
	const byte *row(int y, byte *scratch) const;

	/** Whether a band holds row @p y (if not, the row is all zeros). */
	bool rowHeld(int y) const { return _bands[y >> kBandShift].data != nullptr; }

	/** @p r, clipped to the plane, set to @p v. */
	void fill(const Common::Rect &r, byte v);

	/** Everything zero again: every band given back. */
	void clear();

	/** The same contents as @p other (sizes included). */
	void copyFrom(const BandedPlane &other);

	/**
	 * Bytes of the plane read as if it were one array of width() x height()
	 * bytes (row y at y * width()): @p n from @p offset into @p dst.
	 */
	void readBytes(uint32 offset, byte *dst, uint32 n) const;
	/// And written back that way.
	void writeBytes(uint32 offset, const byte *src, uint32 n);

	/** The whole plane in @p out, a CLUT8 surface made for it. */
	void toSurface(Surface &out) const;

	/// Bands held now.
	uint bandsHeld() const;
	/// Bytes the bands hold now.
	uint32 bytesHeld() const { return bandsHeld() * bandBytes(); }

private:
	BandedPlane(const BandedPlane &);
	BandedPlane &operator=(const BandedPlane &);

	struct Band {
		Band() : data(nullptr), nonZero(0) {}
		byte *data;
		uint32 nonZero;	///< pixels whose value is not 0
	};

	uint32 bandBytes() const { return (uint32)kBandRows * (_packed ? _packedPitch : _w); }
	byte *makeBand(int b);
	void dropBand(int b);
	void unpack();

	int _w, _h;
	int _packedPitch;
	bool _packed;
	bool _bandFailed;	///< a band could not be allocated (warned once)
	Common::Array<Band> _bands;
};

} // End of namespace Graphics

#endif
