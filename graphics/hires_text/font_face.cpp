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

#include "graphics/hires_text/font_face.h"

#include "common/fs.h"
#include "common/stream.h"

namespace Graphics {

bool splitFontFaceIndex(const Common::String &name, Common::String &file, int32 &faceIndex) {
	const size_t hash = name.findLastOf('#');
	if (hash == Common::String::npos || hash == 0 || hash + 1 >= name.size())
		return false;
	uint32 value = 0;
	bool overflow = false;
	for (size_t i = hash + 1; i < name.size(); ++i) {
		const char c = name[i];
		if (c < '0' || c > '9')
			return false;
		if (!overflow) {
			value = value * 10 + (uint32)(c - '0');
			if (value > (uint32)kMaxFontFaceIndex)
				overflow = true;
		}
	}
	file = name.substr(0, hash);
	faceIndex = overflow ? -1 : (int32)value;
	return true;
}

bool fontFileExists(const Common::Path &path) {
	const Common::FSNode node(path);
	return node.exists() && !node.isDirectory();
}

bool resolveFontFace(const Common::Path &path, Common::Path &file, int32 &faceIndex,
                     FontFileExistsFn exists) {
	file = path;
	faceIndex = 0;
	if (path.empty() || exists(path))
		return !path.empty();
	Common::String base;
	int32 index;
	if (!splitFontFaceIndex(path.baseName(), base, index))
		return false;
	const Common::Path candidate = path.getParent().appendComponent(base);
	if (!exists(candidate))
		return false;
	file = candidate;
	faceIndex = index;
	return true;
}

Common::SeekableReadStream *openFontFace(const Common::Path &path, int32 &faceIndex, Common::String &error) {
	Common::Path file;
	faceIndex = 0;
	// A path without a usable face suffix is checked exactly as before:
	// exists(), then isDirectory(), then the read itself.
	if (!resolveFontFace(path, file, faceIndex)) {
		const Common::FSNode node(path);
		if (!node.exists())
			error = "does not exist";
		else if (node.isDirectory())
			error = "is a directory";
		else if (Common::SeekableReadStream *stream = node.createReadStream())
			return stream;
		else
			error = "could not open the file";
		return nullptr;
	}
	if (faceIndex < 0) {
		error = Common::String::format("face index out of range (0..%d)", (int)kMaxFontFaceIndex);
		faceIndex = 0;
		return nullptr;
	}
	Common::SeekableReadStream *stream = Common::FSNode(file).createReadStream();
	if (!stream)
		error = "could not open the file";
	return stream;
}

} // End of namespace Graphics
