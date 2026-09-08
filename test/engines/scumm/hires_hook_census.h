#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/array.h"
#include "common/fs.h"
#include "common/stream.h"
#include "../../system/null_osystem.h"

/**
 * Which charset renderers reach the hi-res layer, and which do not?
 *
 * The FM-Towns bug was not a wrong line of code - it was a renderer nobody
 * had checked. Its fonts loaded, its log looked right, and every double-byte
 * glyph came from the ROM regardless. It stayed that way for weeks because
 * nothing measured which renderers are wired up.
 *
 * This is that measurement, run by `make test` rather than by whoever
 * remembers the devtools script: every class deriving from CharsetRenderer,
 * and whether its drawing path calls into _hiResText. A renderer that
 * legitimately has no hook needs a reason recorded in kExempt below, so the
 * list is a decision rather than an oversight.
 *
 * The rule being enforced: a renderer that draws double-byte text must offer
 * the character to the hi-res layer before drawing it itself.
 *
 * kExempt is the single source of truth for the exemptions.
 * devtools/scumm-hires-hook-census.py parses this table out of this file, so
 * the script and the test cannot disagree.
 */

// Renderers that draw no glyphs of their own, with the reason.
static const struct {
	const char *cls;
	const char *why;
} kExempt[] = {
	{ "CharsetRenderer",
	  "abstract base" },
	{ "CharsetRendererCommon",
	  "abstract; setCurID feeds the layer but draws nothing" },
	{ "CharsetRendererPC",
	  "abstract; drawBits1 is used through its subclasses" },
	{ "CharsetRendererV2",
	  "reaches CharsetRendererV3::printChar, but its font is compiled into "
	  "ScummVM at a fixed 8px rather than read from the game, so a "
	  "replacement is governed by the whole-multiple scale policy" },
	{ "CharsetRendererNES",
	  "NES tile font, single byte, no replacement path designed" },
	{ "CharsetRendererMac",
	  "draws every glyph twice and uses _textSurface as a stencil; "
	  "see the notes on the inverted Mac data flow" },
	{ "CharsetRendererPCE",
	  "inherits the CharsetRendererV3::printChar hook, which offers the "
	  "same character before drawBits1 runs; its own hook would be dead "
	  "code. Its 16bpp branch draws to the VirtScreen, and the layer "
	  "writes byte indices, so that path stays with the System Card font" },
	{ "CharsetRendererV7",
	  "overrides printChar with an error() stub - v7 text goes through "
	  "TextRenderer_v7 and lands in draw2byte/drawCharV7, neither hooked; "
	  "open work, and the reason FT/Dig have no hi-res text" },
	{ "CharsetRendererNut",
	  "v8/SMUSH NutRenderer; open" },
};

// The methods that put pixels on a surface. A hook anywhere else does not
// count, because the character has already been drawn by the time it runs.
static const char *const kDrawing[] = {
	"printChar", "drawChar", "drawBits1", "drawBitsN", "printCharIntern",
	"printCharInternal", "drawCharV7", "draw2byte"
};

static const char *const kHookCall = "_hiResText.drawChar";

class HiResHookCensusTestSuite : public CxxTest::TestSuite {

	// ---------------------------------------------------------------- text

	static bool isIdent(char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		       (c >= '0' && c <= '9') || c == '_';
	}

