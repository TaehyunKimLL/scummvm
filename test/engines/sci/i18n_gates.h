#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/system.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_props.h"
#include "sci/graphics/textlayout16.h"
#include "sci/utf8.h"

/**
 * C11 Task 5: SCI decides hi-res text by the translation, measures glyphs
 * per glyph, and breaks UTF-8 lines with the shared layout stage
 * (I18N_TEXT_DESIGN.md sections 4.1, 4.2, 4.3).
 */
class SciI18nGatesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		if (!g_system)
			Common::install_null_g_system();
	}

	void tearDown() {
		Common::uninstall_null_g_system();
	}

	// --- the gate -------------------------------------------------------

	void test_hires_text_applies() {
		Common::String why;
		TS_ASSERT(!Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kLatin1, false, why));
		TS_ASSERT_EQUALS(why, Common::String("no translation and no CJK code page"));

		why.clear();
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kLatin1, true, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kWindows949, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_01, Common::kWindows932, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_0_EARLY, Common::kWindows936, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_LATE, Common::kWindows950, false, why));

		TS_ASSERT(!Sci::hiresTextApplies(Sci::SCI_VERSION_2, Common::kWindows949, true, why));
		TS_ASSERT_EQUALS(why, Common::String("SCI32 games do not support it yet"));
	}

	// --- the advance rule -----------------------------------------------

	void test_game_advance() {
		Graphics::GlyphMetrics hangul;
		hangul.advance = 16;
		hangul.wide = true;
		// The cell rule of today: a wide glyph advances by the wide cell.
		TS_ASSERT_EQUALS(Sci::gameAdvance(hangul, 4, 8, 2), 8);

		Graphics::GlyphMetrics mark;
		mark.advance = 0;
		mark.combining = true;
		TS_ASSERT_EQUALS(Sci::gameAdvance(mark, 4, 8, 2), 0);
		// Even a mark whose face gives it an advance (Thonburi) does not move.
		mark.advance = 12;
		TS_ASSERT_EQUALS(Sci::gameAdvance(mark, 4, 8, 2), 0);

		Graphics::GlyphMetrics koKai;      // U+0E01
		koKai.advance = 14;
		TS_ASSERT_EQUALS(Sci::gameAdvance(koKai, 4, 8, 2), 7);

		Graphics::GlyphMetrics saraAm;     // U+0E33, rounds half up
		saraAm.advance = 25;
		TS_ASSERT_EQUALS(Sci::gameAdvance(saraAm, 4, 8, 2), 13);

		// A face that cannot say keeps the narrow cell.
		Graphics::GlyphMetrics unknown;
		TS_ASSERT_EQUALS(Sci::gameAdvance(unknown, 4, 8, 2), 4);
	}

	// --- a bitmap bundle behind a TrueType face ---------------------------

	/** 8x8 at 1 bpp; 'A' has its first column-byte set in every row. */
	class OneBitSource : public Graphics::UnicodeGlyphSource {
	public:
		OneBitSource() { _row[0] = 0xF0; _row[1] = 0x01; }
		byte cellWidth() const override { return 8; }
		byte cellHeight() const override { return 8; }
		byte advanceNarrow() const override { return 8; }
		byte advanceWide() const override { return 16; }
		int bitsPerPixel() const override { return 1; }
		int cells(uint32 cp) override { return cp == 'A' ? 1 : 0; }
		const byte *row(uint32 cp, int y) override { return _row; }
		uint32 glyphCount() const override { return 1; }
	private:
		byte _row[2];
	};

	void test_normalized_source_pads_a_smaller_cell_at_8bpp() {
		Common::String error;
		Graphics::NormalizedGlyphSource *n =
			Graphics::NormalizedGlyphSource::create(new OneBitSource(), 12, 10, DisposeAfterUse::YES, error);
		TS_ASSERT(n);
		if (!n)
			return;
		TS_ASSERT_EQUALS(n->cellWidth(), 12);
		TS_ASSERT_EQUALS(n->cellHeight(), 10);
		TS_ASSERT_EQUALS(n->bitsPerPixel(), 8);
		TS_ASSERT_EQUALS(n->cells('A'), 1);
		TS_ASSERT_EQUALS(n->advanceNarrow(), 8);

		const byte *r = n->row('A', 3);
		for (int x = 0; x < 24; x++) {
			// 0xF0 0x01 at 1 bpp: pixels 0-3 and 15 set; 16..23 are padding.
			const byte want = (x < 4 || x == 15) ? 255 : 0;
			TS_ASSERT_EQUALS(r[x], want);
		}
		// Rows below the source's cell are empty.
		r = n->row('A', 9);
		for (int x = 0; x < 24; x++)
			TS_ASSERT_EQUALS(r[x], 0);

		// It joins a chain of a 12x10, 8 bpp face.
		Common::Array<Graphics::UnicodeGlyphSource *> chain;
		chain.push_back(n);
		Graphics::FallbackGlyphSource fb(chain, DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(fb.cells('A'), 1);
	}

	void test_normalized_source_refuses_a_larger_cell() {
		Common::String error;
		TS_ASSERT(!Graphics::NormalizedGlyphSource::create(new OneBitSource(), 6, 10, DisposeAfterUse::YES, error));
		TS_ASSERT(!error.empty());
	}

	// SARA AM (U+0E33) is a spacing vowel (Lo) whose nikhahit ring sits over
	// the base before it, left of its own origin: like a combining mark it
	// is drawn with its origin moved right, so the ring is not cut off.
	void test_ttf_sara_am_keeps_its_ring() {
#ifdef USE_FREETYPE2
		Common::FSNode node("/Users/juami/work/scummvm/runs/c11/data/fonts/sukhumvit-text.ttf");
		if (!node.exists())
			return;
		Common::String error;
		Graphics::TtfGlyphSource *src = Graphics::TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES,
																		  16, error, false);
		TS_ASSERT(src);
		if (!src)
			return;
		Graphics::GlyphMetrics am, aa;
		TS_ASSERT(src->metrics(0x0E33, am));
		TS_ASSERT(src->metrics(0x0E32, aa));
		TS_ASSERT(am.originX > 0);
		TS_ASSERT(!am.combining);
		TS_ASSERT(am.advance > 0);
		TS_ASSERT_EQUALS(aa.originX, 0);   // SARA AA is left alone
		delete src;
#endif
	}

	// --- the decoder ----------------------------------------------------

	void test_sci_decoder_escapes() {
		// "|c1|" is a text code (SCI1.1), CR LF one newline, 가 U+AC00.
		const char *text = "|c1|\xEA\xB0\x80\r\nA";
		Graphics::TextRun run;
		const Sci::SciTextDecoder dec(true);
		run.decode((const byte *)text, strlen(text), dec);

		TS_ASSERT_EQUALS(run.size(), 4u);
		TS_ASSERT_EQUALS(run.cp(0), Graphics::kControlUnit);
		TS_ASSERT(run.flags(0) & Graphics::kUnitControl);
		TS_ASSERT(!(run.flags(0) & Graphics::kUnitNewline));
		TS_ASSERT_EQUALS(run.byteOffset(1) - run.byteOffset(0), 4u);

		TS_ASSERT_EQUALS(run.cp(1), 0xAC00u);
		TS_ASSERT_EQUALS(run.byteOffset(2) - run.byteOffset(1), 3u);

		TS_ASSERT(run.flags(2) & Graphics::kUnitNewline);
		TS_ASSERT_EQUALS(run.byteOffset(3) - run.byteOffset(2), 2u);

		TS_ASSERT_EQUALS(run.cp(3), (uint32)'A');
		TS_ASSERT_EQUALS(run.byteOffset(4), 10u);
	}

	void test_sci_decoder_before_sci11_and_other_line_ends() {
		// Before SCI1.1 '|' is an ordinary character; a lone CR, a lone LF
		// and U+FF20 (SQ4 Japanese) end a line.
		const char *text = "|\rx\ny\xEF\xBC\xA0z";
		Graphics::TextRun run;
		const Sci::SciTextDecoder dec(false);
		run.decode((const byte *)text, strlen(text), dec);

		TS_ASSERT_EQUALS(run.size(), 7u);
		TS_ASSERT_EQUALS(run.cp(0), (uint32)'|');
		TS_ASSERT(!(run.flags(0) & Graphics::kUnitControl));
		TS_ASSERT(run.flags(1) & Graphics::kUnitNewline);
		TS_ASSERT(run.flags(3) & Graphics::kUnitNewline);
		TS_ASSERT(run.flags(5) & Graphics::kUnitNewline);
		TS_ASSERT_EQUALS(run.byteOffset(6) - run.byteOffset(5), 3u);

		// An unterminated code runs to the end of the text, as
		// GfxText16::CodeProcessing() reads it.
		const char *open = "a|f1";
		const Sci::SciTextDecoder dec11(true);
		run.decode((const byte *)open, strlen(open), dec11);
		TS_ASSERT_EQUALS(run.size(), 2u);
		TS_ASSERT(run.flags(1) & Graphics::kUnitControl);
		TS_ASSERT_EQUALS(run.byteOffset(2) - run.byteOffset(1), 3u);
	}

	// --- GetLongest() on the layout stage --------------------------------

	/** Widths in game px: a fixed 4/8 rule, or a proportional ASCII table. */
	class TestMetrics : public Sci::SciLayoutMetrics {
	public:
		explicit TestMetrics(bool proportional) : _proportional(proportional) {}
		int charWidth(uint32 cp) override { return width(cp, _proportional); }

		static int width(uint32 cp, bool proportional) {
			if (Graphics::Unicode::isCombining(cp))
				return 0;
			if (Graphics::Unicode::isWide(cp))
				return 8;
			if (cp < 0x80 && proportional)
				return cp == ' ' ? 3 : 3 + (int)(cp % 3);
			return 4;
		}

	private:
		bool _proportional;
	};

	/**
	 * The byte-walking GetLongest() of GfxText16 before this task, for UTF-8
	 * text below SCI1.1 (no '|' codes, no PQ2 escape): the reference the
	 * layout stage must reproduce on the Korean data.
	 */
	static int16 oldGetLongest(const char *&textPtr, int16 maxWidth, bool early, bool proportional) {
		uint32 curChar = 0;
		int16 lastSpaceCharCount = 0;
		const char *lastSpacePtr = nullptr;
		int16 curCharCount = 0, resultCharCount = 0;
		uint16 tempWidth = 0;
		int curCharBytes = 0;

		for (;;) {
			curChar = Sci::decodeUtf8Char((const byte *)textPtr, curCharBytes);
			switch (curChar) {
			case 0xD:
				if ((*(const byte *)(textPtr + 1)) == 0xA) {
					curCharCount++; textPtr++;
				}
				// fall through
			case 0xA:
			case 0xFF20:
				curCharCount += curCharBytes; textPtr += curCharBytes;
				// fall through
			case 0:
				return curCharCount;
			case ' ':
				lastSpaceCharCount = curCharCount;
				lastSpacePtr = textPtr + 1;
				break;
			default:
				break;
			}
			tempWidth += TestMetrics::width(curChar, proportional);
			if (tempWidth > maxWidth)
				break;
			if (early && lastSpaceCharCount == 0 && tempWidth == maxWidth)
				break;
			curCharCount += curCharBytes; textPtr += curCharBytes;
		}

		if (lastSpaceCharCount) {
			resultCharCount = lastSpaceCharCount;
			textPtr = lastSpacePtr;
			while (*textPtr == ' ')
				textPtr++;
		} else {
			if (early) {
				curCharCount += curCharBytes; textPtr += curCharBytes;
			}
			resultCharCount = curCharCount;
		}
		return resultCharCount;
	}

	/** gamedata/kq1-ko1, where the harness keeps it; empty when absent. */
	static Common::FSNode kq1koDir() {
		static const char *const candidates[] = {
			"/Users/juami/work/scummvm/gamedata/kq1-ko1",
			"../../../gamedata/kq1-ko1",
			"../gamedata/kq1-ko1",
			"gamedata/kq1-ko1"
		};
		for (uint i = 0; i < ARRAYSIZE(candidates); i++) {
			Common::FSNode n(Common::Path(candidates[i], '/'));
			if (n.exists() && n.isDirectory())
				return n;
		}
		return Common::FSNode();
	}

	void test_fitline_matches_getlongest_on_kq1ko() {
		const Common::FSNode dir = kq1koDir();
		if (!dir.exists())
			return;   // no game data here: nothing to compare

		Common::FSList files;
		dir.getChildren(files, Common::FSNode::kListFilesOnly);
		const Graphics::BreakRules rules;   // hangul=word, kinsoku on, Thai on
		Sci::SciLayoutText lt;
		uint nStrings = 0, lines = 0, differences = 0;
		Common::String report;

		for (uint f = 0; f < files.size(); f++) {
			const Common::String name = files[f].getName();
			if (!name.hasPrefixIgnoreCase("text."))
				continue;
			Common::SeekableReadStream *in = files[f].createReadStream();
			if (!in)
				continue;
			Common::Array<char> data(in->size() + 1);
			in->read(data.begin(), in->size());
			data[in->size()] = 0;
			const uint32 size = in->size();
			delete in;

			// An SCI0 patch: 2 header bytes, then NUL-separated strings.
			for (uint32 pos = 2; pos < size; ) {
				const char *str = data.begin() + pos;
				const uint32 len = strlen(str);
				pos += len + 1;
				if (!len)
					continue;
				nStrings++;
				for (int w = 0; w < 3; w++) {
					static const int16 widths[] = { 120, 160, 200 };
					for (int mode = 0; mode < 4; mode++) {
						const bool early = mode & 1;
						const bool proportional = mode & 2;
						TestMetrics m(proportional);
						const char *oldPtr = str;
						while (*oldPtr) {
							const char *lineStart = oldPtr;
							const int16 oldCount = oldGetLongest(oldPtr, widths[w], early, proportional);
							uint32 next = 0;
							const int16 newCount = Sci::getLongestLayout((const byte *)lineStart, widths[w], false,
																		 early, m, rules, lt, next);
							lines++;
							if (oldCount != newCount || (uint32)(oldPtr - lineStart) != next) {
								if (differences < 20)
									report += Common::String::format("\n%s @%u w%d early%d prop%d: old %d/%d new %d/%u: \"%s\"",
										name.c_str(), (uint)(lineStart - str), widths[w], early, proportional,
										oldCount, (int)(oldPtr - lineStart), newCount, next,
										Common::String(lineStart, MIN<uint32>(60, strlen(lineStart))).c_str());
								differences++;
								oldPtr = lineStart + next;   // follow the new layout
							}
							if (!oldCount)
								break;
						}
					}
				}
			}
		}

		TS_ASSERT(nStrings > 1000);
		TS_ASSERT(lines > 40000);
		TSM_ASSERT_EQUALS(Common::String::format("%u of %u lines differ:%s", differences, lines, report.c_str()).c_str(),
						  differences, 0u);
	}

	/** Every width from 1 to 60 on strings built for the old code's corners. */
	void test_getlongest_layout_matches_old_on_edge_cases() {
		static const char *const texts[] = {
			"abcd efg",                  // a first word that fits exactly (early)
			"aa  bb   cc",               // several spaces: the count keeps all but the last
			"hello world  ",             // trailing spaces that overflow at the end
			" leading space here",       // a space at the start is no break
			"  two leading",
			"word\r\nnext line",         // CR LF
			"one\ntwo\rthree",           // LF, CR
			"abcdefghijklmnopqrstuvwxyz", // no space: split
			"x \xEA\xB0\x80\xEA\xB0\x81\xEA\xB0\x82 y \xEA\xB0\x83",   // Hangul words
			"\xEA\xB0\x80\xEA\xB0\x81\xEA\xB0\x82\xEA\xB0\x83\xEA\xB0\x84\xEA\xB0\x85",
			"a b",
			"ab ",
			" ",
			"a  "
		};
		const Graphics::BreakRules rules;
		Sci::SciLayoutText lt;
		uint differences = 0, lines = 0;
		Common::String report;
		for (uint t = 0; t < ARRAYSIZE(texts); t++) {
			for (int16 w = 1; w <= 60; w++) {
				for (int mode = 0; mode < 4; mode++) {
					const bool early = mode & 1;
					const bool proportional = mode & 2;
					TestMetrics m(proportional);
					const char *oldPtr = texts[t];
					while (*oldPtr) {
						const char *lineStart = oldPtr;
						const int16 oldCount = oldGetLongest(oldPtr, w, early, proportional);
						uint32 next = 0;
						const int16 newCount = Sci::getLongestLayout((const byte *)lineStart, w, false, early,
																	 m, rules, lt, next);
						lines++;
						if (oldCount != newCount || (uint32)(oldPtr - lineStart) != next) {
							if (differences < 20)
								report += Common::String::format("\n\"%s\" @%u w%d early%d prop%d: old %d/%d new %d/%u",
									texts[t], (uint)(lineStart - texts[t]), w, early, proportional,
									oldCount, (int)(oldPtr - lineStart), newCount, next);
							differences++;
							oldPtr = lineStart + next;
						}
						if (!oldCount)
							break;
					}
				}
			}
		}
		TSM_ASSERT_EQUALS(Common::String::format("%u of %u lines differ:%s", differences, lines, report.c_str()).c_str(),
						  differences, 0u);
	}

	void test_getlongest_layout_japanese_kinsoku_and_thai() {
		TestMetrics m(false);
		const Graphics::BreakRules rules;
		Sci::SciLayoutText lt;
		uint32 next = 0;

		// 8 px per kanji, 40 px: 5 fit. "あいうえ。お" must not start a line
		// with the full stop: the break moves before え.
		const char *ja = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A\xE3\x80\x82\xE3\x81\x8B";
		// あいうえお。か - 7 units of 8 px, width 40 fits あいうえお, 。 cannot start
		int16 n = Sci::getLongestLayout((const byte *)ja, 40, false, true, m, rules, lt, next);
		TS_ASSERT_EQUALS(n, 12);          // あいうえ, 4 x 3 bytes
		TS_ASSERT_EQUALS(next, 12u);

		// Thai: "ที่นี่" (U+0E17 U+0E35 U+0E48 U+0E19 U+0E35 U+0E48), 4 px
		// per base, marks 0: a line of 4 px holds one base and its marks.
		const char *th = "\xE0\xB8\x97\xE0\xB8\xB5\xE0\xB9\x88\xE0\xB8\x99\xE0\xB8\xB5\xE0\xB9\x88";
		n = Sci::getLongestLayout((const byte *)th, 4, false, false, m, rules, lt, next);
		TS_ASSERT_EQUALS(n, 9);           // ที่: the marks stay with their base
		TS_ASSERT_EQUALS(next, 9u);
	}
};
