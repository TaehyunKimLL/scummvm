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

// Opening a font's face chain, free of the engine's globals so the unit
// tests link it (test/engines/ags/hires_font_chain.h).

#include "common/stream.h"
#include "graphics/hires_text/chain_layout.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_file.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "ags/shared/font/hires_font_chain.h"

namespace AGS3 {

HiResFontChain::HiResFontChain() : source(nullptr), size(0), pixelPpem(0), bitmap(false), trueType(false) {
}

void HiResFontChain::free() {
	// The FallbackGlyphSource (or a NormalizedGlyphSource) owns the faces
	// behind it; a chain of one plain face is that face.
	delete source;
	source = nullptr;
	faces.clear();
	names.clear();
	pixelPpem = 0;
	bitmap = false;
	trueType = false;
}

namespace {

struct OpenedFace {
	Graphics::UnicodeGlyphSource *src;
	Common::String path;
	bool svf;
	int rowPad;
	bool pixel;		///< the plan's pixel face
};

} // End of anonymous namespace

bool openHiResFontChain(const HiResFontPlan &plan, int fontNumber, int size, const Common::Array<uint32> &fitProbes,
						HiResFontChain &out, HiResFaceOpenFn open) {
	out.free();
	out.warnings.clear();
	const int ttfSize = CLIP<int>(size, Graphics::TtfGlyphSource::kMinPixelSize, Graphics::TtfGlyphSource::kMaxPixelSize);
	bool sizeWarned = false;
	out.size = ttfSize;

	Common::Array<OpenedFace> opened;
	int firstSvf = -1;
	for (uint i = 0; i < plan.faces.size(); i++) {
		const Common::Path &path = plan.faces[i];
		const Common::String pathText = path.toString();
		// "<file>.ttc#<N>" names face N of a collection (font_face.h).
		int32 faceIndex = 0;
		Common::String error;
		Common::SeekableReadStream *stream = open(path, faceIndex, error);
		if (!stream) {
			// A plain path keeps its old warning; a "#<N>" one says why.
			Common::String unusedFile;
			int32 unusedIndex;
			if (Graphics::splitFontFaceIndex(path.baseName(), unusedFile, unusedIndex))
				out.warnings.push_back(Common::String::format("hires text: cannot open font '%s' (%s): %s",
															  pathText.c_str(), plan.source.c_str(), error.c_str()));
			else
				out.warnings.push_back(Common::String::format("hires text: cannot open font '%s' (%s)",
															  pathText.c_str(), plan.source.c_str()));
			continue;
		}

		byte head[4];
		const uint32 got = stream->read(head, sizeof(head));
		stream->seek(0);
		if (Graphics::isSvfnFile(head, got)) {
			Graphics::UnicodeGlyphSource *svf = Graphics::createSvfnSource(*stream, error);
			delete stream;
			if (!svf) {
				out.warnings.push_back(Common::String::format("hires text: %s '%s' is not a readable SVFN font",
															  plan.source.c_str(), pathText.c_str()));
				continue;
			}
			// Every SVFN font of a font shares the first one's cell height.
			if (firstSvf >= 0 && svf->cellHeight() != opened[firstSvf].src->cellHeight()) {
				out.warnings.push_back(Common::String::format(
					"HIRESTXT.MAP: %s: cell height %d differs from %s's %d on %d; not used", pathText.c_str(),
					svf->cellHeight(), opened[firstSvf].path.c_str(), opened[firstSvf].src->cellHeight(), fontNumber));
				delete svf;
				continue;
			}
			if (firstSvf < 0)
				firstSvf = opened.size();
			OpenedFace f = { svf, pathText, true, 0, false };
			opened.push_back(f);
			continue;
		}

		if (ttfSize != size && !sizeWarned) {
			out.warnings.push_back(Common::String::format("hires text: font %d cannot be drawn at %dpx, using %dpx",
														  fontNumber, size, ttfSize));
			sizeWarned = true;
		}
		// A pixel font (pixel=, the chain's first face) is held on its grid
		// in the size's cell, never shrunk by the fit; the faces behind it
		// are fitted as usual.
		const bool pixel = plan.pixel > 0 && i == 0;
		Graphics::TtfGlyphSource *ttf = pixel
			? Graphics::TtfGlyphSource::createPixel(stream, DisposeAfterUse::YES, ttfSize, plan.pixel, error, faceIndex)
			: Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, ttfSize, error,
			false, false, fitProbes.empty() ? nullptr : fitProbes.begin(), fitProbes.size(), faceIndex);
		if (!ttf) {
			out.warnings.push_back(Common::String::format("hires text: cannot use font '%s' at %dpx: %s",
														  pathText.c_str(), ttfSize, error.c_str()));
			continue;
		}
		ttf->setCoverageGamma(plan.gamma);
		if (pixel)
			out.pixelPpem = ttf->faceSize();
		OpenedFace f = { ttf, pathText, false, ttf->rowPad(), pixel };
		opened.push_back(f);
	}
	if (opened.empty())
		return false;