	static bool isSpace(char c) {
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	/** The identifier starting at pos, or empty. */
	static Common::String identAt(const Common::String &s, uint pos) {
		uint e = pos;
		while (e < s.size() && isIdent(s[e]))
			e++;
		return Common::String(s.c_str() + pos, e - pos);
	}

	/** The identifier ending just before pos, or empty. */
	static Common::String identBefore(const Common::String &s, uint pos) {
		uint e = pos;
		while (e > 0 && isSpace(s[e - 1]))
			e--;
		uint b = e;
		while (b > 0 && isIdent(s[b - 1]))
			b--;
		return Common::String(s.c_str() + b, e - b);
	}

	/**
	 * The index just past the '}' closing the block that opens at the first
	 * '{' at or after from. Returns s.size() when the braces do not balance,
	 * which cannot silently look like an empty body.
	 */
	static uint blockEnd(const Common::String &s, uint from, uint *bodyStart) {
		uint i = from;
		while (i < s.size() && s[i] != '{')
			i++;
		if (i >= s.size())
			return s.size();
		if (bodyStart)
			*bodyStart = i;
		int depth = 0;
		for (; i < s.size(); i++) {
			if (s[i] == '{')
				depth++;
			else if (s[i] == '}') {
				depth--;
				if (depth == 0)
					return i + 1;
			}
		}
		return s.size();
	}

	static bool contains(const Common::String &hay, const char *needle) {
		return hay.contains(needle);
	}

	// ---------------------------------------------------------------- model

	struct Renderer {
		Common::String name;
		Common::String base;
		Common::Array<Common::String> overrides;  // declared in the header
		Common::Array<Common::String> hooks;      // drawing methods that call the layer
		Common::Array<Common::String> defined;    // methods defined in charset.cpp
	};

	Common::Array<Renderer> _r;

	Renderer *find(const Common::String &name) {
		for (uint i = 0; i < _r.size(); i++)
			if (_r[i].name == name)
				return &_r[i];
		return nullptr;
	}

	static bool isDrawing(const Common::String &m) {
		for (uint i = 0; i < ARRAYSIZE(kDrawing); i++)
			if (m == kDrawing[i])
				return true;
		return false;
	}

	// ------------------------------------------------------------- reading

	/**
	 * The source file, read from the tree this runner was built from.
	 * Fails the test rather than returning empty: a census over no source
	 * would report a clean sheet.
	 */
	Common::String readSource(const char *rel) {
		Common::String path = Common::String(SCUMM_HIRES_CENSUS_SRCDIR) + "/" + rel;
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path +
			         " - the census measured nothing").c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		return out;
	}

	// --------------------------------------------------------------- parse

	/** Every `class CharsetRendererX : public Y {` in the header, in order. */
	void parseClasses(const Common::String &hdr) {
		uint i = 0;
		while (i < hdr.size()) {
			uint eol = i;
			while (eol < hdr.size() && hdr[eol] != '\n')
				eol++;
			Common::String line(hdr.c_str() + i, eol - i);

			if (line.hasPrefix("class CharsetRenderer") && line.contains('{')) {
				Renderer r;
				r.name = identAt(line, 6);

				uint colon = 0;
				while (colon < line.size() && line[colon] != ':' && line[colon] != '{')
					colon++;
				if (colon < line.size() && line[colon] == ':') {
					const char *p = strstr(line.c_str() + colon, "public ");
					if (p)
						r.base = identAt(line, (uint)(p - line.c_str()) + 7);
				}

				uint bodyStart = 0;
				uint end = blockEnd(hdr, i, &bodyStart);
				TSM_ASSERT("unbalanced braces in charset.h", end < hdr.size());
				Common::String body(hdr.c_str() + bodyStart, end - bodyStart);
				parseOverrides(body, r);
				_r.push_back(r);

				i = end;
				continue;
			}
			i = eol + 1;
		}
	}

	/**
	 * Methods the class declares with `override`. A hook in a base class does
	 * not run for a subclass that overrides the method carrying it - and
	 * CharsetRendererV7 overrides printChar inline, with a body that calls
	 * error(), so scanning charset.cpp alone would miss it.
	 */
	void parseOverrides(const Common::String &body, Renderer &r) {
		uint pos = 0;
		while (true) {
			const char *p = strstr(body.c_str() + pos, "override");
			if (!p)
				break;
			uint at = (uint)(p - body.c_str());
			pos = at + 8;
			if (at > 0 && isIdent(body[at - 1]))
				continue;
			if (at + 8 < body.size() && isIdent(body[at + 8]))
				continue;

			// ... ')' [const] override
			uint j = at;
			while (j > 0 && isSpace(body[j - 1]))
				j--;
			if (j >= 5 && Common::String(body.c_str() + j - 5, 5) == "const") {
				j -= 5;
				while (j > 0 && isSpace(body[j - 1]))
					j--;
			}
			if (j == 0 || body[j - 1] != ')')
				continue;

			// Walk back over the argument list to its '('.
			int depth = 0;
			uint k = j - 1;
			while (true) {
				if (body[k] == ')')
					depth++;
				else if (body[k] == '(') {
					depth--;
					if (depth == 0)
						break;
				}
				if (k == 0)
					break;
				k--;
			}
			if (depth != 0)
				continue;
			Common::String name = identBefore(body, k);
			if (!name.empty())
				r.overrides.push_back(name);
		}
	}

