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

#ifndef SCI_GRAPHICS_HIRESTEXTSTATE_H
#define SCI_GRAPHICS_HIRESTEXTSTATE_H

#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "sci/graphics/hirestextsettings.h"

namespace Graphics {
struct PixelFormat;
}

namespace Sci {

/**
 * The hi-res text configuration as it is known before the screen exists
 * (design section 7.1.1's phase 1). SCI creates its graphics driver in
 * GfxScreen's constructor, long before GfxCache, so SciEngine builds this
 * first: the ini keys once (their warnings printed once), the map path, and
 * the map loaded with the engine qualifiers only, quietly. The driver reads
 * the target, blend and coverage from it; GfxCache later loads the map
 * again for the screen actually set (phase 2) from the same path and ini.
 */
class HiresTextState {
public:
	HiresTextState();

	/**
	 * Read the active game domain's ini keys and, when hi-res text applies
	 * (@p applies: hiresTextApplies() of the game) and is not switched off,
	 * the map's phase-1 view. A map refused outright prints its warnings
	 * here, once; GfxCache does not load it again.
	 */
	void load(bool applies);

	/** The game's own folder (relative ini paths resolve against it). */
	const Common::Path &gameDir() const { return _gameDir; }
	/** The ini keys, as read (hires_text=false is kept, not reset). */
	const Graphics::HiResIniOverrides &ini() const { return _ini; }

	/** A map file was named or found (it may still be refused). */
	bool haveMapPath() const { return _haveMapPath; }
	const Common::Path &mapPath() const { return _mapPath; }
	/** The folder the map's own relative paths resolve against. */
	const Common::Path &mapDir() const { return _mapDir; }
	/** The map file exists but is not a version 2 map (warned about already). */
	bool mapRefused() const { return _mapRefused; }

	/** The phase-1 view (engine qualifiers only, no target sections). */
	const Graphics::HiResMap &phase1Map() const { return _phase1; }
	bool phase1Loaded() const { return _phase1Loaded; }

	/** What the driver asks for: sciPhase1(), auto everywhere when hi-res text is off. */
	const SciPhase1 &phase1() const { return _choice; }

	/**
	 * Called by the driver factory with the choice it made for the upscaled
	 * hi-res text driver; GfxDefaultDriver::initScreen() reads it back.
	 * Other drivers never set it, so their request stays upstream's.
	 */
	void setDriverTarget(Graphics::HiResRenderTarget t) { _driverTarget = t; }
	Graphics::HiResRenderTarget driverTarget() const { return _driverTarget; }

	/**
	 * Once the screen is set: one `render_target=<asked> is not available
	 * here; using <set>` when an explicit target was not met. A warning
	 * while hi-res text is on for the game; only a debug line otherwise, so
	 * a global render_target does not nag every other game.
	 */
	void adoptScreen(const Graphics::PixelFormat &actual);

	/**
	 * Graphics::HiResCoverageFn: a 2 or 8 bpp SVF, or anything else that
	 * opens (TrueType), sniffed from the file's first bytes.
	 */
	static bool faceHasCoverage(const Common::Path &face, void *ctx);

private:
	bool _active;                   ///< hi-res text applies and hires_text is not false
	Graphics::HiResIniOverrides _ini;
	Common::Path _gameDir;
	bool _haveMapPath;
	Common::Path _mapPath;
	Common::Path _mapDir;
	bool _mapRefused;
	Graphics::HiResMap _phase1;
	bool _phase1Loaded;
	SciPhase1 _choice;
	Graphics::HiResRenderTarget _driverTarget;
	bool _noted;
};

/** The game folder's own HIRESTXT.MAP, matched case-insensitively; empty when absent. */
Common::Path findDefaultHiresMap(const Common::Path &gameDir);

} // End of namespace Sci

#endif
