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

#include "graphics/hires_text/font_map.h"

#include "common/stream.h"
#include "common/util.h"

namespace Graphics {

HiResFontScope::HiResFontScope()
	: faceSet(false), size(0), sizeSet(false), shift(0), shiftSet(false), cell(kHiResCellGame), cellSet(false),
	  align(kHiResAlignGame), alignSet(false), missing(0), missingSet(false), advance(kHiResAdvanceFont), advanceSet(false) {
}

BreakRules HiResLayoutSettings::apply(const BreakRules &rules) const {
	BreakRules r = rules;
	if (hangulSet)
		r.hangul = hangul;
	if (kinsokuSet)
		r.kinsoku = kinsoku;
	if (thaiSet)
		r.thaiFallback = thai;
	return r;
}

void HiResMap::clear() {
	version = 0;
	target = kHiResTargetAuto;
	targetSet = false;
	blend = kHiResBlendAuto;
	blendSet = false;
	scale = 0;
	scaleSet = false;
	layout = HiResLayoutSettings();
	faces.clear();
	font = HiResFontScope();
	fontIds.clear();
	loads.clear();
	preloads.clear();
	warnings.clear();
}

const HiResFontScope *HiResMap::fontIdScope(int id) const {
	Common::HashMap<int, HiResFontScope>::const_iterator it = fontIds.find(id);
	return it == fontIds.end() ? nullptr : &it->_value;
}

HiResLoadMode HiResMap::loadModeFor(const HiResFaceRef &face) const {
	return loads.getValOrDefault(face.name, kHiResLoadAuto);
}

namespace HiResFontMap {

const char *const kDataPrefix = "data:";
namespace {

struct Key {
	Common::String key, value;
	int line;
};

struct Section {
	Common::String base;    ///< lower case, without qualifiers
	Common::String engine;  ///< lower case; empty: none
	Common::String target;  ///< clut8, rgb; empty: none
	int line;
	Common::Array<Key> keys;
};

bool isEngineQualifier(const Common::String &q) {
	return q == "sci" || q == "scumm" || q == "agi";
}

bool isTargetQualifier(const Common::String &q) {
	return q == "clut8" || q == "rgb";
}

// A ';' or '#' after a space or a tab starts a comment.
Common::String stripComment(const Common::String &line) {
	for (uint i = 1; i < line.size(); ++i) {
		if ((line[i] == ';' || line[i] == '#') && (line[i - 1] == ' ' || line[i - 1] == '\t'))
			return Common::String(line.c_str(), i);
	}
	return line;
}

bool parseInt(const Common::String &text, int lo, int hi, int &out) {
	if (text.empty())
		return false;
	char *end = nullptr;
	const long v = strtol(text.c_str(), &end, 10);
	if (*end || v < lo || v > hi)
		return false;
	out = (int)v;
	return true;
}

class Loader {
public:
	Loader(const Common::String &name, const Common::Path &mapDir, HiResMap &out) : _name(name), _dir(mapDir), _out(out) {}

	void warn(int line, const Common::String &msg) {
		_out.warnings.push_back(Common::String::format("%s:%d: %s", _name.c_str(), line, msg.c_str()));
	}

	bool read(Common::SeekableReadStream &stream, Common::Array<Section> &sections) {
		int lineNo = 0;
		while (!stream.eos() && !stream.err()) {
			Common::String line = stream.readLine();
			++lineNo;
			if (lineNo == 1 && line.size() >= 3 && (byte)line[0] == 0xEF && (byte)line[1] == 0xBB && (byte)line[2] == 0xBF)
				line.erase(0, 3);
			line.trim();
			if (line.empty() || line[0] == ';' || line[0] == '#')
				continue;
			line = stripComment(line);
			line.trim();
			if (line[0] == '[') {
				if (line.lastChar() != ']') {
					warn(lineNo, "a section line must end with ']'");
					return false;
				}
				Section s;
				s.line = lineNo;
				Common::String body(line.c_str() + 1, line.size() - 2);
				body.toLowercase();
				const char *colon = strchr(body.c_str(), ':');
				s.base = colon ? Common::String(body.c_str(), colon) : body;
				while (colon) {
					const char *next = strchr(colon + 1, ':');
					const Common::String q = next ? Common::String(colon + 1, next) : Common::String(colon + 1);
					if (isEngineQualifier(q) && s.engine.empty())
						s.engine = q;
					else if (isTargetQualifier(q) && s.target.empty())
						s.target = q;
					else {
						warn(lineNo, "unknown qualifier '" + q + "' (sci, scumm, agi, clut8, rgb); section ignored");
						s.base.clear();
						break;
					}
					colon = next;
				}
				sections.push_back(s);
				continue;
			}
			const char *eq = strchr(line.c_str(), '=');
			if (!eq || sections.empty()) {
				warn(lineNo, sections.empty() ? "a key before the first section" : "not a key=value line");
				return false;
			}
			Key k;
			k.key = Common::String(line.c_str(), eq);
			k.value = Common::String(eq + 1);
			k.key.trim();
			k.key.toLowercase();
			k.value.trim();
			k.line = lineNo;
			sections.back().keys.push_back(k);
		}
		return true;
	}

