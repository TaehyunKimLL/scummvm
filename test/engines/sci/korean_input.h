#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/ustr.h"
#include "../../system/null_osystem.h"

#include "engines/sci/graphics/koreaninput.h"

/**
 * Korean text entry: keys in, EUC-KR bytes into the game's own heap string.
 *
 * THE CLOSING MEASUREMENT of card S4 lives here, in
 * test_composed_bytes_equal_literal_bytes. A player typing `ans dufdj` and a
 * literal EUC-KR 문 열어 must put the SAME BYTES in the buffer, because those
 * bytes are what the game's script reads back and what GfxFontKorean draws.
 * It needs no GUI, which is why this card is testable at all where K4 was not.
 *
 * The other property that keeps the design honest is the range check. S3b
 * measured that kWindows949 is UHC, not plain Wansung: it encodes all 11172
 * modern syllables while the bundled korean.fnt indexes 2350 of them. A
 * composer that trusted the converter would emit 8822 syllables that encode
 * cleanly and then index outside the glyph table. So the composer checks
 * against the FONT's rectangle, and that is asserted over the whole syllable
 * block below rather than on a few examples.
 */

class SciKoreanInputTestSuite : public CxxTest::TestSuite {

	/**
	 * Type a key string through the composer into a buffer, as the edit
	 * control does. '~' backspaces, '^' toggles Korean off and on again.
	 */
	Common::String type(const char *keys, uint maxChars = 40) {
		Sci::KoreanComposer comp;
		comp.setEnabled(true);
		Common::String text;
		uint runStart = 0;

		for (const char *p = keys; *p; ++p) {
			if (*p == '~') {
				if (!comp.backspace(text, runStart)) {
					if (!text.empty())
						text.deleteLastChar();
				}
				continue;
			}
			if (*p == '^') {
				comp.setEnabled(!comp.isEnabled());
				runStart = text.size();
				continue;
			}
			Common::String before = text;
			if (comp.feed(*p, text, runStart)) {
				if (text.size() > maxChars) {
					text = before;
					comp.reset();
					runStart = text.size();
				}
				continue;
			}
			// Refused, or Korean off: the key is an ordinary ASCII insert.
			text += *p;
			runStart = text.size();
			// controls16.cpp - an ordinary insert leaves the composer
			// owning nothing, so the run re-anchors past what was typed.
			if (!comp.ownsRun())
				runStart = text.size();
		}
		return text;
	}

	Common::String hex(const Common::String &s) {
		Common::String out;
		for (uint i = 0; i < s.size(); ++i)
			out += Common::String::format("%s%02X", i ? " " : "", (byte)s[i]);
		return out;
	}

