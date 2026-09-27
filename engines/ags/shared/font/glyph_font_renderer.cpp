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


#include "common/fs.h"
#include "common/stream.h"
#include "graphics/managed_surface.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "ags/lib/allegro/gfx.h"
#include "ags/lib/allegro/unicode.h"
#include "ags/shared/debugging/out.h"
#include "ags/shared/font/glyph_font_renderer.h"

namespace AGS3 {

using namespace AGS::Shared;

GlyphFontRenderer::~GlyphFontRenderer() {
	for (auto &it : _fontData) {
		FreeSources(*it._value);
		delete it._value;
	}
	_fontData.clear();
}

void GlyphFontRenderer::FreeSources(FontData &fd) {
	for (auto &it : fd.Scaled) {
		delete it._value->Source;
		delete it._value;
	}
	fd.Scaled.clear();
	// The FallbackGlyphSource owns the chain; a chain of one is the source.
	delete fd.Source;
	fd.Source = nullptr;
	fd.Chain.clear();
	fd.Names.clear();
	fd.Drawer.setSource(nullptr);
}

bool GlyphFontRenderer::Build(FontData &fd, const Common::Array<uint32> &fitProbes, bool warn) {
	const HiResFontPlan &plan = fd.Plan;
	if (plan.kind == HiResFontPlan::kBitmap) {
		Common::FSNode node(plan.bitmap);
		Common::SeekableReadStream *stream = (node.exists() && !node.isDirectory()) ? node.createReadStream() : nullptr;
		Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
		const bool ok = stream && font->load(*stream);
		delete stream;
		if (!ok) {
			if (warn)
				Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: %s '%s' is not a readable SVFN font",
							  plan.source.c_str(), plan.bitmap.toString().c_str());
			delete font;
			return false;
		}
		fd.Source = new Graphics::SvfnGlyphSource(font, DisposeAfterUse::YES);
		fd.Chain.push_back(fd.Source);
		fd.Names.push_back(plan.bitmap.baseName().c_str());
		fd.Size = fd.Source->cellHeight();
		fd.Name = fd.Names[0];
		return true;
	}

	Common::Array<Graphics::UnicodeGlyphSource *> sources;
	fd.PixelPpem = 0;
	for (uint i = 0; i < plan.faces.size(); i++) {
		const Common::Path &path = plan.faces[i];
		// "<file>.ttc#<N>" names face N of a collection (font_face.h).
		int32 faceIndex = 0;
		Common::String error;
		Common::SeekableReadStream *stream = Graphics::openFontFace(path, faceIndex, error);
		if (!stream) {
			// A plain path keeps its old warning; a "#<N>" one says why.
			Common::String unusedFile;
			int32 unusedIndex;
			if (warn && Graphics::splitFontFaceIndex(path.baseName(), unusedFile, unusedIndex))
				Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: cannot open font '%s' (%s): %s",
							  path.toString().c_str(), plan.source.c_str(), error.c_str());
			else if (warn)
				Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: cannot open font '%s' (%s)",
							  path.toString().c_str(), plan.source.c_str());
			continue;
		}
		// A pixel font (pixel=, the chain's first face) is held on its grid
		// in the fd.Size cell, never shrunk by the fit (C28); the faces
		// behind it are fitted as usual.
		Graphics::TtfGlyphSource *ttf = (plan.pixel > 0 && i == 0)
			? Graphics::TtfGlyphSource::createPixel(stream, DisposeAfterUse::YES, fd.Size, plan.pixel, error, faceIndex)
			: Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES, fd.Size, error,
			false, false, fitProbes.empty() ? nullptr : fitProbes.begin(), fitProbes.size(), faceIndex);
		if (!ttf) {
			if (warn)
				Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: cannot use font '%s' at %dpx: %s",
							  path.toString().c_str(), fd.Size, error.c_str());
			continue;
		}
		ttf->setCoverageGamma(plan.gamma);
		if (plan.pixel > 0 && i == 0)
			fd.PixelPpem = ttf->faceSize();
		sources.push_back(ttf);
		fd.Names.push_back(path.baseName().c_str());
	}
	if (sources.empty())
		return false;
	fd.Chain = sources;
	// Every face is opened at one size, so their cells agree and the
	// fallback source reads any of them.
	fd.Source = (sources.size() == 1) ? sources[0]
				: new Graphics::FallbackGlyphSource(sources, DisposeAfterUse::YES);
	fd.Name = fd.Names[0];
	return true;
}

