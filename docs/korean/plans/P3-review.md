# P3 review: SCI KQ1 end to end (independent, read-only)

Reviewed: `git diff d7f47c73a07 73bf2f80ba7 -- . ':!docs'` (21 commits, 64 files, +3806/-103) on `ko-2026.4`,
worktree `/home/thkim/work/scummvm-2026.4-korean`. Context read: DESIGN.md sections 2.2, 3-6, 10-12; the plan
`P3-sci-kq1.md` (section 1 deviations) and `P3-notes.md`, taken as claims, not proof. Scratch under
`~/work/tmp/p3-review/` (packs, run configs, repro program); nothing in the worktree was edited.

## What was run

| Check | Command / place | Result |
|---|---|---|
| Unit tests | `~/work/tmp/scummvm2026-p3build/test/runner` | 666 OK (the `korean-vocab/` harness skip is printed, visible) |
| strcheck tests | `TMPDIR=~/work/tmp/p3-review python3 -I tools/korean/test_strcheck.py` | 4 OK |
| Commit messages | `python3 -I devtools/check-commit-msg.py` on each of the 21 commits | all rc 0; 21x `Assisted-by: Claude Code`, 0 `Co-Authored-By` |
| Korean first room + `ansdmf dufdj` (문을 열어) + Return | `~/work/tmp/p3-review/ko`, `go.sh kq1-ko 25 click:412,325 sleep:35 type:"ansdmf dufdj" sleep:1 Return sleep:5` (clut8, surfacesdl) | PNG sha256 `93cd2507...6fa5576`, **byte-identical** to the P3 reference `~/work/tmp/p3-kq1/s20-clut8/open-ko.png`; reply `당신은 데번트리의 잃어버린 보물 세개를 ... 재강구한다.` |
| English target, same room, `open door` | `~/work/tmp/p3-review/en`, target `kq1` (no `language=`), `click:206,162` | 320x200 window, English status line and reply "You reconsider the wisdom of returning to King Edward ..."; log has no `Hi-res text`, `korvocab` or `sci-ko` line: upstream path |
| Uppercase table `ko/SCI-KO.STR` | `~/work/tmp/p3-review/upper` (copy of the pack, only the `.str` renamed), target `language=ko` | **translation silently dropped**: 640x400 window (KO_KOR driver row), English status line `Score: 0 of 158 / King's Quest I`, no `sci-ko.str` / `korvocab` / `Hi-res text` log line, no warning (finding M1) |
| ScummVM GUI load dialog with the Korean save `문` | `~/work/tmp/p3-review/guiload`, `originalsaveload=false`, GFX=opengl, F7 | the slot is listed with its thumbnail; the caption glyphs are boxes because the headless run has no `fonts-cjk.dat`/theme (builtin theme), so legibility is UNVERIFIED here, not a P3 defect; the thumbnail has no status text (it is made from the 1x display screen, which never holds hi-res text: cosmetic) |
| kStrAt UTF-8 write semantics | `~/work/tmp/p3-review/strat_repro.cpp` built against `engines/sci/utf8.cpp` (`./strat_repro`) | see M2 |
| ASan / sanitizer lines in the re-run | `grep AddressSanitizer\|runtime error ko/run.log` | 0 (release build; the P3 ASan run was not repeated) |

## Upstream-impact audit (every hook must be inert for a non-Korean target)

Each hook was read for its gate:

- `TextMode`: `decideTextMode()` gives `kSingle` for a target without `language=` or with a language that has no
  translation folder; `getLanguage()` then returns the detection entry's language. English run above: upstream.
- `SciEngine` ctor/run: the `ko/` SearchMan entry, `ScriptStrings::load`, the fingerprint md5 and `SciHiResText`
  are all behind `_textMode.utf8()` / `_textMode.hires`.
- `kStrAt`/`kStrLen`/`kFormat`/`SegManager::strncpy`/text16 loops/`SwitchToFont1001OnKorean`/`kernelTexteditChange`:
  behind `getTextMode().utf8()`. `kStrCpy`/`kStrCat` tagging, `translated()`, `keyOf()`, `strSplitLanguage(key)`:
  behind `_scriptStrings.isLoaded()` (false when no table was loaded; `lookup()` also checks `_loaded`).
- Screen/paint16/cache: behind `_textPlane` (null when no `SciHiResText`); `putPixel()` gains one null-pointer
  test per pixel for everybody (`clearTextRect` is inline: `_textPlane && ...`), no behaviour change.