	/** EUC-KR 문 열어, spelled as bytes so the test states its own ground truth. */
	static Common::String literalMunYeoleo() {
		const char bytes[] = { (char)0xB9, (char)0xAE, ' ',
							   (char)0xBF, (char)0xAD, (char)0xBE, (char)0xEE, 0 };
		return Common::String(bytes);
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

	// ============================================== the card's close condition

	/**
	 * A composed run and a literal run produce IDENTICAL BYTES.
	 *
	 * The literal side is written out byte by byte rather than produced by
	 * the same converter the composer uses: running both sides through one
	 * encoder would compare it with itself and pass even if the encoding were
	 * wrong. These are the EUC-KR bytes of 문 열어, and they are also what a
	 * translated resource file would hold.
	 */
	void test_composed_bytes_equal_literal_bytes() {
		Common::String composed = type("ans dufdj");
		Common::String literal = literalMunYeoleo();

		TS_ASSERT_EQUALS(hex(composed), hex(literal));
		TS_ASSERT_EQUALS(composed.size(), 7u);
		TS_ASSERT_EQUALS(hex(composed), Common::String("B9 AE 20 BF AD BE EE"));
	}

	/** Same, for 한글: two syllables, no space, no ASCII. */
	void test_composed_hangul_equals_literal_hangul() {
		const char bytes[] = { (char)0xC7, (char)0xD1, (char)0xB1, (char)0xDB, 0 };
		TS_ASSERT_EQUALS(hex(type("gksrmf")), hex(Common::String(bytes)));
	}

	/**
	 * Every syllable the keyboard can reach and the font can draw round-trips
	 * to the same bytes, not just the two phrases above.
	 *
	 * Two examples would pass on an encoder that special-cases them. This
	 * sweeps the whole 25x94 rectangle the font indexes: encode each syllable,
	 * decode the pair back, and require the composer's own converter to agree
	 * with the font's indexing arithmetic.
	 */
	void test_every_drawable_syllable_encodes_into_the_fonts_rectangle() {
		int drawable = 0;
		for (uint32 cp = 0xAC00; cp <= 0xD7A3; ++cp) {
			Common::String enc = Sci::KoreanComposer::encode(cp);
			if (enc.empty()) {
				TS_ASSERT(!Sci::KoreanComposer::isDrawable(cp));
				continue;
			}
			drawable++;
			TS_ASSERT_EQUALS(enc.size(), 2u);
			const byte lead = (byte)enc[0];
			const byte trail = (byte)enc[1];
			// Inside the rows FontKoreanWansung::getCharData() can index.
			TS_ASSERT_LESS_THAN_EQUALS(0xB0, lead);
			TS_ASSERT_LESS_THAN_EQUALS(lead, 0xC8);
			TS_ASSERT_LESS_THAN_EQUALS(0xA1, trail);
			TS_ASSERT_LESS_THAN_EQUALS(trail, 0xFE);
			const int idx = (lead - 0xB0) * 94 + (trail - 0xA1);
			TS_ASSERT_LESS_THAN(idx, 2350);
		}
		// S3b measured this population: 2350 of the 11172 modern syllables.
		TS_ASSERT_EQUALS(drawable, 2350);
	}

	/**
	 * The 8822 encodable-but-glyphless syllables are refused.
	 *
	 * This is the one S3b flagged as correcting the natural reading of the
	 * S1 assessment: encodability and font coverage are NOT the same
	 * boundary, and the converter will not say no.
	 */
	void test_encodable_but_undrawable_syllables_are_refused() {
		int refused = 0;
		for (uint32 cp = 0xAC00; cp <= 0xD7A3; ++cp) {
			if (!Sci::KoreanComposer::isDrawable(cp)) {
				TS_ASSERT(Sci::KoreanComposer::encode(cp).empty());
				refused++;
			}
		}
		TS_ASSERT_EQUALS(refused, 11172 - 2350);
	}

	/**
	 * Compatibility jamo encode and are STILL refused.
	 *
	 * A half-composed syllable is shown as U+3131..U+3163. All 51 encode -
	 * into the 0xA4 row - and none has a glyph, because 0xA4 is outside
	 * 0xB0..0xC8. If the check were "does it encode", a player would see
	 * garbage for every half-typed syllable.
	 */
	void test_compatibility_jamo_encode_but_are_not_drawable() {
		int encodable = 0;
		for (uint32 cp = 0x3131; cp <= 0x3163; ++cp) {
			TS_ASSERT(!Sci::KoreanComposer::isDrawable(cp));
			TS_ASSERT(Sci::KoreanComposer::encode(cp).empty());

			// It DOES encode - the refusal is the font check, not a failure
			// to convert. Shown here so the reason is visible.
			Common::U32String one;
			one += cp;
			if (Common::convertFromU32String(one, Common::kWindows949).size() == 2)
				encodable++;
		}
		TS_ASSERT_EQUALS(encodable, 51);
	}

	/** ASCII passes through the encoder as one byte. */
	void test_ascii_encodes_as_one_byte() {
		for (uint32 cp = 0x01; cp < 0x80; ++cp) {
			TS_ASSERT(Sci::KoreanComposer::isDrawable(cp));
			Common::String enc = Sci::KoreanComposer::encode(cp);
			TS_ASSERT_EQUALS(enc.size(), 1u);
			TS_ASSERT_EQUALS((byte)enc[0], (byte)cp);
		}
	}

	// ============================================== backspace, the three cases

	/**
	 * The card asks for backspace behaviour stated and demonstrated for three
	 * cases. Stated:
	 *
	 *   mid-syllable (cho+jung)   the jungseong goes, the choseong remains and
	 *                             is shown as a lone compatibility jamo - which
	 *                             has NO GLYPH in this font, so the buffer is
	 *                             left holding the choseong with nothing drawn.
	 *                             See test_lone_jamo_is_held_but_not_drawable.
	 *   complete syllable + jong  the jongseong goes, cho+jung remain composed
	 *                             as a drawable syllable.
	 *   an ASCII character        the composer does not own it, so one byte is
	 *                             deleted, exactly as before this card.
	 */
	void test_backspace_on_a_syllable_with_a_final_leaves_the_syllable() {
		// rkq~ : ㄱㅏㅂ -> 갑, backspace -> 가 (EUC-KR B0 A1)
		const char ga[] = { (char)0xB0, (char)0xA1, 0 };
		TS_ASSERT_EQUALS(hex(type("rkq~")), hex(Common::String(ga)));
	}

	void test_backspace_decomposes_a_compound_final() {
		// dkfr~ : ㅇㅏㄹㄱ -> 앍, backspace -> 알 (EUC-KR BE CB)
		const char al[] = { (char)0xBE, (char)0xCB, 0 };
		TS_ASSERT_EQUALS(hex(type("dkfr~")), hex(Common::String(al)));
	}

	void test_backspace_mid_syllable_removes_the_vowel_not_the_syllable() {
		// rk~ : ㄱㅏ -> 가, backspace -> the lone ㄱ, which has no glyph, so
		// the composer holds it and the buffer shows nothing for it.
		Common::String got = type("rk~");
		TS_ASSERT_EQUALS(got.size(), 0u);

		// One more backspace and the composer is empty, not stuck.
		TS_ASSERT_EQUALS(type("rk~~").size(), 0u);
	}

	void test_backspace_on_ascii_deletes_one_byte() {
		TS_ASSERT_EQUALS(type("^abc~^"), Common::String("ab"));
		TS_ASSERT_EQUALS(type("^abc~~~^"), Common::String(""));
	}

	/** Backspacing through a composed run empties it and does not underflow. */
	void test_backspace_through_a_whole_run_terminates() {
		for (int n = 1; n <= 12; ++n) {
			Common::String keys = "gksrmf";
			for (int i = 0; i < n; ++i)
				keys += '~';
			Common::String got = type(keys.c_str());
			TS_ASSERT_LESS_THAN_EQUALS(got.size(), 4u);
		}
		TS_ASSERT_EQUALS(type("gksrmf~~~~~~~~~~~~").size(), 0u);
	}

	/**
	 * A lone jamo is held by the composer and kept OUT of the buffer.
	 *
	 * This is the honest statement of a limitation rather than a hidden one:
	 * the composing state cannot be drawn with the bundled font, so rather
	 * than write bytes that would draw as garbage, the composer keeps them.
	 * Typing the vowel that completes the syllable makes it appear.
	 */
	void test_lone_jamo_is_held_but_not_drawable() {
		TS_ASSERT(!Sci::KoreanComposer::isDrawable(0x3131));   // ㄱ
		TS_ASSERT_EQUALS(type("r").size(), 0u);                 // nothing yet

		const char ga[] = { (char)0xB0, (char)0xA1, 0 };
		TS_ASSERT_EQUALS(hex(type("rk")), hex(Common::String(ga)));  // 가 appears
	}

	// ==================================================== mixed, and the toggle

	/**
	 * Mixed ASCII and Korean keeps both, in order, with the right byte
	 * counts - which is what the cursor and width loops then walk.
	 */
	void test_mixed_ascii_and_korean() {
		// Korean off for "ab", on for 한, off for "!"
		Common::String got = type("^ab^gks^!^");
		const char expect[] = { 'a', 'b', (char)0xC7, (char)0xD1, '!', 0 };
		TS_ASSERT_EQUALS(hex(got), hex(Common::String(expect)));
		TS_ASSERT_EQUALS(got.size(), 5u);
	}

	/**
	 * Switching Korean off commits the syllable in progress rather than
	 * abandoning it.
	 *
	 * The bytes are already in the caller's string; dropping the composer's
	 * state without committing would leave them owned by nobody, and the next
	 * backspace would try to decompose a syllable that is no longer being
	 * composed.
	 */
	void test_toggling_off_commits_the_pending_syllable() {
		Sci::KoreanComposer comp;
		comp.setEnabled(true);
		Common::String text;
		uint runStart = 0;

		comp.feed('g', text, runStart);
		comp.feed('k', text, runStart);          // 하
		comp.feed('s', text, runStart);          // 한
		TS_ASSERT(comp.ownsRun());

		comp.setEnabled(false);
		TS_ASSERT(!comp.ownsRun());
		TS_ASSERT(!comp.isComposing());

		const char han[] = { (char)0xC7, (char)0xD1, 0 };
		TS_ASSERT_EQUALS(hex(text), hex(Common::String(han)));

		// A backspace now is the ordinary byte-wise one; the composer refuses.
		TS_ASSERT(!comp.backspace(text, runStart));
	}

	/**
	 * With the composer off, feed() consumes nothing at all.
	 *
	 * This is the English-unchanged claim at the unit level: the edit control
	 * only reaches these branches when isEnabled(), and isEnabled() is false
	 * until the Han/Yeong key arrives - which EventManager only synthesizes
	 * when sci_hangul_input is set.
	 */
	void test_a_disabled_composer_consumes_nothing() {
		Sci::KoreanComposer comp;
		TS_ASSERT(!comp.isEnabled());

		Common::String text("open door");
		uint runStart = text.size();

		for (int i = 0; i < 256; ++i) {
			TS_ASSERT(!comp.feed((char)i, text, runStart));
			TS_ASSERT(!comp.backspace(text, runStart));
		}
		// Not one byte moved.
		TS_ASSERT_EQUALS(text, Common::String("open door"));
		TS_ASSERT_EQUALS(runStart, 9u);
		TS_ASSERT(!comp.ownsRun());
	}

	/**
	 * A key the composer refuses leaves the buffer exactly as it was.
	 *
	 * The caller then inserts it as ASCII. What must not happen is a partial
	 * write: bytes in the string that the composer no longer accounts for.
	 */
	void test_a_refused_key_changes_nothing() {
		Sci::KoreanComposer comp;
		comp.setEnabled(true);
		Common::String text;
		uint runStart = 0;

		// Digits and punctuation are not jamo keys; the composer flushes and
		// appends them, which is a change - so check a key it truly refuses
		// by driving it into an undrawable state is not reachable from the
		// keyboard. Instead assert the invariant directly over every byte:
		// after feed() returns false, the STRING is untouched.
		//
		// runStart is deliberately NOT part of this. A non-jamo key that
		// ends an undrawable composition returns false after re-anchoring
		// the run past what is already in the string, so that the ASCII the
		// caller is about to insert is not swallowed by the next syllable.
		// The bytes are what must not move; the anchor is allowed to.
		for (int i = 0; i < 256; ++i) {
			Common::String before = text;
			if (!comp.feed((char)i, text, runStart)) {
				TS_ASSERT_EQUALS(text, before);
				// Whatever it became, it must still address this string.
				TS_ASSERT_LESS_THAN_EQUALS(runStart, text.size());
			}
		}
	}

	// ========================================================= the wiring gate

	/**
	 * The Han/Yeong key is synthesized only when asked for, and consumed
	 * before a script can see it.
	 *
	 * Asserted against the source rather than argued, because the whole
	 * no-change-for-existing-games claim rests on it and nothing else in this
	 * file can reach EventManager.
	 */
	void test_the_toggle_key_is_gated_on_the_config_key() {
		Common::String ev = readSource("engines/sci/event.cpp");

		// The Han/Yeong state is owned by the event manager, which flips it
		// and emits no event at all - see the comment there. Mirroring it
		// into the composer here rather than reacting to a key means no
		// script can swallow the toggle, and a key that is never synthesized
		// cannot be caught between the two.
		TS_ASSERT(ev.contains("_hangulInputEnabled = !_hangulInputEnabled;"));
		TS_ASSERT(ev.contains("return noEvent;"));
		// ...and the gate is exactly one config key, read once.
		TS_ASSERT(ev.contains("ConfMan.hasKey(\"sci_hangul_input\")"));

		Common::String ct = readSource("engines/sci/graphics/controls16.cpp");
		TS_ASSERT(ct.contains("hangulInputEnabled() != _koreanInput.isEnabled()"));

		// Every Korean branch in the control is behind isEnabled(): the
		// toggle mirror, the backspace, and the printable-key path.
		int koreanBranches = 0;
		const char *needle = "_koreanInput.isEnabled()";
		for (uint i = 0; i + strlen(needle) <= ct.size(); ++i) {
			if (!strncmp(ct.c_str() + i, needle, strlen(needle)))
				koreanBranches++;
		}
		// Every Korean branch in the control is behind isEnabled(): the
		// toggle mirror reads it twice (compare, then flip), the backspace
		// and printable-key paths once each, and the Hangul debug logging
		// reports it twice more. The run release before the editing
		// switch is gated on ownsRun() instead, which implies isEnabled() -
		// a disabled composer owns nothing.
		TS_ASSERT_EQUALS(koreanBranches, 6);

		// The run must be released before the ordinary editing switch, or
		// an abandoned syllable comes back on the next keypress. See
		// test_the_run_is_released_once_before_the_editing_switch.
		TS_ASSERT(ct.contains("_koreanInput.commit(text, _koreanRunStart)"));

		// The staleness guard compares the BYTES the control last left, not
		// their length. A prompt's run opens at offset 0 of an empty line,
		// so an offset test is still satisfied after a script rewrites the
		// line and cannot see that the run has gone stale.
		TS_ASSERT(ct.contains("_koreanRunText != text"));
	}

	// ============================== the run must not survive a new session

	/**
	 * Typing the same phrase at a prompt twice produces it twice, not
	 * doubled once.
	 *
	 * This is the S11 defect, and it is the reason the staleness guard
	 * compares bytes rather than a length. The composer's run opens at
	 * offset 0 of an empty prompt line. After Enter the script consumes the
	 * line and clears the buffer, so the offset 0 is STILL <= the new size
	 * and an offset-based guard sees nothing wrong. The composer, never
	 * told its line ended, then re-emits everything it still holds over the
	 * fresh line: 한글 typed twice came back 한글한글, and the buffer grew
	 * without bound across further visits.
	 *
	 * Modelled here the way the control does it: commit() on Enter, and a
	 * byte comparison against what the control last left in the string.
	 */
	void test_a_second_prompt_session_does_not_repeat_the_first() {
		Sci::KoreanComposer comp;
		comp.setEnabled(true);
		Common::String text;
		uint runStart = 0;
		Common::String runText;

		// --- session 1: type 한글 at an empty prompt
		for (const char *p = "gksrmf"; *p; ++p) {
			comp.feed(*p, text, runStart);
			runText = text;
		}
		const char han[] = { (char)0xC7, (char)0xD1, (char)0xB1, (char)0xDB, 0 };
		TS_ASSERT_EQUALS(hex(text), hex(Common::String(han)));

		// The run stays anchored at offset 0 for the whole line - feed()
		// rewrites from runStart every time. That is what makes the offset
		// test useless here, so pin it.
		TS_ASSERT_EQUALS(runStart, 0u);

		// --- Enter without the commit (the pre-fix behaviour), then the
		//     script clears the line. The offset guard cannot see this:
		//     runStart is 0 and the new size is 0.
		Common::String cleared;
		TS_ASSERT(!(runStart > cleared.size()));   // guard would sleep
		TS_ASSERT(runText != cleared);             // the byte test fires

		// --- Enter WITH the commit, which is the other half of the fix:
		//     it releases the run so the composer stops holding the line.
		comp.commit(text, runStart);
		runText = text;
		TS_ASSERT(!comp.ownsRun());
		TS_ASSERT_EQUALS(runStart, 4u);

		text.clear();

		// --- session 2: the same control, the same (now empty) string.
		if (runText != text) {
			comp.reset();
			runStart = text.size();
			runText = text;
		}

		for (const char *p = "gksrmf"; *p; ++p) {
			comp.feed(*p, text, runStart);
			runText = text;
		}

		// 한글, once - not 한글한글.
		TS_ASSERT_EQUALS(hex(text), hex(Common::String(han)));
		TS_ASSERT_EQUALS(text.size(), 4u);
	}

	/**
	 * A script rewriting the line is not overwritten by a stale run.
	 *
	 * The harsher half of the same defect: when the script leaves text
	 * behind instead of clearing, the old run's offset still points into a
	 * valid range, and the composer's next key rewrote the script's text
	 * away entirely.
	 */
	void test_a_stale_run_does_not_overwrite_script_text() {
		Sci::KoreanComposer comp;
		comp.setEnabled(true);
		Common::String text;
		uint runStart = 0;
		Common::String runText;

		for (const char *p = "gksrmf"; *p; ++p) {
			comp.feed(*p, text, runStart);
			runText = text;
		}

		// The script replaces the line with its own text.
		text = Common::String("look at door");

		// Offset guard: asleep. Byte guard: fires.
		TS_ASSERT(!(runStart > text.size()));
		TS_ASSERT(runText != text);
		if (runText != text) {
			comp.reset();
			runStart = text.size();
			runText = text;
		}

		comp.feed('r', text, runStart);
		comp.feed('k', text, runStart);

		// The script's text is still there, with the new syllable after it.
		TS_ASSERT(text.hasPrefix("look at door"));
		const char ga[] = { (char)0xB0, (char)0xA1, 0 };
		TS_ASSERT_EQUALS(hex(text),
		                 hex(Common::String("look at door") + Common::String(ga)));
	}

	/**
	 * Every exit from a composing syllable releases the run.
	 *
	 * The prompt case (Enter) was the reported symptom, but it is not the
	 * only way a half-typed syllable ends. Measured with the probe against
	 * the real composer, two more leaked before the control grew a single
	 * release point:
	 *
	 *   Delete on a composing 한 -> the byte-wise delete took one byte out
	 *     of the string, the composer still held 한, and the next jamo
	 *     re-emitted it: expected D1 B0A1, got C7D1 B0A1.
	 *   Ctrl+C on a composing 한 -> the line was cleared and the next jamo
	 *     brought the whole syllable back: expected B0A1, got C7D1 B0A1.
	 *
	 * The cause is shared and so is the fix: reaching the ordinary editing
	 * switch means the key is about to be handled by byte-wise edits and
	 * caret moves that know nothing about the composer, so ownership ends
	 * there - once, for every such key, rather than per case.
	 */
	void test_every_exit_from_a_composing_syllable_releases_the_run() {
		const char han[] = { (char)0xC7, (char)0xD1, 0 };
		const char ga[]  = { (char)0xB0, (char)0xA1, 0 };

		// --- Delete: the remaining bytes stay, the syllable does not
		//     come back.
		{
			Sci::KoreanComposer comp;
			comp.setEnabled(true);
			Common::String text;
			uint runStart = 0;
			for (const char *p = "gks"; *p; ++p)
				comp.feed(*p, text, runStart);
			TS_ASSERT_EQUALS(hex(text), hex(Common::String(han)));
			TS_ASSERT(comp.ownsRun());

			comp.commit(text, runStart);       // the control's release point
			TS_ASSERT(!comp.ownsRun());
			text.deleteChar(0);                // Delete's byte-wise edit
			runStart = text.size();

			comp.feed('r', text, runStart);
			comp.feed('k', text, runStart);
			const char want[] = { (char)0xD1, (char)0xB0, (char)0xA1, 0 };
			TS_ASSERT_EQUALS(hex(text), hex(Common::String(want)));
		}

		// --- Ctrl+C: the cleared line stays cleared.
		{
			Sci::KoreanComposer comp;
			comp.setEnabled(true);
			Common::String text;
			uint runStart = 0;
			for (const char *p = "gks"; *p; ++p)
				comp.feed(*p, text, runStart);

			comp.commit(text, runStart);
			text.clear();
			runStart = 0;

			comp.feed('r', text, runStart);
			comp.feed('k', text, runStart);
			TS_ASSERT_EQUALS(hex(text), hex(Common::String(ga)));
			TS_ASSERT_EQUALS(text.size(), 2u);
		}

		// --- A caret move keeps the bytes but still ends the run, so the
		//     next syllable appends instead of rewriting.
		{
			Sci::KoreanComposer comp;
			comp.setEnabled(true);
			Common::String text;
			uint runStart = 0;
			for (const char *p = "gks"; *p; ++p)
				comp.feed(*p, text, runStart);

			comp.commit(text, runStart);
			runStart = text.size();

			comp.feed('r', text, runStart);
			comp.feed('k', text, runStart);
			TS_ASSERT_EQUALS(hex(text),
			                 hex(Common::String(han) + Common::String(ga)));
		}
	}

	/**
	 * The release point is reached by EVERY key that falls through to the
	 * ordinary editing switch, not just the ones named above.
	 *
	 * Asserted against the source because the property is structural: the
	 * commit sits before the switch, so a case added later inherits it.
	 * A per-key list would not, which is how Delete and Ctrl+C were missed
	 * when Enter alone was handled.
	 */
	void test_the_run_is_released_once_before_the_editing_switch() {
		Common::String ct = readSource("engines/sci/graphics/controls16.cpp");

		const int commits = countOccurrences(ct, "_koreanInput.commit(");
		TS_ASSERT_EQUALS(commits, 1);

		// ...and it is guarded by ownsRun(), so a line with no composer run
		// is untouched - which is every non-Korean game.
		TS_ASSERT(ct.contains("if (_koreanInput.ownsRun()) {"));

		// The commit must come BEFORE the editing switch, or the byte-wise
		// edits run first and the composer rewrites over them.
		const char *needle = "_koreanInput.commit(";
		const char *sw = "			switch (eventKey) {";
		TS_ASSERT(ct.contains(sw));
		TS_ASSERT_LESS_THAN(findOffset(ct, needle), findOffset(ct, sw));
	}

	/**
	 * Digits and symbols typed with Korean entry ON.
	 *
	 * A non-jamo key ends the syllable in progress and is then inserted as
	 * itself. Two things have to hold and neither is automatic:
	 *
	 *  - the ASCII survives verbatim, once, in the order it was typed;
	 *  - a syllable typed afterwards lands AFTER it.
	 *
	 * The second one failed. A lone jamo is a compatibility jamo that this
	 * font cannot draw, so it is held out of the string; when a digit ended
	 * that composition, HangulComposer committed the jamo into its own
	 * buffer, encodedRun() reported an undrawable COMMITTED character, and
	 * feed() refused the key whole - leaving the composer still holding the
	 * jamo while the control inserted the digit itself. The next vowel then
	 * emitted the syllable after the digit: ㄱ 1 ㅏ gave "1가".
	 */
	void test_digits_and_symbols_with_korean_on() {
		const char han[] = { (char)0xC7, (char)0xD1, 0 };
		const char ga[]  = { (char)0xB0, (char)0xA1, 0 };

		// A digit after a COMPLETE syllable: the syllable is committed,
		// the digit follows it, and a further syllable follows that.
		{
			Common::String got = type("gks123rk");
			const char want[] = { (char)0xC7, (char)0xD1, '1', '2', '3',
			                      (char)0xB0, (char)0xA1, 0 };
			TS_ASSERT_EQUALS(hex(got), hex(Common::String(want)));
		}

		// Symbols and space behave the same as digits.
		TS_ASSERT_EQUALS(hex(type("gks!")),
		                 hex(Common::String(han) + Common::String("!")));
		{
			const char want[] = { (char)0xC7, (char)0xD1, ' ',
			                      (char)0xB0, (char)0xA1, 0 };
			TS_ASSERT_EQUALS(hex(type("gks rk")), hex(Common::String(want)));
		}

		// ASCII with no Korean around it is untouched.
		TS_ASSERT_EQUALS(type("123"), Common::String("123"));
		TS_ASSERT_EQUALS(type("!"), Common::String("!"));

		// A digit interrupting a LONE jamo. The jamo was never drawable and
		// never in the string, so it is dropped: what remains is the digit,
		// alone, with nothing inserted ahead of it.
		TS_ASSERT_EQUALS(hex(type("r1")), hex(Common::String("1")));

		// ...and a syllable typed after the digit follows it, rather than
		// jumping in front of it as it did before.
		TS_ASSERT_EQUALS(hex(type("r1rk")),
		                 hex(Common::String("1") + Common::String(ga)));

		// The same, with the digit between two complete syllables.
		{
			const char want[] = { (char)0xC7, (char)0xD1, '1',
			                      (char)0xB0, (char)0xA1, 0 };
			TS_ASSERT_EQUALS(hex(type("gks1rk")), hex(Common::String(want)));
		}
	}

	/**
	 * Backspace steps over an ASCII byte inside the composer's run one
	 * character at a time, and a digit's commit is not undone by it.
	 */
	void test_backspace_over_ascii_in_a_korean_run() {
		// 한12, then back over each byte.
		TS_ASSERT_EQUALS(hex(type("gks12~")),
		                 hex(Common::String((const char[]){ (char)0xC7, (char)0xD1, '1', 0 })));
		TS_ASSERT_EQUALS(hex(type("gks12~~")),
		                 hex(Common::String((const char[]){ (char)0xC7, (char)0xD1, 0 })));

		// The digit committed 한, so this backspace removes the whole
		// syllable rather than decomposing it to 하 - which is what a
		// native IME does once a syllable has been committed.
		TS_ASSERT_EQUALS(type("gks12~~~").size(), 0u);
	}

	// ===================================== the key that reaches the toggle

	/**
	 * The Han/Yeong key travels as a keymapper action, not as a raw keycode.
	 *
	 * This is the S8 fix, and it is asserted rather than argued because the
	 * defect it closes is invisible on the machine that builds it. Measured
	 * on Linux with a diagnostic build (harness/s8keys.sh): the real X11
	 * `Hangul` keysym arrives as SDL scancode 144 (LANG1) and left
	 * SDLToOSystemKeycode() as KEYCODE_INVALID - keycode 0 at the engine.
	 * Right Alt arrives as KEYCODE_RALT only because X11 maps that physical
	 * key to Alt_R; Windows does not, which is why the old keycode test
	 * could never fire there.
	 *
	 * So: LANG1 must have a Common::KeyCode at all, and the engine must act
	 * on the action rather than on RALT.
	 */
	void test_hanyeong_is_a_keymap_action_not_a_raw_keycode() {
		// The keycode has to exist before anything can carry it.
		Common::String kb = readSource("common/keyboard.h");
		TS_ASSERT(kb.contains("KEYCODE_HANGUL"));

		// ...and the SDL2 backend has to produce it from LANG1. Without
		// this line the key is KEYCODE_INVALID and no keymap can bind it.
		Common::String sdl2 = readSource("backends/events/sdl/sdl2-events.cpp");
		TS_ASSERT(sdl2.contains("SDL_SCANCODE_LANG1"));
		TS_ASSERT(sdl2.contains("Common::KEYCODE_HANGUL"));

		// The keymapper must list it, or it is bindable by neither the
		// default mapping nor the user.
		Common::String hw = readSource("backends/keymapper/hardware-input.cpp");
		TS_ASSERT(hw.contains("KEYCODE_HANGUL"));

		// The engine acts on the ACTION.
		Common::String ev = readSource("engines/sci/event.cpp");
		TS_ASSERT(ev.contains("kSciActionHangulToggle"));
		TS_ASSERT(ev.contains("EVENT_CUSTOM_ENGINE_ACTION_START"));

		// And no longer on the raw keycode - otherwise a rebind would be a
		// lie, because right Alt would keep toggling alongside the new key.
		TS_ASSERT(!ev.contains("scummVMKeycode == Common::KEYCODE_RALT"));

		// The action is registered with all three defaults, so a keyboard
		// with a Han/Yeong key, one where X11 calls it right Alt, and one
		// with neither are all covered without the user editing anything.
		Common::String me = readSource("engines/sci/metaengine.cpp");
		TS_ASSERT(me.contains("addDefaultInputMapping(\"HANGUL\")"));
		TS_ASSERT(me.contains("addDefaultInputMapping(\"RALT\")"));
		TS_ASSERT(me.contains("addDefaultInputMapping(\"C+SPACE\")"));
		TS_ASSERT(me.contains("addDefaultInputMapping(\"S+SPACE\")"));

		// Still gated on the one config key: an SCI game that did not ask
		// for Korean input gets no keymap and no bindings.
		TS_ASSERT(me.contains("hasKey(\"sci_hangul_input\", target)"));
	}

private:
	static int countOccurrences(const Common::String &hay, const char *needle) {
		const uint n = strlen(needle);
		int count = 0;
		for (uint i = 0; i + n <= hay.size(); ++i)
			if (!strncmp(hay.c_str() + i, needle, n))
				count++;
		return count;
	}

	/** Byte offset of @p needle, or hay.size() when absent. */
	static uint findOffset(const Common::String &hay, const char *needle) {
		const uint n = strlen(needle);
		for (uint i = 0; i + n <= hay.size(); ++i)
			if (!strncmp(hay.c_str() + i, needle, n))
				return i;
		return hay.size();
	}

	Common::String readSource(const char *rel) {
		Common::String path = Common::String(SCI_TEST_SRCDIR) + "/" + rel;
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path).c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		TS_ASSERT_LESS_THAN(0u, out.size());
		return out;
	}

public:
	void test_korean_sentences_put_the_verb_first_and_are_logged() {
		const Common::String voc = readSource("engines/sci/parser/vocabulary.cpp");
		TS_ASSERT(voc.contains("retval.push_front(verb);"));
		TS_ASSERT(voc.contains("sawKorean && retval.size() > 1"));
		TS_ASSERT(voc.contains("\"[parse] sentence '%s' => %s\""));
	}

