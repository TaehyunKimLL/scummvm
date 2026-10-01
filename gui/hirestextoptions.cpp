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


#include "gui/hirestextoptions.h"

#include "common/config-manager.h"
#include "common/translation.h"

namespace GUI {

Common::Array<HiResTargetEntry> renderTargetEntries(uint32 offered, bool storedSet, Graphics::HiResRenderTarget stored) {
	static const Graphics::HiResRenderTarget kTargets[] = {
		Graphics::kHiResTargetClut8, Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb888
	};

	Common::Array<HiResTargetEntry> entries;
	HiResTargetEntry e;
	e.target = Graphics::kHiResTargetAuto;
	e.available = true;
	entries.push_back(e);

	uint count = 0;
	for (uint i = 0; i < ARRAYSIZE(kTargets); ++i) {
		if (offered & (1u << kTargets[i])) {
			e.target = kTargets[i];
			entries.push_back(e);
			++count;
		}
	}

	const bool keepStored = storedSet && stored != Graphics::kHiResTargetAuto && !(offered & (1u << stored));
	if (keepStored) {
		e.target = stored;
		e.available = false;
		entries.push_back(e);
	}

	if (count < 2 && !keepStored)
		entries.clear();
	return entries;
}

Common::U32String renderTargetLabel(const HiResTargetEntry &e) {
	Common::U32String label;
	switch (e.target) {
	case Graphics::kHiResTargetClut8:
		label = _("8-bit palette");
		break;
	case Graphics::kHiResTargetRgb565:
		label = _("16-bit colour");
		break;
	case Graphics::kHiResTargetRgb888:
		label = _("True colour");
		break;
	case Graphics::kHiResTargetAuto:
	default:
		label = _("Auto");
		break;
	}
	if (!e.available)
		label += _(" (not available here)");
	return label;
}

bool readRenderTarget(const Common::String &domain, Graphics::HiResRenderTarget &t) {
	if (!ConfMan.hasKey("render_target", domain))
		return false;
	return Graphics::parseRenderTarget(ConfMan.get("render_target", domain), t);
}

bool writeRenderTarget(const Common::String &domain, Graphics::HiResRenderTarget t) {
	const Common::String value = Graphics::renderTargetName(t);
	if (ConfMan.hasKey("render_target", domain) && ConfMan.get("render_target", domain) == value)
		return false;
	ConfMan.set("render_target", value, domain);
	return true;
}

} // End of namespace GUI
