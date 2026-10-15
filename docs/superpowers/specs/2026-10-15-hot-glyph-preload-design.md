# Hot glyphs: preload what the game draws, stream the rest

Date: 2026-10-15. Branch: jamo. Status: design (approved in direction by the user: "정적분석을 해서 미리 메모리에
올릴 문자를 선택하고 없으면 디스크에서 읽게 할까?" -> "응").

## Why

A face is read whole or streamed (`[load]` whole|stream|auto, README "Whole or streamed"). Whole costs the full
2,350-syllable face although a game draws about half (MI2's bundle: 1,194 syllables; a title/card charset only a
few dozen). Streaming costs a disk read the first time each glyph is drawn - on DOS a seek per cache miss. Subsetting
the face (dropping unused glyphs) was rejected: a missed character would have no glyph. Instead: keep every glyph in
the file, and keep in memory only the glyphs a static analysis says the game draws with that face. A wrong guess
costs one disk read, never a box.

## What

### The file: a hot prefix

- The bake orders glyph data so the hot glyphs come first: glyph indices 0..N-1. The code point table already maps
  code point -> glyph index, so only the cmap indices and the metrics order change; every glyph's bitmap is
  byte-identical.
- Header flags bit 3 (0x8, "hot prefix", currently unused: 0x1 proportional, 0x2 jamo sets, 0x4 marks at origin)
  says a **HOTG** block follows the code point table (after JSET when both are present):
  `4 "HOTG", 2 version (1), 2 reserved (0), 4 N` (hot glyph count, 1..glyphs). Older readers ignore the bit and the
  block (same rule as JSET: the block ends where the next table starts; a block that does not validate is ignored
  with one warning, the font loads as without it).

### The engine

- `auto`/`stream` faces with a hot prefix: at open, read glyph data [0, N) in one read into a resident buffer (not
  part of the LRU cache, not evicted); glyphs >= N go through the existing streamed path and cache. `whole` faces:
  unchanged (everything is in memory). `auto` with data <= threshold: whole as today (the prefix changes nothing).
- If the resident allocation fails: plain streaming (warn once).
- Prefetch: hot glyphs are skipped (already resident).
- Jamo-set faces: pieces are glyphs; the bake marks every piece hot (they are few), so a jamo face streams only its
  rare non-Hangul glyphs.
- Debug: `caches all` (debug socket) reports per face: resident hot bytes, hot hits, cold misses.

### The analysis (tools)

- Per game and per charset, the set of characters drawn: walk the scripts (tools/korean/scummscript.py), attribute
  each print/text opcode to the charset set before it (conservatively across control flow; undecidable -> every
  candidate charset), map original strings to translations through the bundle (korean.trs), add verb/object names
  and anything else drawn with the charset. Validate against engine text logs from the route runs (every logged line's
  characters must be in its charset's set; report misses).
- The hot set of a face = union of the sets of the charsets that use it (map chains), plus ASCII. Rest of the
  base (2,350 + supplement) stays cold in the file.
- Bake: `--hot-chars FILE` (or a plan tsv column) orders and flags the face; facecoverage.py also reports hot-set
  coverage (informational - a miss is a disk read, not a failure).

## Measures of success

- MI2 on DOS (DOSBox-X 8 and 16 MB): memory with hot prefixes vs `[load]` whole/auto, and count of cold reads on the
  standard routes (should be ~0 for dialogue faces).
- Rendering byte-identical to whole loading (dump compare) on the routes.

## Out of scope

- Lazy opening of faces never drawn (separate TODO card t_21701c7a).
- Hot sets for engines other than SCUMM (SCI/AGI read the block; their analysis comes later).