	void test_the_key_that_opens_a_parser_prompt_is_composed() {
		const Common::String ctl = readSource("engines/sci/graphics/controls16.cpp");
		// the opening key is already ASCII in the new line; the guard feeds it to the composer
		TS_ASSERT(ctl.contains("(byte)text[0] > 32 && (byte)text[0] < 127 && cursorPos == 1"));
		TS_ASSERT(ctl.contains("_koreanInput.feed(seedKey, text, _koreanRunStart)"));
		// a refused seed puts the original byte back
		TS_ASSERT(ctl.contains("text = koreanTextBefore;"));
	}

	void test_review_fixes_are_in_the_edit_control() {
		const Common::String ctl = readSource("engines/sci/graphics/controls16.cpp");
		// composed text goes in at the cursor, not at the end of the line
		TS_ASSERT(ctl.contains("text.erase(cursorPos);"));
		TS_ASSERT(ctl.contains("text += tail;"));
		// Ctrl/Alt chords bypass the composer
		TS_ASSERT(ctl.contains("!(modifiers & (kSciKeyModCtrl | kSciKeyModAlt))"));
		// a rejected composed key leaves the caret where it was
		TS_ASSERT(ctl.contains("MIN<uint16>(oldCursorPos, text.size())"));
		// the debug dump costs nothing with the channel off
		TS_ASSERT(ctl.contains("if (!DebugMan.isDebugChannelEnabled(kDebugLevelHangul))"));
		// PC-98 keeps its original draw order
		TS_ASSERT(ctl.contains("g_sci->usesKoreanText() && _screen->gfxDriver()->driverBasedTextRendering()"));
	}

	void test_parser_converts_utf8_words_before_the_cp949_lookup() {
		const Common::String voc = readSource("engines/sci/parser/vocabulary.cpp");
		TS_ASSERT(voc.contains("utf8ToCp949(tempword, kword)"));
		TS_ASSERT(voc.contains("if (g_sci->heapStringsAreUtf8())"));
	}
};