	/**
	 * Method bodies in charset.cpp, so a hook is attributed to the renderer
	 * whose method actually contains it.
	 */
	void parseDefinitions(const Common::String &cpp) {
		uint i = 0;
		while (i < cpp.size()) {
			uint eol = i;
			while (eol < cpp.size() && cpp[eol] != '\n')
				eol++;

			// Only definitions at column 0; anything indented is a nested
			// call, not a definition.
			if (eol > i && !isSpace(cpp[i])) {
				Common::String line(cpp.c_str() + i, eol - i);
				uint scan = 0;
				while (true) {
					const char *p = strstr(line.c_str() + scan, "::");
					if (!p)
						break;
					uint at = (uint)(p - line.c_str());
					scan = at + 2;
					Common::String cls = identBefore(line, at);
					if (!cls.hasPrefix("CharsetRenderer"))
						continue;
					Common::String meth = identAt(line, at + 2);
					if (meth.empty())
						continue;
					uint after = at + 2 + meth.size();
					while (after < line.size() && isSpace(line[after]))
						after++;
					if (after >= line.size() || line[after] != '(')
						continue;

					Renderer *r = find(cls);
					if (!r)
						break;
					uint bodyStart = 0;
					uint end = blockEnd(cpp, i + after, &bodyStart);
					TSM_ASSERT("unbalanced braces in charset.cpp", end < cpp.size());
					Common::String body(cpp.c_str() + bodyStart, end - bodyStart);

					r->defined.push_back(meth);
					if (isDrawing(meth) && contains(body, kHookCall))
						r->hooks.push_back(meth);

					i = end;
					goto nextLine;
				}
			}
			i = eol + 1;
			continue;
		nextLine:
			while (i < cpp.size() && cpp[i] != '\n')
				i++;
			i++;
		}
	}

	// ---------------------------------------------------------------- rules

	/** Does this class declare meth itself, in the header or in the .cpp? */
	bool declares(const Common::String &cls, const Common::String &meth) {
		Renderer *r = find(cls);
		if (!r)
			return false;
		for (uint i = 0; i < r->overrides.size(); i++)
			if (r->overrides[i] == meth)
				return true;
		for (uint i = 0; i < r->defined.size(); i++)
			if (r->defined[i] == meth)
				return true;
		return false;
	}

	/**
	 * The nearest ancestor whose hook this class actually reaches.
	 *
	 * An ancestor's hook only counts when this class does not override the
	 * method carrying it. CharsetRendererV2 and CharsetRendererV7 both define
	 * their own printChar, so the hook in their base never runs for them.
	 */
	bool inheritsHook(const Common::String &cls, Common::String &viaCls,
	                  Common::String &viaMeth) {
		Renderer *self = find(cls);
		if (!self)
			return false;
		Common::String cur = self->base;
		int guard = 0;
		while (!cur.empty() && guard++ < 32) {
			Renderer *anc = find(cur);
			if (!anc)
				return false;
			for (uint h = 0; h < anc->hooks.size(); h++) {
				const Common::String &meth = anc->hooks[h];
				// Walk from cls up to cur; an override anywhere in between
				// cuts the inheritance.
				bool overridden = false;
				Common::String walk = cls;
				int wguard = 0;
				while (walk != cur && wguard++ < 32) {
					if (declares(walk, meth)) {
						overridden = true;
						break;
					}
					Renderer *w = find(walk);
					if (!w || w->base.empty())
						break;
					walk = w->base;
				}
				if (!overridden) {
					viaCls = cur;
					viaMeth = meth;
					return true;
				}
			}
			cur = anc->base;
		}
		return false;
	}

	static const char *exemptReason(const Common::String &cls) {
		for (uint i = 0; i < ARRAYSIZE(kExempt); i++)
			if (cls == kExempt[i].cls)
				return kExempt[i].why;
		return nullptr;
	}

	bool _parsed = false;