bool GlyphFontRenderer::Attach(int fontNumber, const HiResFontPlan &plan, int gameHeight, bool alpha,
							   IAGSFontRendererInternal *game, const FontRenderParams &params) {
	FontData *fd = new FontData();
	fd->Plan = plan;
	fd->Game = game;
	fd->Params = params;
	fd->Size = plan.size > 0 ? plan.size * MAX(1, params.SizeMultiplier) : gameHeight;
	if (plan.kind == HiResFontPlan::kFaces &&
		(fd->Size < Graphics::TtfGlyphSource::kMinPixelSize || fd->Size > Graphics::TtfGlyphSource::kMaxPixelSize)) {
		const int size = CLIP<int>(fd->Size, Graphics::TtfGlyphSource::kMinPixelSize, Graphics::TtfGlyphSource::kMaxPixelSize);
		Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: font %d cannot be drawn at %dpx, using %dpx",
					  fontNumber, fd->Size, size);
		fd->Size = size;
	}
	if (!Build(*fd, Common::Array<uint32>(), true)) {
		Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: %s names no usable font; font %d stays the game's",
					  plan.source.c_str(), fontNumber);
		delete fd;
		return false;
	}
	fd->Drawer.setSource(fd->Source);
	fd->Drawer.setAlpha(alpha);
	fd->Fallback.set(game, fontNumber);
	_fontData[fontNumber] = fd;

	Common::String names;
	for (uint i = 0; i < fd->Names.size(); i++)
		names += (i ? ", " : "") + Common::String(fd->Names[i].GetCStr());
	Debug::Printf(kDbgMsg_Info, "hires text: font %d from %s: %s at %dpx (cell %dx%d), alpha %s",
				  fontNumber, plan.source.c_str(), names.c_str(), fd->Size,
				  fd->Source->cellWidth(), fd->Source->cellHeight(), alpha ? "on" : "off");
	return true;
}

void GlyphFontRenderer::SetTranslationSample(const Common::Array<uint32> &sample) {
	if (sample.empty())
		return;
	for (auto &it : _fontData) {
		FontData &fd = *it._value;
		// A TrueType chain is opened again with the translation's own
		// characters in the vertical fit (Thai marks, Japanese brackets).
		// A pixel face (the chain's first, pixel=) ignores the sample and
		// opens as before; the faces behind it are fitted to it.
		if (fd.Plan.kind == HiResFontPlan::kFaces) {
			FontData fresh;
			fresh.Plan = fd.Plan;
			fresh.Size = fd.Size;
			const uint n = MIN<uint>(sample.size(), Graphics::TtfGlyphSource::kMaxExtraFitProbes);
			Common::Array<uint32> probes(sample.begin(), n);
			if (Build(fresh, probes, false)) {
				FreeSources(fd);
				fd.Source = fresh.Source;
				fd.Chain = fresh.Chain;
				fd.Names = fresh.Names;
				fd.PixelPpem = fresh.PixelPpem;
				fd.Drawer.setSource(fd.Source);
				fd.FitProbes = probes;
				fresh.Source = nullptr;
			}
		}
		// Each face is asked for what the faces before it lack.
		Common::Array<uint32> wanted = sample;
		for (uint i = 0; i < fd.Chain.size() && !wanted.empty(); i++) {
			Graphics::UnicodeGlyphSource *src = fd.Chain[i];
			const Graphics::CoverageReport report = Graphics::checkCoverage(src, wanted);
			const Common::String fallback = (i + 1 < fd.Chain.size()) ? Common::String(fd.Names[i + 1].GetCStr())
				: Common::String::format("the game's font %d", it._key);
			const Common::String text = Graphics::coverageWarning(fd.Names[i].GetCStr(), report, fallback);
			if (!text.empty()) {
				Common::String line;
				for (uint c = 0; c <= text.size(); c++) {
					if (c == text.size() || text[c] == '\n') {
						if (!line.empty())
							Debug::Printf(kDbgMsg_Warn, "WARNING: font %d: %s", it._key, line.c_str());
						line.clear();
					} else {
						line += text[c];
					}
				}
			}
			Common::Array<uint32> missing;
			for (uint k = 0; k < wanted.size(); k++) {
				if (src->cells(wanted[k]) <= 0)
					missing.push_back(wanted[k]);
			}
			wanted = missing;
		}
	}
}

bool GlyphFontRenderer::IsGameBitmapFont(int fontNumber) {
	auto it = _fontData.find(fontNumber);
	return it != _fontData.end() && it->_value->Game && it->_value->Game->IsBitmapFont();
}

