# P4 review (Claude lane, independent)

Scope: `5741781806a..8bb55159d99` (nine engine files, two test files), read in the detached checkout
`~/work/tmp/scummvm-review-p4`. Runs used the implementer's builds (`~/work/tmp/scummvm2026-p4build`,
`~/work/tmp/scummvm2026-p4asan`), one probe build of my own (`~/work/tmp/p4review-claude/bld`, ASan, from a
`git archive` of `8bb55159d99` with one change, never in the worktree), the scratch data under `~/work/tmp/p4/`
and the run driver `~/work/tmp/p4/go.sh`/`go1.sh`. All my run folders are under `~/work/tmp/p4review-claude/`.

## Verdict

As a base for P5 (AGI), P4 is sound: none of the findings below is in code that AGI shares, and the English
targets are unchanged. There is one exception to that inertness, and it is P3 code: the copy-path bug in M1 is
in the SCI Korean path only.

P4 is **not** finished as an SCI phase. The "QFG1 VGA heap corruption" in the notes is **ours, not upstream**:
`SegManager::strncpy(reg_t, reg_t)` copies a translated string from the stack into a `kMemory` block sized for
the English text, and DYNMEM growth (task a4) does not cover that path (M1). Required fixes, in order:

1. **M1**: grow (or bound) the destination on the `non-raw -> raw` path of `strncpy(reg_t, reg_t, n)` for UTF-8
   games. Correct P4-notes finding 4: the crash is in our copy path, not in "SCI1.1 hi-res text".
2. **M2**: stop the answer view's synonym expansion from letting wrong answers pass (requirement 2).
3. Minor items m1 and m2 when they are next touched. m3 is a note only.

## Findings

### M1 (major): StrCpy from a stack/local string into a DYNMEM block overruns the heap; this is the QFG1 VGA crash

`engines/sci/engine/seg_manager.cpp:771-778` (`non-raw -> raw` branch of `strncpy(reg_t, reg_t, n)`).
P4 added growth only to the `const char *` overload and to a raw source in the same block. A source in a
non-raw segment (stack, locals) still runs upstream's loop. That loop writes `dest_r.raw[i]` up to the
source's NUL and never checks `dest_r.maxSize`. Upstream gets away with it because the script sized the block
for the English text. A Korean text longer than that runs off the block.