	void census() {
		if (_parsed)
			return;
		Common::String hdr = readSource("engines/scumm/charset.h");
		Common::String cpp = readSource("engines/scumm/charset.cpp");
		TSM_ASSERT("charset.h is empty", !hdr.empty());
		TSM_ASSERT("charset.cpp is empty", !cpp.empty());
		parseClasses(hdr);
		parseDefinitions(cpp);
		_parsed = true;
	}

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	/**
	 * The parse has to find the hierarchy before any verdict it reaches means
	 * anything. A census over zero renderers reports zero problems.
	 */
	void test_the_census_actually_read_the_renderers() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		census();
		TSM_ASSERT("no CharsetRenderer classes found - the parse failed, and a "
		           "census over nothing cannot detect a missing hook",
		           _r.size() >= 10);

		// The hierarchy has to be resolved too, or every inherited hook
		// silently becomes "unaccounted".
		Renderer *v3 = find("CharsetRendererV3");
		TSM_ASSERT("CharsetRendererV3 not found", v3 != nullptr);
		TS_ASSERT_EQUALS(v3->base, Common::String("CharsetRendererPC"));

		// And at least one hook has to be visible, or the scan for the call
		// is broken and nothing would ever be reported as hooked.
		uint withHooks = 0;
		for (uint i = 0; i < _r.size(); i++)
			withHooks += _r[i].hooks.empty() ? 0 : 1;
		TSM_ASSERT("no renderer appears to call the hi-res layer - the scan "
		           "for " "_hiResText.drawChar" " is broken", withHooks > 0);
#endif
	}

	/**
	 * The rule: a renderer that draws double-byte text offers the character
	 * to the hi-res layer first. Every renderer either does that (directly or
	 * through a base class whose method it does not override), or carries a
	 * recorded reason why it needs no hook.
	 */
	void test_every_renderer_reaches_the_layer_or_says_why_not() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		census();

		Common::String unaccounted;
		for (uint i = 0; i < _r.size(); i++) {
			const Common::String &cls = _r[i].name;
			if (!_r[i].hooks.empty())
				continue;
			Common::String viaCls, viaMeth;
			if (inheritsHook(cls, viaCls, viaMeth))
				continue;
			if (exemptReason(cls))
				continue;
			if (!unaccounted.empty())
				unaccounted += ", ";
			unaccounted += cls;
		}

		if (!unaccounted.empty())
			TS_FAIL(("charset renderers with no hi-res hook and no recorded "
			         "reason: " + unaccounted +
			         " - add a hook in its drawing path, or add it to kExempt "
			         "in this file with the reason it needs none").c_str());
#endif
	}

	/**
	 * An exemption for a class that no longer exists is a reason nobody has
	 * read since the class was renamed. The devtools script never checked
	 * this, so a stale entry could sit in the list indefinitely.
	 */
	void test_no_exemption_outlives_its_renderer() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		census();
		for (uint i = 0; i < ARRAYSIZE(kExempt); i++) {
			TSM_ASSERT(kExempt[i].cls, find(Common::String(kExempt[i].cls)) != nullptr);
			TSM_ASSERT(kExempt[i].cls, strlen(kExempt[i].why) > 10);
		}
#endif
	}

	/**
	 * The hooks that are known to exist. A renderer losing its hook is caught
	 * by the rule above only while it has no exemption; naming them here means
	 * a hook cannot be quietly removed and papered over with a new exemption.
	 */
	void test_the_known_hooks_are_still_wired_up() {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
#else
		census();
		static const char *const kHooked[] = {
			"CharsetRendererClassic",       // printChar
			"CharsetRendererTownsClassic",  // drawBitsN
			"CharsetRendererV3",            // printChar
			"CharsetRendererTownsV3",       // drawBits1
		};
		for (uint i = 0; i < ARRAYSIZE(kHooked); i++) {
			Renderer *r = find(Common::String(kHooked[i]));
			TSM_ASSERT(kHooked[i], r != nullptr);
			TSM_ASSERT(kHooked[i], !r->hooks.empty());
		}

		// These two draw through a base class hook they do not override.
		static const char *const kInherits[] = {
			"CharsetRendererPCE",
			"CharsetRendererV2",
		};
		for (uint i = 0; i < ARRAYSIZE(kInherits); i++) {
			Common::String viaCls, viaMeth;
			TSM_ASSERT(kInherits[i],
			           inheritsHook(Common::String(kInherits[i]), viaCls, viaMeth));
			TSM_ASSERT(kInherits[i], viaCls == "CharsetRendererV3");
			TSM_ASSERT(kInherits[i], viaMeth == "printChar");
		}
#endif
	}
};