	// The sections named @p base that apply, least specific first: S, S:t, S:e, S:e:t.
	Common::Array<const Section *> select(const Common::Array<Section> &sections, const Common::String &base,
	                                      const HiResMapQualifiers &q) {
		Common::Array<const Section *> out;
		const Common::String t = targetQualifier(q.target);
		for (int rank = 0; rank < 4; ++rank) {
			for (uint i = 0; i < sections.size(); ++i) {
				const Section &s = sections[i];
				if (s.base != base)
					continue;
				const bool e = !s.engine.empty(), tq = !s.target.empty();
				if (e && s.engine != q.engine)
					continue;
				if (tq && s.target != t)
					continue;
				if ((int)e * 2 + (int)tq == rank)
					out.push_back(&s);
			}
		}
		return out;
	}

	bool parseValue(const Common::String &text, int line, HiResFontValue &out) {
		out = HiResFontValue();
		Common::String rest = text;
		while (!rest.empty()) {
			const char *comma = strchr(rest.c_str(), ',');
			Common::String item = comma ? Common::String(rest.c_str(), comma) : rest;
			rest = comma ? Common::String(comma + 1) : Common::String();
			item.trim();
			if (item.empty())
				continue;
			if (out.endsInOriginal) {
				warn(line, "entries after 'original' are ignored: '" + item + "'");
				continue;
			}
			if (item.equalsIgnoreCase("original")) {
				out.endsInOriginal = true;
			} else if (item.equalsIgnoreCase("same")) {
				if (out.same)
					warn(line, "'same' twice; the second is ignored");
				out.same = true;
				out.sameAt = out.faces.size();
			} else if (_out.faces.contains(item)) {
				out.faces.push_back(_out.faces[item]);
			} else if (item.contains('.') || item.contains('/') || item.hasPrefix(kDataPrefix)) {
				out.faces.push_back(resolveFace(item, _dir));
			} else {
				warn(line, "unknown face name '" + item + "'");
			}
		}
		return !out.empty();
	}