	// One cell for the chain: faces of another cell or depth are presented
	// in it at 8 bpp, so the fallback source reads every one of them.
	Common::Array<Graphics::ChainFaceInfo> infos;
	for (uint i = 0; i < opened.size(); i++) {
		Graphics::ChainFaceInfo info;
		info.cellWidth = opened[i].src->cellWidth();
		info.cellHeight = opened[i].src->cellHeight();
		info.bpp = opened[i].src->bitsPerPixel();
		info.baselineRow = opened[i].src->baselineRow();
		info.trueType = !opened[i].svf;
		info.rowPad = opened[i].rowPad;
		infos.push_back(info);
	}
	const Graphics::ChainLayout layout = Graphics::layoutFaceChain(infos, false, 0, 0);

	Common::Array<Graphics::UnicodeGlyphSource *> joined;
	for (uint i = 0; i < opened.size(); i++) {
		Graphics::UnicodeGlyphSource *src = opened[i].src;
		if (layout.normalize[i]) {
			Common::String error;
			// Owns the face from here on, and deletes it when it fails.
			src = Graphics::NormalizedGlyphSource::create(opened[i].src, layout.cellWidth, layout.cellHeight,
														  layout.tops[i], DisposeAfterUse::YES, error);
			if (!src) {
				out.warnings.push_back(Common::String::format("hires text: font %d: %s cannot join the face chain (%s)",
															  fontNumber, opened[i].path.c_str(), error.c_str()));
				if (opened[i].pixel)
					out.pixelPpem = 0;
				continue;
			}
		}
		// The fallback source reads every face with the first one's geometry
		if (!joined.empty() &&
			(src->cellWidth() != joined[0]->cellWidth() || src->cellHeight() != joined[0]->cellHeight() ||
			 src->bitsPerPixel() != joined[0]->bitsPerPixel())) {
			out.warnings.push_back(Common::String::format(
				"hires text: font %d: %s cannot join the face chain (cell %dx%d at %d bpp, the chain's %dx%d at %d bpp)",
				fontNumber, opened[i].path.c_str(), src->cellWidth(), src->cellHeight(), src->bitsPerPixel(),
				joined[0]->cellWidth(), joined[0]->cellHeight(), joined[0]->bitsPerPixel()));
			delete src;
			if (opened[i].pixel)
				out.pixelPpem = 0;
			continue;
		}
		joined.push_back(src);
		out.faces.push_back(opened[i].src);
		out.names.push_back(Common::Path(opened[i].path).baseName());
		out.bitmap = out.bitmap || opened[i].svf;
		out.trueType = out.trueType || !opened[i].svf;
	}
	if (joined.empty())
		return false;
	// A chain of SVFN fonts only: its cell is its size
	if (!out.trueType)
		out.size = joined[0]->cellHeight();
	out.source = (joined.size() == 1) ? joined[0]
				 : new Graphics::FallbackGlyphSource(joined, DisposeAfterUse::YES);
	return true;
}

} // namespace AGS3