void GlyphFontRenderer::FreeMemory(int fontNumber) {
	auto it = _fontData.find(fontNumber);
	if (it == _fontData.end())
		return;
	FontData *fd = it->_value;
	_fontData.erase(it);
	if (fd->Game)
		fd->Game->FreeMemory(fontNumber);
	FreeSources(*fd);
	delete fd;
}

void GlyphFontRenderer::Decode(const char *text) {
	_cps.resize(0);
	for (int cp = ugetxc(&text); cp; cp = ugetxc(&text))
		_cps.push_back((uint32)cp);
}

int GlyphFontRenderer::GetTextWidth(const char *text, int fontNumber) {
	FontData &fd = *_fontData[fontNumber];
	Decode(text);
	return fd.Drawer.textWidth(_cps.begin(), _cps.size(), &fd.Fallback);
}

int GlyphFontRenderer::GetTextHeight(const char *text, int fontNumber) {
	return GetFontHeight(fontNumber);
}

void GlyphFontRenderer::RenderText(const char *text, int fontNumber, BITMAP *destination, int x, int y, int colour) {
	FontData &fd = *_fontData[fontNumber];
	if (y > destination->cb)  // optimisation, as the other renderers
		return;
	Decode(text);
	// Allegro's clip is inclusive; the drawer's is exclusive at right/bottom.
	const Common::Rect clip = destination->clip ?
		Common::Rect(destination->cl, destination->ct, destination->cr + 1, destination->cb + 1) :
		Common::Rect(0, 0, destination->w, destination->h);
	fd.Fallback.target(destination);
	fd.Drawer.drawText(*destination->getSurface().surfacePtr(), clip, _cps.begin(), _cps.size(), x, y,
					   (uint32)colour, &fd.Fallback);
	fd.Fallback.target(nullptr);
}

GlyphFontRenderer::ScaledChain *GlyphFontRenderer::GetScaled(FontData &fd, int fontNumber, int scale) {
	auto it = fd.Scaled.find(scale);
	if (it != fd.Scaled.end())
		return it->_value->Source ? it->_value : nullptr;

	ScaledChain *sc = new ScaledChain();
	sc->N = scale;
	sc->Small = &fd.Chain;
	fd.Scaled[scale] = sc;
	// An SVFN font has one size: it is upscaled (section 4.3)
	if (fd.Plan.kind != HiResFontPlan::kFaces) {
		Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: font %d (%s) has no %dx faces; it is upscaled",
					  fontNumber, fd.Name.GetCStr(), scale);
		return nullptr;
	}
	FontData big;
	// A pixel face opens at N x its 1x ppem, so it lines up with the N x pens.
	big.Plan = scaledPlan(fd.Plan, scale, fd.PixelPpem);
	big.Size = fd.Size * scale;
	bool ok = big.Size <= Graphics::TtfGlyphSource::kMaxPixelSize && Build(big, fd.FitProbes, false);
	// The same faces, in the same order: rowShift() pairs them up
	if (ok && big.Names.size() == fd.Names.size()) {
		for (uint i = 0; i < big.Names.size(); i++)
			ok = ok && big.Names[i] == fd.Names[i];
	} else {
		ok = false;
	}
	if (!ok) {
		FreeSources(big);
		Debug::Printf(kDbgMsg_Warn, "WARNING: hires text: font %d (%s) cannot be opened at %dpx; its %dx text is upscaled",
					  fontNumber, fd.Name.GetCStr(), fd.Size * scale, scale);
		return nullptr;
	}
	sc->Source = big.Source;
	sc->Chain = big.Chain;
	big.Source = nullptr;
	Debug::Printf(kDbgMsg_Info, "hires text: font %d at %dx: %dpx (cell %dx%d)", fontNumber, scale, fd.Size * scale,
				  sc->Source->cellWidth(), sc->Source->cellHeight());
	return sc;
}

int GlyphFontRenderer::ScaledChain::rowShift(uint32 cp) {
	// Every entry of both chains is a TtfGlyphSource: GetScaled() builds a
	// ScaledChain only for a kFaces plan, whose Build() opens TrueType faces
	// and nothing else, and only when both chains name the same faces.
	assert(Small && Chain.size() == Small->size());
	// The face that draws cp, at both sizes: its N x baseline goes to N x
	// its game-size baseline
	for (uint i = 0; i < Chain.size() && i < Small->size(); i++) {
		if (Chain[i]->cells(cp) > 0)
			return N * static_cast<Graphics::TtfGlyphSource *>((*Small)[i])->baseline() -
				static_cast<Graphics::TtfGlyphSource *>(Chain[i])->baseline();
	}
	return 0;
}