	void parseScope(const Common::Array<const Section *> &list, HiResFontScope &s) {
		Common::Array<uint> rangeFrom, advanceFrom;	// the section each range or advance rule came from
		for (uint i = 0; i < list.size(); ++i) {
			for (uint k = 0; k < list[i]->keys.size(); ++k) {
				const Key &kv = list[i]->keys[k];
				const Common::String &v = kv.value;
				if (kv.key == "face") {
					HiResFontValue fv;
					if (parseValue(v, kv.line, fv)) {
						s.face = fv;
						s.faceSet = true;
					}
				} else if (kv.key == "size") {
					if (parseInt(v, 4, 255, s.size))
						s.sizeSet = true;
					else
						warn(kv.line, "size '" + v + "' is not 4..255");
				} else if (kv.key == "shift") {
					if (parseInt(v, -32, 32, s.shift))
						s.shiftSet = true;
					else
						warn(kv.line, "shift '" + v + "' is not -32..32");
				} else if (kv.key == "cell") {
					if (v.equalsIgnoreCase("game") || v.equalsIgnoreCase("glyph")) {
						s.cell = v.equalsIgnoreCase("game") ? kHiResCellGame : kHiResCellGlyph;
						s.cellSet = true;
					} else
						warn(kv.line, "cell '" + v + "' is not game or glyph");
				} else if (kv.key == "align") {
					if (v.equalsIgnoreCase("game"))
						s.align = kHiResAlignGame;
					else if (v.equalsIgnoreCase("cell"))
						s.align = kHiResAlignCell;
					else if (v.equalsIgnoreCase("font"))
						s.align = kHiResAlignFont;
					else {
						warn(kv.line, "align '" + v + "' is not game, cell or font");
						continue;
					}
					s.alignSet = true;
				} else if (kv.key == "missing") {
					if (v.equalsIgnoreCase("off")) {
						s.missing = 0;
						s.missingSet = true;
					} else if (parseCodePoint(v, s.missing))
						s.missingSet = true;
					else
						warn(kv.line, "missing '" + v + "' is not a code point or off");
				} else if (kv.key == "advance") {
					if (parseAdvance(v, s.advance))
						s.advanceSet = true;
					else
						warn(kv.line, "advance '" + v + "' is not font, game or cell");
				} else if (kv.key.hasPrefix("range.") || kv.key.hasPrefix("advance.")) {
					const bool range = kv.key.hasPrefix("range.");
					HiResRangeSpec spec;
					Common::String error;
					if (!parseRangeSpec(Common::String(kv.key.c_str() + (range ? 6 : 8)), spec, error)) {
						warn(kv.line, error);
						continue;
					}
					// A spec already set (the same span, however spelled) is replaced: a qualified
					// section is more specific than the base one, and a later line beats an earlier.
					Common::Array<HiResRangeSpec> &specs = range ? s.rangeSpecs : s.advanceSpecs;
					Common::Array<uint> &from = range ? rangeFrom : advanceFrom;
					uint at = specs.size();
					for (uint e = 0; e < specs.size(); ++e) {
						if (specs[e].kind == spec.kind && (spec.kind != kHiResSpecSpan ||
						    (specs[e].span.lo == spec.span.lo && specs[e].span.hi == spec.span.hi))) {
							at = e;
							break;
						}
					}
					if (at < specs.size() && from[at] == i)
						warn(kv.line, "'" + kv.key + "' repeats a range of this section; the later line wins");
					if (range) {
						HiResFontValue fv;
						if (!parseValue(v, kv.line, fv))
							continue;
						if (at < specs.size())
							s.rangeValues[at] = fv;
						else
							s.rangeValues.push_back(fv);
					} else {
						HiResAdvance a;
						if (!parseAdvance(v, a)) {
							warn(kv.line, "advance '" + v + "' is not font, game or cell");
							continue;
						}
						if (at < specs.size())
							s.advanceValues[at] = a;
						else
							s.advanceValues.push_back(a);
					}
					if (at < specs.size())
						specs[at] = spec;
					else
						specs.push_back(spec);
					if (at < from.size())
						from[at] = i;
					else
						from.push_back(i);
				} else if (kv.key == "pixel" || kv.key == "origin" || kv.key.hasPrefix("origin.") || kv.key == "mirror") {
					warn(kv.line, "'" + kv.key + "' was removed in map version 3; delete it");
				} else {
					warn(kv.line, "unknown key '" + kv.key + "'");
				}
			}
		}
	}

