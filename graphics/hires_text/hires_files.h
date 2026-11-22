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

#ifndef GRAPHICS_HIRES_TEXT_HIRES_FILES_H
#define GRAPHICS_HIRES_TEXT_HIRES_FILES_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

/** How hi-res text reaches its files: maps, faces. Tests give their own. */
class HiResFiles {
public:
	virtual ~HiResFiles() {}
	/** The file @p name ("X.MAP", "ko/X.MAP") under @p dir, each component matched ignoring case; empty if none. */
	virtual Common::Path find(const Common::Path &dir, const Common::String &name) = 0;
	/** Where @p ref is: a data: reference through the data folders; empty if it is nowhere. */
	virtual Common::Path locate(const HiResFaceRef &ref) = 0;
	/** @p path (from find() or locate()) opened for reading; nullptr if it cannot be. */
	virtual Common::SeekableReadStream *open(const Common::Path &path) = 0;
};

/**
 * The file system: find() lists @p dir; locate() takes a data: reference in
 * each of the extrapaths (command line, game, application), then DATA_PATH,
 * then the roots given to addDataRoot(), and finally SearchMan (the path
 * then starts with "searchman:").
 */
class HiResDiskFiles : public HiResFiles {
public:
	HiResDiskFiles();
	void addDataRoot(const Common::Path &dir) { _roots.push_back(dir); }
	const Common::Array<Common::Path> &getDataRoots() const { return _roots; }

	Common::Path find(const Common::Path &dir, const Common::String &name) override;
	Common::Path locate(const HiResFaceRef &ref) override;
	Common::SeekableReadStream *open(const Common::Path &path) override;

	/** "data:" part @p relative is safe: not empty, not absolute, no "..". */
	static bool isSafeDataPath(const Common::Path &relative);

private:
	Common::Array<Common::Path> _roots;
};

/**
 * The map file @p name for a game: a data: or absolute value as it says; a
 * bare name in the language folder @p langFolder (ko) of @p gameDir first,
 * then in @p gameDir. Empty if there is none.
 */
Common::Path findGameMapFile(HiResFiles &files, const Common::Path &gameDir, const Common::String &langFolder,
                             const Common::String &name);

} // End of namespace Graphics

#endif