- Driver: `compositeText` only when `_textPlane && !isEmpty()`; `init.cpp` `requestRGB` only when `getHiResText()`.
- Parser: `Vocabulary` loads the word table only for `hasParser() && utf8() && !_foreign`; `lookupKorean`,
  `joinPairs`, `arrange`, the fallback tree and the `said()` retry are behind `_koreanVocab.isLoaded()` /
  `_koreanFallback.empty()` / `_koreanFallbackValid`.
- Event/keymap: `_hangulInput` requires `utf8()`; the `sci-korean` keymap is added only when the target's
  `language` is `ko`; the OS-IME surrogate rewrite is behind `_hangulInput`.
- Detection: `identifyGame` lists the game folder once per start when `language=` is set (any SCI target with a
  language, e.g. a German game) and takes the upstream path unless the folder holds that language's translation;
  `detectGames` adds variants only for folders with a translation.
- `GfxFont` `uint16 -> uint32`: all four subclasses updated (`GfxFontFromResource`, `GfxFontSjis`, `GfxFontKorean`,
  new `GfxFontHiRes`); `GfxFontFromResource` indexes `_chars[chr]` behind `chr < _numChars` (now cannot wrap);
  `GfxFontKorean::isDoubleByte` masks `& 0xFF`; implicit `uint16 -> uint32` at the SCI32 callers (`text32.cpp`,
  the notes' SCI32 build links). No array indexed by an unmasked `chr` was found.
- The two defensive upstream changes:
  - `Width()/Draw()` `while (len-- > 0)` and `DrawStatus()` `int16 textLen` (was `uint16`): differ from upstream only
    when `len` is negative. The producers are `GetLongest()` (an `int16` count of a line, never negative for text
    < 32 KiB) and `strlen` of a status string; upstream's `while (len--)` on a negative value looped ~65k times
    reading past the buffer. Inert for every real caller.
  - `tokenizeString()` stops a word at `VOCAB_MAX_WORDLENGTH - 1` (255) bytes: upstream wrote the 256th byte past
    `currentWord[256]` (and kept going). Differs only for a 256+ character word, which no edit control can produce
    (the console `parse` command could). Inert in play. In non-UTF-8 mode the CP437 lowercase map is still applied.
- `PATCHES.md`: the `ko/03-sci` rows name every upstream file the diff touches (checked file by file against
  `git diff --stat`); `dists/engine-data/hires_text/maps/korean-default.map` is the fork's own file.

## Findings (ranked)

### Blocker

None.

### Major

**M1. `textModeInputs()` finds the string table case-sensitively; a `ko/SCI-KO.STR` pack silently runs in CP949 mode.**
`engines/sci/sci.cpp:243-251` (`textModeInputs`): `translationFolder` comes from
`Common::translationFolderLanguages()`, which matches `sci-%s.str` and `hires*.map` *in any case*
(`common/language_folder.cpp:51`), but `stringTable` is `gameDataDir.getChild(table).exists() ||
folder.getChild(table).exists()`, which is case-sensitive on Linux/macOS. `decideTextMode()` then takes
`korean && !stringTable` -> `TextMode::kCp949` (`textmode.cpp:67-68`): language KO_KOR (the 2x driver row, the
upstream `message.map` path), but no UTF-8 heap, no ScriptStrings, no hi-res text, no Korean parser, and no warning.
`ScriptStrings::load()` itself goes through SearchMan, which *would* have found the file.
Repro: `~/work/tmp/p3-review/upper` (pack copy with `ko/sci-ko.str` renamed `SCI-KO.STR`, `language=ko`): 640x400
window, English `Score: 0 of 158`, `run.log` has no `sci-ko` / `korvocab` / `Hi-res text` line.
The same branch is taken by a `ko/` folder that holds only maps (no `.str`): `translationFolder` true, `stringTable`
false -> `kCp949`; with `ko/resource.msg` UTF-8 text the CP949 lead-byte test (`SwitchToFont1001OnKorean`) would then
pick font 1001 and `GfxFontKorean` `error("Could not load ScummVM's 'korean.fnt'")` (UNVERIFIED, read only).
Fix (small): derive `stringTable` from the same any-case listing (`folder.getChildren()` +
`equalsIgnoreCase(table)`, or extend `translationFolderLanguages` to report which kind of file matched), and in
`decideTextMode` treat `korean && translationFolder && !stringTable` as `kUtf8` with a note ("translation folder
without sci-ko.str") rather than `kCp949`; keep `kCp949` for `detected == KO_KOR` only. Add a `TextMode` test for the
maps-only folder and a `textModeInputs` test is not possible without a file system, so a warning line is the
minimum.

**M2. `kStrAt` write on a UTF-8 heap overwrites bytes in place: the string becomes invalid UTF-8.**
`engines/sci/engine/kstring.cpp:171-187` (`strAtUtf8`, `write` branch): the new code point's `n` bytes are written
at `utf8WriteOffset(index)` with `strAtByte()` for each byte, without removing the old code point's bytes or shifting
the tail. Writing ASCII over a syllable leaves two orphan continuation bytes; writing a syllable over ASCII eats the
next two characters. Only a NUL write is harmless (it terminates). Repro (`~/work/tmp/p3-review/strat_repro.cpp`,
same helpers, same algorithm):
```
before: "가나" bytes=EA B0 80 EB 82 98  kStrLen=2
after StrAt(0,'A'): "A??나" bytes=41 B0 80 EB 82 98  kStrLen=4
after StrAt(0,U+AC00): "가d" bytes=EA B0 80 64  kStrLen=2
```
`decodeUtf8Char` then passes the orphans through as code points 0xB0/0x80 (drawn as whatever the face routes them
to, counted by `kStrLen`). Whether a KQ1 script writes a non-NUL value with `StrAt` was not traced (UNVERIFIED for
KQ1); SCI0 system scripts do (capitalising the first letter of a Print/edit string, `(StrAt str 0 (- (StrAt str 0)
32))`), and the Camelot 36-byte save records DESIGN.md names use this kernel call. The return value (`old`) is right.
Fix: in the write branch rebuild the string (`head` + encoded + `tail` after the old code point's bytes), check
`new size + 1 <= dest_r.maxSize`, write it with `SegManager::strncpy` (one code path, one bound), warn and refuse
when it does not fit. Add a unit test over the helper semantics (ASCII over a syllable, a syllable over ASCII, NUL).

**M3. A `kFormat` result (now Korean, longer) is copied into the script's buffer with no bound but the segment end.**
`engines/sci/engine/kstring.cpp:519-524` writes the translated, particle-resolved text with `strcpy_()`;
`SegManager::strncpy` (`seg_manager.cpp:702-718`) cuts only when the text reaches `dest_r.maxSize`, and for a
script-local buffer `maxSize` is the bytes left in the *segment*, not the buffer the script declared. A translation
longer than the English buffer therefore overwrites the following locals/objects silently (no host overflow, no
warning). Upstream has the same latent behaviour, but the translations make the long case the normal case. No
corruption was observed in the KQ1 runs (UNVERIFIED as a KQ1 failure); the plan defers "growing a DYNMEM buffer" to
P4, which does not cover locals. Required before P4 (QFG2/Camelot have long formatted strings): at least a
diagnostic, e.g. compare the written length with the English length of the untranslated format result and warn
(`kDebugLevelKorean`) when the Korean result is longer, so the harness can shorten or the engine can grow the
target; a real fix needs the buffer size (script locals: the declared array size is not known to the VM, so the
practical answer is a per-game/harness cap, with a loud warning).

### Minor

**m1. `identifyGame` restores `language` into the transient domain instead of erasing its own transient key**
(`engines/sci/detection.cpp:255-258`). The value is the same, so detection and the engine see `ko`, but the
transient domain now shadows the target's own key for the session (edits of the target's language in the in-game
options are hidden until the launcher clears the transient domain, `base/main.cpp:901`). Fix:
`ConfMan.getDomain(kTransientDomain)->erase("language")` when it was not set there before.

**m2. The Han/Yeong keymap defaults are wide** (`engines/sci/metaengine.cpp:704-712`): `RALT` is AltGr on most
European layouts, `S+SPACE` fires when Shift is still held after a capital, `F12`/`SCROLLOCK` are extra. Each
toggles the composer silently. Suggest `HANGUL` + one explicit chord (`C+SPACE`), the rest left to the keymapper.

**m3. The edit control's `max` selector is taken as bytes** (`controls16.cpp:181`, `KoreanEditLine::key`): a
Korean line holds `max/3` syllables (KQ1 parser: 13; a 20-char save name field: 6). This is the safe reading (the
script's buffer is `max+1` bytes), but it is user-visible and undocumented; say so in the notes and revisit with M3
(a bigger buffer is the only way to more syllables).

**m4. `kGetSaveFiles` cuts a long UTF-8 name mid-sequence** (`kfile.cpp`, `Common::strlcpy(..., kMaxSaveNameLength)`):
a name saved from the ScummVM GUI dialog with more than 35 bytes (11 syllables) shows a dangling lead byte in the
original restore list. In-game names cannot exceed the field. Fix: cut on a code point boundary
(`utf8PrevBoundary`) in UTF-8 mode.

**m5. `GfxFontHiRes::getHeight()` is at least `ceil((ascent+descent)/2)`** (`hirestext.cpp:95-97`): with the 16 px
fallback face every SCI font is >= 8 px tall. KQ1's fonts are 8 px so nothing moves; P4 games with 6 px fonts get
taller lines (menus, status bars, text boxes) unless their maps route those ids to a smaller face. Not a P3 bug;
record it in the P4 map work.

**m6. The Korean fallback tree is built only after a successful primary parse** (`grammar.cpp:636-646`,
`parseGNF` returns 1 at "No results" before reaching it). A sentence whose rearranged form the grammar rejects
never gets its particle-free form tried. `kParse` reports failure either way, so the only loss is a `said()` that
the fallback would have matched. Measure with k16said (Task 20) before changing.

**m7. `strcheck.py` is stricter than the engine on numbers** (`NUM` rejects `05`; `ScriptStrings::readNum` accepts
it), and the BOM in `check()`/`main()` is a literal U+FEFF in the source (invisible; fragile). Use `"﻿"`. The
`-I` run and determinism are fine; `check` returns 0 with duplicates and with no header (documented, intended).

**m8. Savegame thumbnails and the pre-transition frame never hold hi-res text** (the plane is not part of
`_displayScreen`): thumbnails show no status line; when `kDrawPic` clears the plane under the new picture the old
display's Hangul vanishes at the start of the transition. Cosmetic.

**m9. `texteditChangeUtf8` `undo()` after `adopt()` only** (`controls16.cpp:189-209`): when `adopt()` changed the text
(first jamo) and no key event follows, `changed` is true and a too-wide line would `undo()` to a stale snapshot
(`snapshot()` is only taken in `key()`); unreachable for a one-glyph line, but `adopt()` should snapshot too.

### Nits

- `putPixel()` builds a `Common::Rect` and calls `clearLowresRect()` per pixel on Korean targets (a full-screen pic
  is 64k calls into the banded plane). The runs are fine; if a profile ever shows it, clear per span in `GfxView`/
  `GfxPicture` instead.
- `kFormat`'s `%s` padding (`strLength - slen`) counts bytes, so a `%-10s` column with Hangul is narrower by two
  columns per syllable (rare in SCI0).
- `kTextSize`/`_k_GenericDrawControl` translate a `TEXTEDIT` control's text through `keyOf()`: a default text copied
  from a script string shows its translation while the buffer holds the English; editing then mixes them. Not seen in
  KQ1 (the prompt starts empty).
- The `korean-default.map` comment edit is fine; `DIST_FILES_HIRES_TEXT` installs `NEODGM.TXT` (the licence) too,
  which is right.
- Tests: `textmode.h`, `script_strings.h`, `korean_input.h`, `hirestext.h`, `utf8.h`, `korean_parser.h` test what
  their names say; the header-match test uses upper-case hex to exercise `anyCase`; the harness-data test prints its
  skip. No test passes trivially with the code removed except Task 7's type change (compile-only, as the notes say).

## Verdict

P3 is a sound base for P4: the Korean paths are gated so that an English target runs upstream (verified by the
English run and by reading every hook), the hi-res plane stays in step through save/restore bits, XOR/swap and
pixel writes, the layout walks code points, the parser and the edit line work end to end on KQ1 (re-run byte-identical
to the reference), and the two defensive upstream edits are inert for valid input. Nothing blocks.

Required before P4 (data games LB1/Camelot/KQ4/QFG1E/QFG2/LSL1):

1. **M1**: any-case string-table detection and `kUtf8` for a translation folder without `.str` (never silent CP949).
2. **M2**: `kStrAt` write rebuilds the string instead of overwriting bytes (Camelot's save records and SCI0 system
   scripts write with `StrAt`).
3. **M3**: a length diagnostic (at least) when a `kFormat` result is longer than the English one, so the first
   overflow in a P4 game is seen, not guessed.

Recommended with P4 (not required): m1, m4, m9; m5 noted for the P4 maps; m6 measured by k16said (Task 20).