	static bool parseAdvance(const Common::String &v, HiResAdvance &out) {
		if (v.equalsIgnoreCase("font"))
			out = kHiResAdvanceFont;
		else if (v.equalsIgnoreCase("game"))
			out = kHiResAdvanceGame;
		else if (v.equalsIgnoreCase("cell"))
			out = kHiResAdvanceCell;
		else
			return false;
		return true;
	}

private:
	Common::String _name;
	Common::Path _dir;
	HiResMap &_out;
};

} // End of anonymous namespace

const char *targetQualifier(HiResRenderTarget target) {
	switch (target) {
	case kHiResTargetClut8:
		return "clut8";
	case kHiResTargetRgb565:
	case kHiResTargetRgb888:
		return "rgb";
	default:
		return "";
	}
}

bool parseRenderTarget(const Common::String &text, HiResRenderTarget &out) {
	static const char *const names[] = { "auto", "clut8", "rgb565", "rgb888" };
	for (int i = 0; i < 4; ++i) {
		if (text.equalsIgnoreCase(names[i])) {
			out = (HiResRenderTarget)i;
			return true;
		}
	}
	return false;
}

bool parseBlend(const Common::String &text, HiResBlend &out) {
	if (text.equalsIgnoreCase("auto"))
		out = kHiResBlendAuto;
	else if (text.equalsIgnoreCase("on"))
		out = kHiResBlendOn;
	else if (text.equalsIgnoreCase("off"))
		out = kHiResBlendOff;
	else
		return false;
	return true;
}

bool parseBool(const Common::String &text, bool &out) {
	if (text.equalsIgnoreCase("on")) {
		out = true;
		return true;
	}
	if (text.equalsIgnoreCase("off")) {
		out = false;
		return true;
	}
	return !text.empty() && Common::parseBool(text, out);
}

bool parseCodePoint(const Common::String &text, uint32 &out) {
	const char *p = text.c_str();
	if ((p[0] == 'u' || p[0] == 'U') && p[1] == '+')
		p += 2;
	else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	else
		return false;
	const size_t n = strlen(p);
	if (n < 1 || n > 6)
		return false;
	uint32 v = 0;
	for (; *p; ++p) {
		if (!Common::isXDigit(*p))
			return false;
		v = v * 16 + (uint32)(Common::isDigit(*p) ? *p - '0' : (*p | 0x20) - 'a' + 10);
	}
	if (v > 0x10FFFF)
		return false;
	out = v;
	return true;
}

HiResFaceRef resolveFace(const Common::String &value, const Common::Path &mapDir) {
	HiResFaceRef ref;
	ref.name = value;
	if (value.hasPrefix(kDataPrefix)) {
		ref.data = true;
		ref.path = Common::Path(value.c_str() + strlen(kDataPrefix), '/');
	} else if (!value.empty() && (value[0] == '/' || value[0] == '\\' || (value.size() > 2 && value[1] == ':'))) {
		ref.path = Common::Path(value, value.contains('\\') ? '\\' : '/');
	} else {
		ref.path = mapDir.join(Common::Path(value, '/'));
	}
	return ref;
}

bool load(Common::SeekableReadStream &stream, const Common::String &name, const Common::Path &mapDir,
          const HiResMapQualifiers &qualifiers, HiResMap &out) {
	out.clear();
	Loader loader(name, mapDir, out);
	Common::Array<Section> sections;
	if (!loader.read(stream, sections))
		return false;

	// [map] version: the gate.
	Common::Array<const Section *> list = loader.select(sections, "map", HiResMapQualifiers());
	for (uint i = 0; i < list.size(); ++i)
		for (uint k = 0; k < list[i]->keys.size(); ++k)
			if (list[i]->keys[k].key == "version" && !parseInt(list[i]->keys[k].value, 1, 99, out.version))
				out.version = -1;
	if (out.version != 3) {
		if (out.version == 2)
			loader.warn(1, "map version 2 is not read: set [map] version=3 and delete pixel=, gamma=, origin=, mirror=, "
			               "[text], [shadow] and [glyphs]");
		else
			loader.warn(1, "no [map] version=3: not a hi-res text map of this ScummVM");
		const Common::Array<Common::String> w = out.warnings;
		out.clear();
		out.warnings = w;
		return false;
	}

	// Sections nobody reads any more, and unknown ones.
	for (uint i = 0; i < sections.size(); ++i) {
		const Common::String &b = sections[i].base;
		if (b.empty() || b == "map" || b == "render" || b == "layout" || b == "fonts" || b == "font" ||
		    b == "load" || b == "preload")
			continue;
		if (b.hasPrefix("font.")) {
			int id;
			if (!parseInt(Common::String(b.c_str() + 5), 0, 65535, id))
				loader.warn(sections[i].line, "[" + b + "]: the font id must be 0..65535");
			continue;
		}
		if (b == "text" || b == "shadow" || b.hasPrefix("shadow.") || b == "glyphs" || b.hasPrefix("glyphs."))
			loader.warn(sections[i].line, "[" + b + "] was removed in map version 3; delete it");
		else
			loader.warn(sections[i].line, "unknown section [" + b + "]");
	}

	list = loader.select(sections, "render", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			if (kv.key == "target") {
				if (parseRenderTarget(kv.value, out.target))
					out.targetSet = true;
				else
					loader.warn(kv.line, "target '" + kv.value + "' is not auto, clut8, rgb565 or rgb888");
			} else if (kv.key == "blend") {
				if (parseBlend(kv.value, out.blend))
					out.blendSet = true;
				else
					loader.warn(kv.line, "blend '" + kv.value + "' is not auto, on or off");
			} else if (kv.key == "scale") {
				if (kv.value.equalsIgnoreCase("auto")) {
					out.scale = 0;
					out.scaleSet = true;
				} else if (parseInt(kv.value, 1, 2, out.scale))
					out.scaleSet = true;
				else
					loader.warn(kv.line, "scale '" + kv.value + "' is not auto, 1 or 2");
			} else if (kv.key == "gamma" || kv.key == "pixel") {
				loader.warn(kv.line, "'" + kv.key + "' was removed in map version 3; delete it");
			} else {
				loader.warn(kv.line, "unknown key '" + kv.key + "'");
			}
		}
	}

