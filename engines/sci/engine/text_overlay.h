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

#ifndef SCI_ENGINE_TEXT_OVERLAY_H
#define SCI_ENGINE_TEXT_OVERLAY_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace Sci {

/**
 * A Text.MAP / Text.Res pair as shipped by the Korean SCI fan patches.
 *
 * The overlay replaces whole TEXT resources: for a resource the patch covers,
 * lookupText() takes its strings from here instead of from RESOURCE.00x, so
 * the game data on disk is never modified.
 *
 * Format (reversed from KQ1, KQ5, SQ1, Mixed-Up Mother Goose and Fairy Tales;
 * see harness/i18n/m5fmt.py, which validates all five):
 *
 *   Text.MAP
 *     0x00 byte[4] magic 03 06 00 FF
 *     0x04 u16     size of the record area, in bytes
 *     0x06 records of { u16 resource number, u32 offset into Text.Res }
 *     then padding to a fixed file size, holding STALE records from a
 *     previous game - the header size is the only authority.
 *
 *   Text.Res, at each record offset
 *     +0 byte type      always 3 (kResourceTypeText)
 *     +1 u16  number    resource number, must match the record
 *     +3 u16  storedLen payload length + 4 (SCI counts the 4 header bytes
 *                       that follow the number)
 *     +5 u16  origLen   length of the English resource being replaced;
 *                       informational, and NOT equal to storedLen in SQ1
 *     +7 u16  method    0 = stored; no compressed block has been observed
 *     +9 byte[storedLen - 4]  NUL-separated strings, indexed like a real
 *                       TEXT resource
 *
 * Blocks are laid out consecutively but some are followed by unused slack
 * (up to 434 bytes in SQ1), so a reader must seek with the record offset and
 * never walk the file by adding block sizes.
 */
class TextOverlay {
public:
	TextOverlay() : _loaded(false) {}

	/**
	 * Look for Text.MAP/Text.Res in the game directory and index them.
	 * Returns true if a usable overlay was found. Safe to call when absent.
	 */
	bool load();

	bool isLoaded() const { return _loaded; }

	/**
	 * Fetch string @p index of TEXT resource @p number.
	 * Returns false when this overlay does not cover the resource, in which
	 * case the caller must fall back to the real resource.
	 */
	bool getText(uint16 number, int index, Common::String &out) const;

	/** Number of TEXT resources this overlay overrides. */
	uint size() const { return _blocks.size(); }

private:
	struct Block {
		uint32 offset;	///< payload start inside _data
		uint32 length;	///< payload length in bytes
	};

	bool _loaded;
	Common::Array<byte> _data;			///< the whole Text.Res
	Common::HashMap<uint16, Block> _blocks;	///< resource number -> payload
};

} // End of namespace Sci

#endif // SCI_ENGINE_TEXT_OVERLAY_H