bool GlyphFontRenderer::RenderTextScaled(const char *text, int fontNumber, BITMAP *destination, int x, int y,
										 int colour, int scale) {
	auto it = _fontData.find(fontNumber);
	if (it == _fontData.end())
		return false;
	FontData &fd = *it->_value;
	ScaledChain *sc = GetScaled(fd, fontNumber, scale);
	if (!sc)
		return false;
	if (y * scale > destination->cb)
		return true;
	Decode(text);
	const Common::Rect clip = destination->clip ?
		Common::Rect(destination->cl, destination->ct, destination->cr, destination->cb) :
		Common::Rect(0, 0, destination->w, destination->h);
	fd.Fallback.target(destination);
	fd.Drawer.drawTextScaled(*destination->getSurface().surfacePtr(), clip, _cps.begin(), _cps.size(), x, y,
							 (uint32)colour, &fd.Fallback, *sc);
	fd.Fallback.target(nullptr);
	return true;
}

const char *GlyphFontRenderer::GetFontName(int fontNumber) {
	auto it = _fontData.find(fontNumber);
	return it != _fontData.end() ? it->_value->Name.GetCStr() : "";
}

int GlyphFontRenderer::GetFontHeight(int fontNumber) {
	return _fontData[fontNumber]->Source->cellHeight();
}

void GlyphFontRenderer::GetFontMetrics(int fontNumber, FontMetrics *metrics) {
	const int h = GetFontHeight(fontNumber);
	*metrics = FontMetrics();
	metrics->NominalHeight = h;
	metrics->RealHeight = h;
	metrics->CompatHeight = h;
	metrics->VExtent = std::make_pair(0, h);
}

void GlyphFontRenderer::AdjustFontForAntiAlias(int fontNumber, bool aa_mode) {
	auto it = _fontData.find(fontNumber);
	if (it != _fontData.end() && it->_value->Game)
		it->_value->Game->AdjustFontForAntiAlias(fontNumber, aa_mode);
}

int GlyphFontRenderer::GameFallback::charWidth(uint32 cp) {
	if (!_game)
		return 0;
	char buf[8] = { 0 };
	usetc(buf, (int)cp);
	return _game->GetTextWidth(buf, _font);
}

void GlyphFontRenderer::GameFallback::drawChar(uint32 cp, int x, int y, uint32 colour) {
	if (!_game || !_dst)
		return;
	char buf[8] = { 0 };
	usetc(buf, (int)cp);
	_game->RenderText(buf, _font, _dst, x, y, (int)colour);
}

void GlyphFontRenderer::GameFallback::drawCharScaled(uint32 cp, int x, int y, uint32 colour, int scale) {
	// The game's font at game resolution into a scratch cell, then upscaled:
	// the character looks as it does today (section 4.3)
	if (!_game || !_dst)
		return;
	char buf[8] = { 0 };
	usetc(buf, (int)cp);
	const int w = _game->GetTextWidth(buf, _font);
	const int h = _game->GetTextHeight(buf, _font);
	if (w <= 0 || h <= 0)
		return;
	// One scratch cell, kept and grown (one per font's fallback)
	const int depth = bitmap_color_depth(_dst);
	if (!_cell || bitmap_color_depth(_cell) != depth || _cell->w < w || _cell->h < h) {
		const int cw = (_cell && bitmap_color_depth(_cell) == depth) ? MAX<int>(w, _cell->w) : w;
		const int ch = (_cell && bitmap_color_depth(_cell) == depth) ? MAX<int>(h, _cell->h) : h;
		if (_cell)
			destroy_bitmap(_cell);
		_cell = create_bitmap_ex(depth, cw, ch);
		if (!_cell)
			return;
	}
	const uint32 key = bitmap_mask_color(_cell);
	set_clip_rect(_cell, 0, 0, w - 1, h - 1);
	clear_to_color(_cell, key);
	_game->RenderText(buf, _font, _cell, 0, 0, (int)colour);
	const Common::Rect clip = _dst->clip ? Common::Rect(_dst->cl, _dst->ct, _dst->cr, _dst->cb)
										 : Common::Rect(0, 0, _dst->w, _dst->h);
	const Graphics::Surface area = _cell->getSurface().surfacePtr()->getSubArea(Common::Rect(0, 0, w, h));
	GlyphTextDrawer::upscaleOnto(*_dst->getSurface().surfacePtr(), clip, area, key, x * scale, y * scale, scale);
}

GlyphFontRenderer::GameFallback::~GameFallback() {
	if (_cell)
		destroy_bitmap(_cell);
}

} // namespace AGS3