	list = loader.select(sections, "layout", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			bool b;
			if (kv.key == "hangul") {
				if (kv.value.equalsIgnoreCase("word") || kv.value.equalsIgnoreCase("any")) {
					out.layout.hangul = kv.value.equalsIgnoreCase("any") ? kHangulBreakAny : kHangulBreakWord;
					out.layout.hangulSet = true;
				} else
					loader.warn(kv.line, "hangul '" + kv.value + "' is not word or any");
			} else if ((kv.key == "kinsoku" || kv.key == "thai") && HiResFontMap::parseBool(kv.value, b)) {
				(kv.key == "kinsoku" ? out.layout.kinsoku : out.layout.thai) = b;
				(kv.key == "kinsoku" ? out.layout.kinsokuSet : out.layout.thaiSet) = true;
			} else {
				loader.warn(kv.line, "unknown key or value '" + kv.key + "=" + kv.value + "'");
			}
		}
	}

	list = loader.select(sections, "fonts", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			bool valid = !kv.key.empty() && kv.key != "same" && kv.key != "original" && kv.key != "data";
			for (uint c = 0; valid && c < kv.key.size(); ++c)
				valid = Common::isAlnum(kv.key[c]) || kv.key[c] == '_' || kv.key[c] == '-';
			if (!valid || kv.value.empty()) {
				loader.warn(kv.line, "'" + kv.key + "' is not a face name (letters, digits, _ and -; not same, original, data)");
				continue;
			}
			HiResFaceRef ref = resolveFace(kv.value, mapDir);
			ref.name = kv.key;
			out.faces[kv.key] = ref;
		}
	}

	list = loader.select(sections, "load", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			HiResLoadMode m;
			if (kv.value.equalsIgnoreCase("auto"))
				m = kHiResLoadAuto;
			else if (kv.value.equalsIgnoreCase("whole"))
				m = kHiResLoadWhole;
			else if (kv.value.equalsIgnoreCase("stream"))
				m = kHiResLoadStream;
			else {
				loader.warn(kv.line, "load '" + kv.value + "' is not auto, whole or stream");
				continue;
			}
			if (!out.faces.contains(kv.key))
				loader.warn(kv.line, "[load] names '" + kv.key + "', which [fonts] does not");
			else
				out.loads[kv.key] = m;
		}
	}

	list = loader.select(sections, "preload", qualifiers);
	for (uint i = 0; i < list.size(); ++i) {
		for (uint k = 0; k < list[i]->keys.size(); ++k) {
			const Key &kv = list[i]->keys[k];
			if (!out.faces.contains(kv.key))
				loader.warn(kv.line, "[preload] names '" + kv.key + "', which [fonts] does not");
			else
				out.preloads[kv.key] = resolveFace(kv.value, mapDir);
		}
	}

	loader.parseScope(loader.select(sections, "font", qualifiers), out.font);
	for (uint i = 0; i < sections.size(); ++i) {
		int id;
		if (!sections[i].base.hasPrefix("font.") || !parseInt(Common::String(sections[i].base.c_str() + 5), 0, 65535, id) ||
		    out.fontIds.contains(id))
			continue;
		loader.parseScope(loader.select(sections, sections[i].base, qualifiers), out.fontIds[id]);
	}
	return true;
}
} // End of namespace HiResFontMap

} // End of namespace Graphics