Reproduction (QFG1 VGA, `data/qfg1`, `language=ko`, the implementer's final ASan build):

```
ASAN_OPTIONS=detect_leaks=0 BIN=~/work/tmp/scummvm2026-p4asan/scummvm ~/work/tmp/p4/go1.sh ~/work/tmp/p4review-claude/qv-koasan qfg1-ko 40
==19059==ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 1
    #0 Sci::SegManager::strncpy(reg_t, reg_t, unsigned long)   seg_manager.cpp:775
    #2 Sci::kStrCpy                                             kstring.cpp:109
0x... is located 0 bytes after 68-byte region, allocated by Sci::SegManager::allocDynmem <- kMemory
```

Under gdb (`qv-gdb/gdb.log`): `src` is segment type `SEG_TYPE_STACK` (non-raw, maxSize 8002), `dest` is
`SEG_TYPE_DYNMEM` with maxSize 68, `n = 0xFFFFFFFF`, and `getString(src)` is the 158-byte
`지금 사용 중인 이 게임은 디자이너, 미술가, 프로그래머, 음악가를 비롯한 수많은 사람들의 노력으로 만들어졌습니다.`
The English target on the same data under the same ASan build (`qv-enasan`) shows 0 AddressSanitizer lines and
reaches its first screen. So the cause is the translation length meeting an unbounded copy path. It is not an
upstream bug in the game, and it explains why the P3 binary crashes too.

The fix was verified in a probe build. It routes every source of a UTF-8 copy into a raw destination through
the copy-out, and from there through `strncpy(reg_t, const char *, n)`, which grows or cuts:

```cpp
	if (g_sci && g_sci->getTextMode().utf8() && dest_r.isRaw &&
	    (!src_r.isRaw || (src.getSegment() == dest.getSegment() && getSegmentType(dest.getSegment()) == SEG_TYPE_DYNMEM))) {
		Common::String text;
		if (src_r.isRaw)
			text = Common::String((const char *)src_r.raw, Common::strnlen((const char *)src_r.raw, MIN<size_t>(n, src_r.maxSize)));
		else {
			text = getString(src);
			if (text.size() > n)
				text = Common::String(text.c_str(), n);
		}
		... (rest of the existing same-block branch)
```

With this change the same run gives 0 AddressSanitizer lines. The log shows
`0032:0000 (kMemory() critical) grown from 68 to 159 bytes` and, later, `002b:0000 ... grown from 94 to 226 bytes`.
The notice screen renders fully in Korean (3 lines, framed), and the game goes on to the Sierra logo
(`qv-fix/`, `qv-f7/`).

For the final fix, keep upstream's no-padding behaviour for non-raw sources: run the padding loop only when
`src_r.isRaw`. A unit test needs a SegManager, so add the ASan QFG1 VGA start to the checks instead. P4 games
were ASan-clean on the implementer's short paths, so whether a P4 game reaches this path is UNVERIFIED. Any
SCI01+ game that `StrCpy`s a formatted or message text from a temp array into a `Memory` block will.

Related, and UNVERIFIED: the 158-byte source had already been written into the stack temp array by
`MessageState::outputString`. That bounds the write only by the stack segment's maxSize, not by the script's
temp size. This is the P3 "buffer of no known size" class, which `kFormat` warns about and `kMessage` does not.

### M2 (major, requirement 2): the answer view lets wrong Korean answers pass that the English game refuses

`engines/sci/parser/korean_parser.cpp:312-336` (`englishLine`), used by `kstring.cpp:160` (`koreanAnswerView`).
The view writes every English word of every group a token may belong to, and the game's search is a substring
search. Synonyms and other readings therefore bring in English words that contain the answer. In practice
that means an English synonym that holds the answer's letters (`그리핀` → `... airplane ...`) or another reading
of an ambiguous Korean word.

Reproduction in play: QFG2 room 199, question #11 (element of the magic shop owner, answer "air"), same key
sequence as `b2run.sh`, with my own run folders:

```
door-griffin/run.log:  answer "그리핀" reads as "airplane griffin griffon gryphon jet merv plane"
door-despair/run.log:  answer "절망" reads as "despair"
```

Both screenshots (`door-*/scummvm-qfg2-ko-00001.png`) show the riddle (#16) next, which means the answer
**passed**. The English game refuses "griffin". ("despair" also passes in English, so that one is parity.)

From the harness map (`scripts/fp.py` over `harness/dos/release/qfg2/k3map.json` + `k2targets.json`):

- 19 Korean words read with "air" in them, among them `그리핀`, `비행기`, `안경` (`pair`), `머리` (`hair`),
  `계단` (`stair`).
- 47 Korean words read with "all", among them `먹어` (`swallow`), `길` (`alley`), `화염` (`fireball`),
  `작은` (`small`).
- `쥐`/`생쥐` read as `fenrus mouse rat`.

QFG1 room 29/22 has the same mechanism (not run).

Minimal fix: give each `KoreanAnswerRoom` row the words its script searches for (QFG2 199: the answer words of
script 2; QFG1 22/29: script 802's). For each token, `englishLine` then emits only the search words that are
whole words of the token's groups. If none match, the line is empty and the typed text stays as it is. This
keeps `스테판` → `stefan` (same group) and drops the substring accidents. Add a unit test with `그리핀`-style
data: a group {griffin, airplane} must not read as containing "air" when the search list is {air}.

### m1 (minor): `sci11StringsStart` reads little-endian; `identifyOffsets` reads script-endian

`engines/sci/engine/script_strings.cpp:279-293` vs `script.cpp:444` (`getUint16SEAt`). On a big-endian SCI1.1
heap (Mac), the hook (`script.cpp:489-490`) finds no `0x1234` and starts the strings at the first object. That
gives bogus string ids and capacities (`stringBytesAt` drives `strcpyBounded`). It only applies when a UTF-8
target is loaded, so it is latent: every Korean pack is DOS. Fix: pass `g_sci->isBE()` (or read with
`READ_SCI11ENDIAN_UINT16`). The hook itself is still UNVERIFIED on data (the restore dialog of QFG1 VGA was
not reached in my runs either). M1 is what blocked the implementer's a1 step 4: with M1 fixed, QFG1 VGA
starts and that step can now be run.

### m2 (minor): same-block copy pads from the uncut length

`seg_manager.cpp:766-768`: when the inner `strncpy` had to **cut** (destination not at offset 0, so no growth),
padding starts at `text.size() + 1`, not at the cut length + 1. The bytes between them keep the cut-off tail.
The string is still terminated. Fix: pad from `strnlen(grown.raw, grown.maxSize) + 1`.

### m3 (nit): `hiResFont` masks `kSci32SystemFont`

`graphics/cache.cpp:75-76`: for SCI32 the id `-1` is not a resource and becomes 2047. It is unreachable today,
because no SCI32 Korean text plane exists. Guard it with `getSciVersion() < SCI_VERSION_2` when SCI32 hi-res
text arrives. The rest of a5 matches `GfxFontFromResource` exactly (`scifont.cpp:201-202`).

## Checked and found sound

- **Inertness**: every new path checks `getTextMode().utf8()` first (view, hook, growth, copy-out), or needs a
  non-null hi-res object (`hiResFont`). The English QFG1 VGA run on the ASan build was clean. The
  implementer's seven English first screens were not repeated.
- **DYNMEM growth, segment side**: `growDynmem` only grows at offset 0. Script `reg_t`s (segment:offset) stay
  valid. No C++ caller keeps a raw pointer to the destination across the copy: every `strcpy_`/`strncpy`/
  `strcpyBounded` caller passes an owned `Common::String`, and `strAtUtf8` returns right after
  `strcpyBounded`. `realloc` matches the `calloc` in `allocDynmem` and the `free` in `~DynMem`. `maxSize`
  follows `_size` (`DynMem::dereference`).
- **Saved games**: `DynMem::saveLoadWithSerializer` (`savegame.cpp:604-612`) syncs `_size` and then `_size`
  bytes, and on load it `calloc`s the saved size. A grown block therefore saves and restores at its grown size.
- **The shared "system strings" block** (`_saveDirPtr` at 0, `_parserPtr` at +256): it can only grow through a
  copy of 512 bytes or more to offset 0. `_parserPtr` is a `reg_t` and stays valid.
- **Answer rooms**: QFG2 199 and QFG1 29 are confirmed by play logs (the implementer's and my 199 runs). Room
  22 is UNVERIFIED, as the notes say. The table and `englishLine` match jamo `e5295f1b411`/`40e65ff93ac`. P4
  also keeps `_koreanFallback` across the view's tokenisation, which jamo did not.
- **Pack test**: it fails on a fingerprint mismatch (`checkTranslationHeader` → `loadFromStream` false), and on
  a v2 table or a wrong count. It passes with a visible `TS_WARN` when `korean-packs/` is absent, which is
  always the case in `make test`'s working directory.
- **P4-notes findings 1-3** (LSL1 icon box, composer off without a word table, Alt+S leaking into the
  composer): I have no new evidence and no fix to add.
