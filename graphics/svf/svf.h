/* svf.h - portable SVFN bitmap font renderer (spec/renderer-design.md, format spec/SVF.md). Reads SVFN v3.
 *
 * C99, no allocation: the caller owns every byte (the face struct, scratch cells). Only <stdint.h>, <stddef.h> and
 * <string.h> are used by the library.
 *
 * Bad arguments: every function is safe with NULL pointers. One that returns svf_err returns SVF_E_ARG for a NULL face,
 * chain, target or out-parameter, and for a NULL buffer or text given with a length above 0 (a NULL buffer of length 0
 * is an empty one: too small, SVF_E_NOMEM, or for svf_open_mapped an empty file, SVF_E_MAGIC). One that cannot return
 * svf_err returns the sentinel written at it (a NULL face reads as a zeroed face, what a failed open leaves: accessors
 * 0, nothing found). c/tests/test_args.c calls each with each.
 */
#ifndef SVF_H
#define SVF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads of mapped face bytes. Every read of a mapped face's bytes goes through these; a build whose faces live outside
 * the data address space overrides SVF_RD8 (an AVR build: -DSVF_RD8(p)=pgm_read_byte(p)); the wider reads are built
 * from it, little-endian, so they need no override. A streamed face's bytes are RAM (its tables buffer, cache and the
 * caller's cell) and are read with plain loads whatever SVF_RD8 is, so one build can draw PROGMEM faces and SD-card
 * faces. SVF_RD8_PLAIN is defined when SVF_RD8 is not overridden (the library then compiles no source test). */
#ifndef SVF_RD8
#define SVF_RD8(p) (*(const uint8_t *)(p))
#define SVF_RD8_PLAIN 1
#endif
#ifndef SVF_RD16LE
#define SVF_RD16LE(p) ((uint16_t)(SVF_RD8(p) | ((uint16_t)SVF_RD8((const uint8_t *)(p) + 1) << 8)))
#endif
#ifndef SVF_RD32LE
#define SVF_RD32LE(p) ((uint32_t)SVF_RD16LE(p) | ((uint32_t)SVF_RD16LE((const uint8_t *)(p) + 2) << 16))
#endif

typedef enum {
	SVF_OK = 0,
	SVF_E_MAGIC,   /* not an SVFN file */
	SVF_E_VERSION, /* not version 3; version 2 is not read since v3: convert with tools/svf2to3.py */
	SVF_E_BPP,     /* bits per pixel not 1, 2 or 8 */
	SVF_E_RANGE,   /* a size or offset outside the file, an empty glyph box, a code point or glyph that is absent */
	SVF_E_NOMEM,   /* a caller buffer too small */
	SVF_E_IO,      /* a read callback failed */
	SVF_E_ARG,     /* a null pointer or bad argument */
	SVF_E_LAYOUT   /* layout bytes, flag bits, alignment or offset table not valid, or a layout this build does not read */
} svf_err;

/* Layout units of a version 3 face (header bytes 9 and 10, each a log2 of 1..64 bytes): REC the record unit, ROW the
 * row unit, TAB the table unit. SVF_LAYOUT(rec, row, tab) is the code of one such triple; the offset width (u16 or u32)
 * is not part of it. The named triples are the writer profiles of tools/jamoset.py: mcu-strict (4 / 1 / 4 bytes),
 * tight (dos and wasm, 1 / 1 / 1 bytes) and simd16 (16 / 1 / 16 bytes).
 *
 * SVF_LAYOUTS: unset, the build reads every valid layout; otherwise a comma list of SVF_LAYOUT codes it reads
 * (-DSVF_LAYOUTS=SVF_LAYOUT_MCU_STRICT for every source). A face outside the list is refused at open with
 * SVF_E_LAYOUT, before any table is read. With one code the three units fold to constants. */
#define SVF_LAYOUT(rec, row, tab) ((uint16_t)((rec) | (row) << 3 | (tab) << 8))
#define SVF_LAYOUT_MCU_STRICT SVF_LAYOUT(2, 0, 2)
#define SVF_LAYOUT_TIGHT SVF_LAYOUT(0, 0, 0)
#define SVF_LAYOUT_SIMD16 SVF_LAYOUT(4, 0, 4)

/* Header flags (header bytes 6..7). */
#define SVF_FLAG_METRICS 0x0001u         /* a metrics table */
#define SVF_FLAG_JAMO 0x0002u            /* a JSET block follows the code point table */
#define SVF_FLAG_MARKS_AT_ORIGIN 0x0004u /* combining marks are drawn at their origin */

/* Status bits found at open (svf_status). */
#define SVF_FACE_CMAP_LINEAR 0x0001u    /* the code point table is not strictly increasing (unsorted, or a code point
                                         * listed twice): looked up by a linear scan; the last entry for a code
                                         * point wins, as in jamoset.parse_svfn */
#define SVF_FACE_DY_IGNORED 0x0002u     /* the header's dy range is invalid: every glyph is drawn at dy 0 */
#define SVF_FACE_METRICS_DROPPED 0x0004u /* flags name a metrics table outside the file or inside the header: dropped,
                                         * cell advances */
#define SVF_FACE_JSET_BAD 0x0008u        /* flags name a JSET block that fails validation (jamoset.unpack, or a piece
                                         * number with no glyph), or has a version other than 1, 2 or 3 (a version 3 block too in a
                                         * build with neither SVF_JMAP nor SVF_JMAP_RECOG): the face is plain,
                                         * no syllable is composed */
#define SVF_FACE_JSET_UNREAD 0x0010u     /* the JSET part is well formed (a version 1 or 2 header) but its reader is not
                                         * compiled (SVF_JSET_V1 / SVF_JSET_V2 = 0): the face is plain */
#define SVF_FACE_JMAP_BAD 0x0020u        /* a version 3 block (JMAP) fails a JMAP rule, its fallback included (a LIST block
                                         * in a build without SVF_JMAP_LIST: a header rule or its fallback) */
#define SVF_FACE_JMAP_UNREAD 0x0040u     /* a version 3 block (JMAP) in a build with SVF_JMAP_RECOG and without SVF_JMAP (or a
                                         * LIST block without SVF_JMAP_LIST): a fallback JSET it carries is read instead
                                         * (JSET_UNREAD / JSET_BAD tell its fate),
                                         * else plain (without SVF_JMAP_RECOG the block is JSET_BAD) */

/* Jamo pieces are glyphs at code point SVF_PIECE_BASE + piece number; SVF_PIECE_BLANK in a JSET table draws nothing. */
#define SVF_PIECE_BASE 0xF0000u
#define SVF_PIECE_BLANK 0xFFFFu

typedef struct {
	uint8_t advance;
	int8_t bearing_x;
	uint8_t ink_w;
	int8_t dy; /* rows below the cell position; 0 when outside the face's dy range */
} svf_metrics;

/* Reads @p len bytes of the file at @p offset into @p dst; returns the bytes read (anything but @p len fails the read,
 * SVF_E_IO), or < 0 on an error. Never asked for a byte at or past the file length given to svf_open_stream. */
typedef long (*svf_read_fn)(void *ctx, uint32_t offset, uint8_t *dst, uint32_t len);

/* SVF_JMAP (switch doc below, with the other size switches) is needed here: it adds fields to svf_face. */
#ifndef SVF_JMAP
#if !defined(__AVR__) && !defined(SVF_SMALL) && \
    (defined(__unix__) || defined(_WIN32) || defined(__APPLE__) || defined(__DJGPP__) || defined(__EMSCRIPTEN__))
#define SVF_JMAP 1
#else
#define SVF_JMAP 0
#endif
#endif

/* A face. The caller allocates it (any storage) and passes it to an open function; the fields are private. */
typedef struct svf_face {
	const uint8_t *data;  /* mapped: the whole file. Streamed: the caller's tables buffer (stream state, then the code
	                       * point table, the metrics table, the JSET region and the glyph offset table), the offsets
	                       * below except data_off rebased into it */
	svf_read_fn rd;       /* streamed: the read callback (NULL for a mapped face) */
	void *ctx;            /* streamed: its context */
	uint8_t *cache;       /* streamed: the block cache (slot tags, then slot blocks), or NULL */
	uint32_t cmap_off;    /* code point table: count entries of u32 cp, u32 glyph index */
	uint32_t metrics_off; /* metrics table, 4 bytes a glyph; valid when status has no METRICS_DROPPED and flags bit 0 */
	uint32_t data_off;    /* glyph data: records at the offset table's entries (a file offset,
	                       * mapped or streamed) */
	uint32_t goff_off;    /* glyph offset table, count + 1 entries: a file offset when mapped, an offset into the
	                       * tables buffer when streamed */
	uint32_t jset_off;    /* JSET region (flags bit 1): the header's offset and size. Where a version 3 block (JMAP) has a
	                       * fallback JSET, jset_off/jset_len are re-pointed at the fallback once the block is checked (a
	                       * SVF_JMAP build, or a SVF_JMAP_RECOG build without SVF_JMAP), so they always name the JSET part
	                       * that is read; in a SVF_JMAP build jmap_off keeps the block's start */
	uint16_t jset_len;    /* a region longer than any block the build reads is SVF_FACE_JSET_BAD at open (clipped to
	                       * 0xFFFF; a version 3 region is JMAP_BAD with SVF_JMAP, JMAP_UNREAD or JMAP_BAD with
	                       * SVF_JMAP_RECOG, instead; a region of exactly 0xFFFF is never valid);
	                       * a valid block's length (a version 1 or 2 block's compat bytes, and a fallback's, are its last
	                       * 51; a JMAP block's are at offset 16) */
	uint16_t count;  /* glyphs */
	uint16_t flags;  /* header flags */
	uint16_t status; /* SVF_FACE_* */
	uint16_t stride; /* bytes per cell: pitch * cell_h */
	uint16_t slots;  /* streamed: block cache slots (0 without a cache) */
	uint8_t bpp, cell_w, cell_h, ascent, pitch;
	int8_t dy_min, dy_max; /* the dy range in force (0, 0 when ignored) */
	/* A valid JSET block (svf_is_jamo): lead classes, tail classes, syllable advance; version 2: 1 when the rows carry
	 * aliases (NA > 0), the bit widths of the initial, medial and final set fields of a cell (the set counts stay in the
	 * block header; the reader derives the row offsets from these at each lookup); version 1: jset_na 0, jset_w the
	 * counts NI, NM, NF. All 0 for a face without one. */
	uint8_t jset_lc, jset_tc, jset_adv, jset_na;
	uint8_t jset_w[3];
#if SVF_JMAP
	uint32_t jmap_off;   /* SVF_JMAP: the start of a usable version 3 block (JMAP) in data; 0 = none usable */
	uint16_t jmap_pi, jmap_pm; /* its initial and medial piece counts (the final plane's count is not read at lookup) */
	uint8_t jmap_flags;  /* its flags byte: 1 4B entries, 2 SPARSE, 4 FALLBACK, 8 LIST */
#endif
	uint8_t block_shift; /* streamed: log2 of the cache block size */
	uint8_t layout;      /* bits 0-2 REC, 3-5 ROW, bit 6 u32 offsets */
} svf_face;

/* Opens a face held whole at @p data (len bytes; RAM, flash or PROGMEM). Validates the header, the layout bytes against
 * the build's SVF_LAYOUTS, every table's place in the file and its alignment, the glyph offset table, every glyph
 * record (its box inside the cell, its rows inside its length), every glyph index of the code point table and the JSET
 * block (a bad block leaves a plain face with SVF_FACE_JSET_BAD, not an error). A @p len above 0xFFFFFFFF is refused (SVF_E_RANGE): offsets
 * are 32-bit. The face reads @p data in place for as long as it is used; the bytes must not change while it does: a
 * glyph's record is drawn without its checks being made again. */
svf_err svf_open_mapped(svf_face *f, const uint8_t *data, size_t len);

/* The layouts this build reads (SVF_LAYOUTS): returns how many codes, with @p set (which may be NULL) pointing at them;
 * 0 and @p set NULL when the build reads every valid layout. */
size_t svf_layouts(const uint16_t **set);

/* The face's header values; 0 for a NULL face (and for a zeroed one). */
uint8_t svf_bpp(const svf_face *f);
uint8_t svf_cell_w(const svf_face *f);
uint8_t svf_cell_h(const svf_face *f);
uint8_t svf_ascent(const svf_face *f);
uint16_t svf_flags(const svf_face *f);
uint16_t svf_status(const svf_face *f);
uint16_t svf_glyph_count(const svf_face *f);

/* The code point of code point table entry @p i (file order), or 0xFFFFFFFF if @p i >= svf_glyph_count or @p f is
 * NULL. */
uint32_t svf_cmap_cp(const svf_face *f, uint16_t i);

/* Non-zero if the face draws @p cp: a glyph in its code point table, or, with a valid JSET, any U+AC00..U+D7A3 and a
 * compatibility jamo U+3131..U+3163 whose compat piece is not blank (a JMAP-only face: a syllable its table lists, or a
 * compat jamo whose JMAP compat entry is not blank). 0 for a NULL face. */
int svf_has(const svf_face *f, uint32_t cp);

/* The metrics of @p cp. Returns 1 from the metrics table, 0 if the face has none (then m = {cell_w, 0, cell_w, 0}),
 * -1 if the face has no glyph for @p cp or @p f or @p m is NULL (m untouched). A composed code point (see svf_glyph)
 * returns 1 with {JSET advance or cell_w when it is 0, 0, cell_w, 0}. */
int svf_metrics_of(const svf_face *f, uint32_t cp, svf_metrics *m);

/* Bytes of one stored cell: ((cell_w * bpp + 7) / 8) * cell_h; 0 for a NULL face. */
size_t svf_cell_bytes(const svf_face *f);

/* Copies the stored rows of @p cp into @p cell (MSB first, each row padded to a byte, the bits past the cell width
 * zero). With a valid JSET, U+AC00..U+D7A3 and the compat jamo with a piece are composed (svf_compose) instead of
 * read from the code point table. SVF_E_ARG for a NULL face, or a NULL @p cell with cell_len > 0 (checked first);
 * SVF_E_RANGE if the face has no glyph for @p cp, SVF_E_NOMEM if
 * cell_len < svf_cell_bytes (for a composed code point, see svf_compose), SVF_E_IO if a streamed face's read
 * fails (the svf_cell_bytes bytes of @p cell are then zero). */
svf_err svf_glyph(const svf_face *f, uint32_t cp, uint8_t *cell, size_t cell_len);

/* -DSVF_NO_STREAM: a mapped-only build (the Uno, the M0+ P1/P2 profiles). svf_stream.c compiles to nothing and the
 * three stream functions below are not declared, on purpose: a call to one of them in such a build is a configuration
 * error, which the compiler reports (an undeclared function under -Werror, or the link) rather than a run-time
 * failure. Build without the switch to open streamed faces. */
#ifndef SVF_NO_STREAM
/* Opens a face read through @p rd: the header and the tables are read once, here, into @p tables (tables_len bytes,
 * at least svf_stream_tables_size; SVF_E_NOMEM otherwise), and validated exactly as svf_open_mapped validates a file of
 * @p file_len bytes, every range against @p file_len before it is read, except the glyph records: a version 3 record
 * is checked when it is read (SVF_E_RANGE, the cell left blank). Glyph rows are read when drawn: without a cache
 * (@p cache_len 0) one call a glyph or jamo piece whose record fits the cell, else two (its header, then its rows),
 * and one a row only where the rows do not fit the cell (row unit above 1 byte); with a cache, whole @p block_size
 * blocks (aligned in the file, a power of two in 64..4096) kept in @p cache, least recently used replaced. @p cache_len counts
 * the cache's own tags: a slot is block_size + 8 bytes and the cache must hold at least 2 (SVF_E_ARG otherwise; at most
 * 65535 are used). A read that fails or comes back short returns SVF_E_IO for the glyph being drawn and leaves no
 * cache slot holding its bytes. The face uses @p rd, @p ctx, @p tables and @p cache for as long as it is used, and one
 * face (its tables and cache) is not used from two threads at once.
 *
 * On any error the face is left all zero (no pointer to @p tables, @p cache or @p ctx kept).
 *
 * Composing a syllable of a streamed face without a cache reads each piece into the second half of the caller's cell:
 * svf_compose (and svf_glyph for a composed code point) needs cell_len >= 2 * svf_cell_bytes then (SVF_E_NOMEM
 * otherwise). */
svf_err svf_open_stream(svf_face *f, svf_read_fn rd, void *ctx, uint32_t file_len, uint8_t *tables, size_t tables_len,
                        uint8_t *cache, size_t cache_len, uint16_t block_size);

/* The tables buffer svf_open_stream needs for this file: 8 bytes of stream state, the code point table, a usable
 * metrics table, the JSET region and (version 3) the glyph offset table, (glyphs + 1) * 2 or 4 bytes (the JSET
 * region is only its 16-byte header where it is longer than the build's readers take, so the size depends on the
 * reader build); SVF_E_NOMEM where that does not fit a size_t. A streamed open validates the offset table; a glyph
 * record is checked when it is read (SVF_E_RANGE, the cell left blank). Reads and validates the header (one call of
 * @p rd); 0 with @p err set to the open error when the header is refused (@p err SVF_OK otherwise; @p err may be NULL); 0 and SVF_E_ARG for a NULL
 * @p rd. */
size_t svf_stream_tables_size(svf_read_fn rd, void *ctx, uint32_t file_len, svf_err *err);

/* Calls made to the read callback of a streamed face since it was opened, open included (0 for a mapped face or a NULL
 * @p f). */
uint32_t svf_stream_reads(const svf_face *f);
#endif /* !SVF_NO_STREAM */

/* Non-zero if the face composes syllables: a usable JSET part (none of the JSET and JMAP status bits kept it from
 * being read); 0 for a NULL face or a plain one. */
int svf_is_jamo(const svf_face *f);

/* The piece numbers of @p cp in draw order, as jamoset.pieces_of: for U+AC00..U+D7A3 initial, medial and (with a tail)
 * final, SVF_PIECE_BLANK included; returns 2 or 3 (a JMAP entry decides the count: a syllable with a tail may return 2,
 * and one without 3). For U+3131..U+3163 with a compat piece that is not blank, the piece; returns 1. Returns 0 (out
 * untouched) for anything else, a face without a valid JSET or JMAP, or a NULL @p f or @p out, and for a face with
 * only a JMAP a syllable the JMAP does not list (svf_is_jamo is 1 there: the syllable is not composed). */
int svf_jamo_pieces(const svf_face *f, uint32_t cp, uint16_t out[3]);

/* Composes @p cp (a syllable, or a compat jamo with a piece) into @p cell: the maximum coverage of its pieces at each
 * pixel (OR at 1 bpp, per 2-bit field at 2 bpp, per byte at 8 bpp); blank pieces draw nothing, so a syllable of blank
 * pieces is an empty cell. SVF_E_ARG for a NULL face, or a NULL @p cell with cell_len > 0 (checked first);
 * SVF_E_RANGE if svf_jamo_pieces gives 0, SVF_E_NOMEM if cell_len < svf_cell_bytes, or
 * < 2 * svf_cell_bytes for a streamed face without a cache (the second half holds the piece being read); SVF_E_IO if a
 * streamed face's read fails (the svf_cell_bytes bytes of @p cell are then zero, never a partial composition). */
svf_err svf_compose(const svf_face *f, uint32_t cp, uint8_t *cell, size_t cell_len);

/* ---- line drawing ---------------------------------------------------------------------------------------------- */

/* Non-zero if @p cp is a combining mark: advances 0 and is drawn over the glyph before it. */
typedef int (*svf_is_mark_fn)(uint32_t cp);

/* The default mark predicate: U+0300..U+036F (the core carries no Unicode table; pass your own for more). */
int svf_is_mark_default(uint32_t cp);

/* A chain of faces: each code point is drawn by the first face that has it (svf_has). @p is_mark NULL means
 * svf_is_mark_default. "A chain without faces" below is one whose faces is NULL, count 0 or any of its first count
 * faces NULL. A code point no face has draws nothing and advances @p missing_advance pixels (scaled as every
 * advance is).
 *
 * Scaling: the line is drawn at scale_num / scale_den (0/0, a zeroed chain, is 1/1; otherwise each must be 1..16,
 * SVF_E_ARG else; the ratio is reduced by their gcd, so 2/2 is 1/1). tools/svfline.py ("Scaling") is the reference.
 * - Lengths: S(v) = (v * num + den / 2) / den for v >= 0 and -S(-v) for v < 0 (round half up, symmetric). The pen
 *   moves by S(advance) for each advance (metrics, JSET, missing_advance), so it is in output pixels; a mark's originX
 *   is S(originX); a cell's top row is y_baseline - S(ascent) + S(dy), so the scaled cell's baseline row is its scaled
 *   ascent. x, y_baseline and the target's clip rectangle are output pixels and never scaled.
 * - Cells: a scaled cell is ceil(cell_w * num / den) x ceil(cell_h * num / den) pixels of 8-bit coverage. A whole
 *   multiple N (den 1) draws each source pixel as an N x N block. Any other ratio is area-averaged: output pixel (ox, oy)
 *   covers the source square [ox * den / num, (ox + 1) * den / num) on each axis, and its coverage is the area-weighted
 *   mean of the source coverage over that square (source outside the cell counts 0), in 16.16 fixed point rounded half
 *   up, which is the exact mean rounded half up. The scaled cell is then drawn as any cell (each kind's rules below,
 *   per cell: an RGB565 pixel two scaled cells cover takes the later one, SVF_T_COVERAGE_CB gets one call a pixel of
 *   each cell, row by row within the cell).
 * - Scratch: a scaled line needs, for its largest face, 2 * svf_cell_bytes + the scaled cell (svf_chain_scratch_bytes).
 * - Stack: the scaler's own frame is not scratch. With SVF_SCALE_FAST (the default) svf__scale_cell keeps its tables and
 *   accumulator on the stack: a 610 B frame on AVR (680 B on Cortex-M0+), so a scaled svf_draw_utf8 peaks at about 864 B
 *   of stack on AVR (1008 B on M0+; static worst case, +413 / +328 B over a build without it); an Uno (2048 B of RAM)
 *   that scales must budget that. -DSVF_SCALE_FAST=0 (or -DSVF_SMALL) is the direct loop: peak 451 B on AVR, 680 B on
 *   M0+ (the same as without scaling).
 * - Building with SVF_NO_SCALE (-DSVF_NO_SCALE for every source) leaves scaling out at compile time: svf_scale.c
 *   compiles to nothing, svf_draw.c refers to none of it, and any ratio but n/n is SVF_E_ARG. A link-time switch: a
 *   build that never scales but omits the define still links svf_scale.o (svf_draw.c calls it for ratios not 1/1). */
typedef struct {
	svf_face **faces;
	uint8_t count;
	svf_is_mark_fn is_mark;
	uint8_t missing_advance;
	uint8_t scale_num, scale_den; /* the line's scale (above); 0/0 is 1/1 */
	/* The composed-syllable cache (svf_chain_cache; private, all zero: none). A zeroed chain has no cache. */
	uint8_t *cache;
	uint16_t cache_slots, cache_cell;
} svf_chain;

/* Gives @p c a composed-syllable cache in @p buf (@p len bytes), or with @p len 0 none. svf_draw_utf8 then keeps the
 * cells it composes (svf_glyph of a syllable or compat jamo of a face with a valid JSET; never a cell read from the
 * code point table, never a failed one) and draws a cell it holds without composing it again, so a streamed face reads
 * nothing for it. Pixels are the same with or without the cache: it holds the stored (unscaled) cell, the line's
 * scale and target apply as usual. The buffer is len / (8 + the largest svf_cell_bytes of the chain's faces) slots
 * (at most 65535 are used): u32 code point, u8 face index (its place in the chain), 3 bytes, then the cell; the first
 * slot's last two of those 3 bytes hold the round-robin replacement cursor. Entries are keyed by (face index, code
 * point) and replaced round robin; a lookup scans the slots in order (linear in their number: fine for the tens of
 * slots a small target holds, slow for thousands). Every slot is emptied here: call it again after changing c->faces or
 * reopening a face of the chain. The chain uses @p buf until the next call; one chain (its cache) is not drawn with
 * from two threads at once. SVF_E_ARG for a NULL chain, a chain without faces, or a NULL @p buf with @p len > 0 (the
 * chain then has no cache); SVF_E_NOMEM (no cache) if @p len > 0 holds no slot. */
svf_err svf_chain_cache(svf_chain *c, uint8_t *buf, size_t len);

typedef enum {
	SVF_T_GRAY8, SVF_T_MONO_PAGE, SVF_T_MONO_ROW, SVF_T_RGB565, SVF_T_COVERAGE_CB, /* 0..4 */
	SVF_T_INDEXED8, /* 5: one byte a pixel, a palette index from a coverage ramp or a threshold */
	SVF_T_RGB555,   /* 6: two bytes a pixel, xRRRRRGGGGGBBBBB */
	SVF_T_XRGB8888, /* 7: four bytes a pixel, memory order B G R X */
	SVF_T_RGB888,   /* 8: three bytes a pixel, memory order B G R */
	SVF_T_GRAY      /* 9: gray_bits (1, 2, 4 or 8) bits a pixel, packed in rows; the first pixel of a byte in its most
	                 * significant bits, or its least with SVF_TF_GRAY_LSB_FIRST; a level 0..2^bits-1 a pixel, the
	                 * GRAY8 max rule on levels */
} svf_target_kind;

#define SVF_TF_565_BIGENDIAN 0x01u                  /* RGB565 and RGB555: high byte first */
#define SVF_TF_RGB16_BIGENDIAN SVF_TF_565_BIGENDIAN /* the name that says it applies to both; the same bit */
#define SVF_TF_GRAY_LSB_FIRST 0x02u                 /* SVF_T_GRAY: the first pixel of a byte in its low bits (bit 0x01 stays RGB16's) */

/* The target kinds a build compiles: -DSVF_TARGETS=<OR of these> for every source (default: all). A kind left out is
 * not in the object code at all (the point: a Cortex-M0+ or Arduino build that draws on one kind carries one blit),
 * and svf_target_init and svf_draw_utf8 refuse it with SVF_E_ARG. Bit i is svf_target_kind value i. */
#define SVF_TARGET_GRAY8 0x001u
#define SVF_TARGET_MONO_PAGE 0x002u
#define SVF_TARGET_MONO_ROW 0x004u
#define SVF_TARGET_RGB565 0x008u
#define SVF_TARGET_CB 0x010u
#define SVF_TARGET_INDEXED8 0x020u
#define SVF_TARGET_RGB555 0x040u
#define SVF_TARGET_XRGB8888 0x080u
#define SVF_TARGET_RGB888 0x100u
#define SVF_TARGET_GRAY 0x200u
#ifndef SVF_TARGETS
#define SVF_TARGETS 0x3FFu
#endif

/* Word stores: 1 where a 4-byte memcpy at any alignment is one store (x86, WASM), else 0; -DSVF_WORD_STORE=0/1 for
 * every source overrides. Pixels are the same either way. */
#ifndef SVF_WORD_STORE
#if defined(__i386__) || defined(__x86_64__) || defined(__wasm__)
#define SVF_WORD_STORE 1
#else
#define SVF_WORD_STORE 0
#endif
#endif

/* Size switches (every one a -D for every source; a switch left out of a build is not compiled at all):
 *   SVF_SMALL        umbrella: the defaults of SVF_MERGE_WORDS, SVF_GRAY8_LUT1, SVF_SCALE_FAST and SVF_JSET_POP4
 *                    become 0 (an explicit -DSVF_X=1 still wins over it). Pixels are the same; speed is lost for flash.
 *   SVF_MERGE_WORDS  0: the word-at-a-time merge of jamo piece rows (svf_jamo.c) is left out, byte loops only.
 *   SVF_GRAY8_LUT1   0: the 64 B nibble table and its GRAY8 1 bpp blit are left out, the bit loop alone.
 *   SVF_SCALE_FAST   0: the separable, periodic-weight area average of svf__scale_cell (a 610..680 B stack frame, the
 *                    weight tables and block accumulator) is replaced by the direct 2-D loop (small frame, slower).
 *   SVF_NO_SCALE     scaling is left out entirely (svf_scale.c compiles to nothing; ratios but n/n are SVF_E_ARG).
 *   SVF_NO_STREAM    streamed faces are left out (svf_stream.c compiles to nothing; mapped faces only).
 *   SVF_TARGETS      OR of the SVF_TARGET kinds to compile (default all); the kinds left out are refused.
 *   SVF_LAYOUTS      a comma list of the SVF_LAYOUT codes the build reads (default every valid layout).
 *   SVF_JSET_V1      0: the JSET block version 1 reader is left out (a version 1 face opens plain, SVF_FACE_JSET_UNREAD).
 *   SVF_JSET_V2      0: the JSET block version 2 (compact) reader is left out (a version 2 face opens plain).
 *                    SVF_JSET_V1 defaults to 1; SVF_JSET_V2 to 1, except on AVR (__AVR__), where it defaults to 0 (v1
 *                    only: both readers do not fit an ATmega328P beside the rest). At least one must be 1. A small MCU
 *                    build picks the version its faces carry (tools write v1 by default, v2 only when requested:
 *                    --jset-version).
 *   SVF_JMAP         1: the version 3 JSET block (JMAP: a direct map from a syllable to its pieces, spec/SVF.md "Version 3")
 *                    is read: validated at open (R1-R8, JMAP_BAD on failure), looked up by svf_jamo_pieces, and its
 *                    fallback JSET (version 1 or 2, when that reader is compiled) answers the syllables it does not list.
 *                    svf_face grows (+9 B on AVR, +12 on Cortex-M, 64 -> 76 on -m32, 80 -> 96 on a 64-bit host: the 96 B
 *                    cap): SVF_JMAP is the one switch that changes svf_face's layout (SVF_SMALL, which defaults it, with
 *                    it), so every translation unit that includes svf.h and the library must be built with the same
 *                    SVF_JMAP. 0: version 3 is not read (SVF_JMAP_RECOG below).
 *                    Defaults to 1 only off AVR, without SVF_SMALL, on __unix__ _WIN32 __APPLE__ __DJGPP__ __EMSCRIPTEN__ (desktop, DOS, WASM); 0 on every MCU; an explicit -DSVF_JMAP=0/1 wins.
 *                    The KS-only build is -DSVF_JMAP=1 -DSVF_JSET_V1=0 -DSVF_JSET_V2=0: a fallback is then JSET_UNREAD.
 *   SVF_JMAP_LIST    1 (with SVF_JMAP): the LIST form of the version 3 block (flag 0x08: up to 255 exceptions, a sorted list of
 *                    syllable -> three piece numbers over the pool shared with the fallback JSET) is read: rules R1', L2, L3, R4 at open,
 *                    L5-L7 too unless SVF_JSET_TRUSTED, binary search at lookup; a miss goes to the fallback. 0: a LIST block
 *                    is not read: its header rules (R1', L2, L3, R4) are applied, a block that fails them is JMAP_BAD, one that passes
 *                    is JMAP_UNREAD with its fallback (if any) read in its place, a plain face when it has none (the content rules L5-L7 are not applied). Defaults to the value of SVF_JMAP (on where SVF_JMAP is: host, DOS, WASM, SDL; 0 on every MCU unless SVF_JMAP=1 is asked for, then LIST follows); an explicit -DSVF_JMAP_LIST=0/1 wins. 1 without SVF_JMAP is an #error.
 *   SVF_JMAP_RECOG   1: a build without SVF_JMAP recognises a version 3 block (JMAP): SVF_FACE_JMAP_UNREAD, and with its
 *                    FALLBACK flag the fallback JSET (version 1 or 2) is read in its place (about +280 B AVR, +164 B
 *                    Cortex-M0+); a LIST block (flag 0x08) is recognised the same way (header bytes 6-9 only). 0: a version 3 block is an unknown version (SVF_FACE_JSET_BAD, plain). Defaults to 1
 *                    only off AVR, without SVF_SMALL, on __unix__ _WIN32 __APPLE__ __DJGPP__ __EMSCRIPTEN__; an
 *                    explicit -DSVF_JMAP_RECOG=0/1 wins. Compiled out when SVF_JMAP is 1: where SVF_JMAP defaults to 1
 *                    (the same targets) the recognition is active only with an explicit -DSVF_JMAP=0.
 *   SVF_JSET_BIG     1: JSET blocks of versions 1 and 2 may have up to 19 lead x 28 tail classes and 128 / 128 / 64
 *                    initial / medial / final sets (the narrow limits are 12 x 12 and 32 / 24 / 16; a block over the
 *                    limits of the build is SVF_FACE_JSET_BAD). Version 2 compat bytes then carry the set in bits 0-6
 *                    (bit 7 the final switch, 0xFF blank), the narrow bit 5-6 test is dropped. The largest blocks are
 *                    47,373 B (version 1) and 45,068 B (version 2, every slot an alias). The 16-bit sites (the set
 *                    table's bit offsets and length, the class bit sets) are 32 bits wide under the switch only; the
 *                    narrow build's code is unchanged and svf_face does not change. Code: AVR about +26 B (version 1
 *                    reader), +72 B (both readers); Cortex-M0+ svf_jamo.o no larger. Defaults to 1 only off AVR, without SVF_SMALL, on __unix__ _WIN32 __APPLE__ __DJGPP__ __EMSCRIPTEN__ (the SVF_JMAP list; independent of it); 0 on every MCU; an explicit -DSVF_JSET_BIG=0/1 wins.
 *   SVF_JSET_POP4    0: the version 2 reader counts bits one set bit a step instead of with a 16-entry nibble table
 *                    (about 28 B on Cortex-M0+, 24 B with SVF_JSET_V1=0; about 90 fewer instructions a 3-piece
 *                    syllable). Defaults to 1, except 0 on AVR and under SVF_SMALL; an explicit -DSVF_JSET_POP4=1 wins.
 *   SVF_JSET_TRUSTED 1: the JSET table contents are not validated (default 0: every JSET is validated at open). The
 *                    open check then keeps only the header length, the magic, the version and the block size the counts
 *                    give against the header's region size (the version 1 reader also keeps its count limits; the
 *                    version 2 reader does not: its counts are trusted too). A streamed face still loads no more than
 *                    the region. WARNING: only for faces the developer builds into flash; a hostile or corrupt table
 *                    (counts, classes, sets, pieces) reads out of bounds, and in a 16-bit build (AVR) with SVF_JSET_V2
 *                    and TRUSTED the size product of a corrupt count can wrap. With SVF_JMAP a version 3 block keeps only
 *                    R1-R4 (header, counts, NS, region size and fallback header); its rank table, bitmap, entries, compat
 *                    entries and the glyph of every piece (R5-R8; of a LIST block L5-L7: compat entries, key order, fields) are trusted, and a corrupt rank or bitmap drives an
 *                    entry read up to about 264 KB outside the block. Never for faces from a file, a network
 *                    or a user (the fuzz harnesses refuse to compile with it).
 * Task 9's code point table search has no switch (it is smaller than the code it replaced). */
#ifndef SVF_JSET_V1
#define SVF_JSET_V1 1
#endif
#ifndef SVF_JSET_V2
#if defined(__AVR__)
#define SVF_JSET_V2 0
#else
#define SVF_JSET_V2 1
#endif
#endif
#ifndef SVF_JSET_TRUSTED
#define SVF_JSET_TRUSTED 0
#endif
#ifndef SVF_JMAP_RECOG
#if !defined(__AVR__) && !defined(SVF_SMALL) && \
    (defined(__unix__) || defined(_WIN32) || defined(__APPLE__) || defined(__DJGPP__) || defined(__EMSCRIPTEN__))
#define SVF_JMAP_RECOG 1
#else
#define SVF_JMAP_RECOG 0
#endif
#endif
#ifndef SVF_JMAP_LIST
#if SVF_JMAP
#define SVF_JMAP_LIST 1	/* follows SVF_JMAP (a literal, so -dM shows it); an explicit -DSVF_JMAP_LIST=0/1 wins */
#else
#define SVF_JMAP_LIST 0
#endif
#endif
#if SVF_JMAP_LIST && !SVF_JMAP
#error "SVF_JMAP_LIST needs SVF_JMAP"
#endif
#ifndef SVF_JSET_BIG
#if !defined(__AVR__) && !defined(SVF_SMALL) && \
    (defined(__unix__) || defined(_WIN32) || defined(__APPLE__) || defined(__DJGPP__) || defined(__EMSCRIPTEN__))
#define SVF_JSET_BIG 1
#else
#define SVF_JSET_BIG 0
#endif
#endif
#if !SVF_JSET_V1 && !SVF_JSET_V2 && !SVF_JMAP
#error "SVF_JSET_V1, SVF_JSET_V2 and SVF_JMAP are all 0: at least one reader is needed"
#endif

#ifdef SVF_SMALL
#define SVF__FAST_DEFAULT 0
#else
#define SVF__FAST_DEFAULT 1
#endif

/* JSET popcount table (SVF_JSET_POP4 above): 0 on AVR (the table would cost flash and RAM, and AVR builds read v1 by
 * default) and under SVF_SMALL; -DSVF_JSET_POP4=0/1 for every source overrides. Pixels are the same either way. */
#ifndef SVF_JSET_POP4
#if defined(__AVR__)
#define SVF_JSET_POP4 0
#else
#define SVF_JSET_POP4 SVF__FAST_DEFAULT
#endif
#endif

/* Merge words: 1 where svf__merge may merge 1 and 2 bpp jamo piece rows four bytes at a time (every address on x86,
 * WASM and Arm cores with unaligned access, elsewhere where the piece row and the cell row line up mod 4), 0 for the
 * byte loops alone (AVR: 8-bit registers, the word costs four); -DSVF_MERGE_WORDS=0/1 for every source overrides.
 * On a strict-align core the word path is compiled out with a non-GNU compiler, and strict-align builds use words
 * only for runs of 8+ bytes. Pixels are the same either way. */
#ifndef SVF_MERGE_WORDS
#if defined(__AVR__)
#define SVF_MERGE_WORDS 0
#else
#define SVF_MERGE_WORDS SVF__FAST_DEFAULT
#endif
#endif

/* GRAY8 table: 1 where the GRAY8 blit of a 1 bpp cell writes each source byte as two 16-row nibble lookups (a 64 B table,
 * four pixels each); 0 for the bit loop alone (AVR: the table would cost flash; Xtensa: slower). -DSVF_GRAY8_LUT1=0/1
 * for every source overrides. Pixels are the same either way. */
#ifndef SVF_GRAY8_LUT1
#if defined(__AVR__) || defined(__XTENSA__) /* Xtensa: ESP32-S3 instruction counts +5..+33 % on GRAY8 1 bpp with it */
#define SVF_GRAY8_LUT1 0
#else
#define SVF_GRAY8_LUT1 SVF__FAST_DEFAULT
#endif
#endif

/* Fast scaling: 1 for the separable area average of scaled cells (svf_scale.c: periodic per-axis weights, a block
 * accumulator; about 400 B more stack than the direct loop and more flash), 0 for the direct 2-D loop. -DSVF_SCALE_FAST=0/1
 * for every source overrides. Pixels are the same either way. */
#ifndef SVF_SCALE_FAST
#define SVF_SCALE_FAST SVF__FAST_DEFAULT
#endif

/* A drawing target. A drawn cell sends each of its pixels with coverage > 0 (1 bpp ink 255, 2 bpp level k * 85, 8 bpp
 * the byte) inside the clip rectangle to the target, row by row, cells in line order; each kind converts that
 * coverage per pixel and touches nothing else (no pixel outside the clip, no byte of another pixel, no padding byte
 * of a row past its pixels). All arithmetic is unsigned and fits 16 bits where the target is built for a 16-bit int
 * (AVR), except where a uint32_t is named:
 * - SVF_T_GRAY8: one byte a pixel at buf[y * stride + x]; it becomes max(existing, coverage).
 * - SVF_T_MONO_PAGE (SSD1306 pages): bit y & 7 of buf[(y >> 3) * stride + x] is set when coverage >= mono_threshold
 *   (0 counts as 1: a pixel without coverage is never set); bits are only ever set, never cleared.
 * - SVF_T_MONO_ROW (1 bpp rows, MSB first): bit 7 - (x & 7) of buf[y * stride + (x >> 3)], the same rule.
 * - SVF_T_RGB565: the two bytes at buf[y * stride + 2 * x] (low byte first; high byte first with
 *   SVF_TF_RGB16_BIGENDIAN) become bg565 and fg565 blended by coverage per 5/6/5 channel,
 *   (c_bg * (255 - cov) + c_fg * cov + 127) / 255.
 *   The existing pixel is not read: bg565 is the colour the caller's background is known to be. A pixel two cells of
 *   one line cover (a mark over its base, overlapping cells) takes the later cell's blend.
 * - SVF_T_COVERAGE_CB: plot(ctx, x, y, coverage) a pixel (no buffer: buf, buf_len and stride are not used); a pixel
 *   two cells cover is plotted once per cell. plot must be set before drawing (SVF_E_ARG otherwise).
 * - SVF_T_INDEXED8: one byte a pixel at buf[y * stride + x]. With n = index_levels and b = index_base the pixel's
 *   level is L = (cov * n + 127) / 255 for n >= 2 (n = 255 gives L = cov, a 2 bpp level k with n = 3 gives k); for
 *   n = 1 it is 1 when cov >= mono_threshold (0 counting as 1), else 0, as MONO_ROW. L = 0 leaves the pixel
 *   untouched; otherwise it becomes max(existing, b + L), the GRAY8 rule on indices. The defaults (b 0, n 255) draw
 *   the GRAY8 bytes. Consequence of max: text shows only where the existing index is below the ink's, so the
 *   background index (and any colour the text must cover) has to be below the ramp b + 1 .. b + n.
 * - SVF_T_RGB555: the two bytes at buf[y * stride + 2 * x] (low byte first; high byte first with
 *   SVF_TF_RGB16_BIGENDIAN) are (fg565 & 0x8000) | the blend of fg565 & 0x7FFF and bg565 & 0x7FFF by coverage per
 *   5/5/5 channel, the RGB565 arithmetic on three 5-bit channels. Bit 15 of the pixel is bit 15 of fg565 whatever the
 *   coverage (the default fg565 0xFFFF sets it; pass 0x7FFF to clear it). The existing pixel is not read; the later
 *   cell wins, as RGB565.
 * - SVF_T_XRGB8888: the four bytes at buf[y * stride + 4 * x], in memory B, G, R, X (the little-endian dword
 *   X RR GG BB of an x86 VBE 32 bpp mode). B, G and R blend bg888 and fg888 per 8-bit channel,
 *   (c_bg * (255 - cov) + c_fg * cov + 127) / 255 computed in uint32_t; X is bits 31..24 of fg888. Later cell wins.
 * - SVF_T_RGB888: the three bytes at buf[y * stride + 3 * x], in memory B, G, R, the same blend. Later cell wins.
 * - SVF_T_GRAY: gray_bits (1, 2, 4 or 8) bits a pixel, packed in rows, the pixel x in bits [x * gray_bits, (x + 1) *
 *   gray_bits) of the row counted from the row's first byte: the first pixel of a byte in its most significant bits,
 *   or in its least with SVF_TF_GRAY_LSB_FIRST (no effect at bits 8). With n = 2^gray_bits - 1 the pixel's level is
 *   L = (cov * n + 127) / 255 for gray_bits >= 2 (the INDEXED8 ramp rule with n the top level; n = 255 gives L = cov);
 *   for gray_bits 1 it is 1 when cov >= mono_threshold (0 counting as 1), else 0, as MONO_ROW. L = 0 leaves the pixel;
 *   otherwise the pixel's field becomes max(field, L), the GRAY8 rule on levels. mono_threshold is read at gray_bits
 *   1 only. Bits 8 writes the same bytes as SVF_T_GRAY8; bits 1, MSB first, the same bytes as SVF_T_MONO_ROW at the same
 *   threshold. The padding bits of a row's last byte and the bytes past ceil(w * gray_bits / 8) are never written.
 * Rows: the least stride is GRAY8 w, MONO_PAGE w, MONO_ROW ceil(w/8), RGB565 2w, INDEXED8 w, RGB555 2w, XRGB8888 4w,
 * RGB888 3w, GRAY ceil(w * gray_bits / 8) (svf_target_init checks ceil(w / 8): the caller sets gray_bits after
 * it, and every draw re-checks). stride is a uint16_t, so an XRGB8888 target is at most 16383 pixels wide and an RGB888 one 21845.
 * Coordinates: x right, y down, (0, 0) the top-left pixel; all addressing is in size_t. */
typedef struct svf_target {
	svf_target_kind kind;
	uint8_t *buf;
	size_t buf_len;
	int16_t w, h;
	uint16_t stride;                            /* bytes from one row (SSD1306: one 8-row page) to the next */
	int16_t clip_x0, clip_y0, clip_x1, clip_y1; /* half-open, any values (drawing intersects it with the target);
	                                            * svf_target_init sets the whole target */
	uint16_t fg565, bg565; /* RGB565: 5/6/5 values; RGB555: 5/5/5 values, bit 15 of fg565 copied to the pixel (the
	                        * names are historical) */
	uint8_t mono_threshold; /* MONO_PAGE, MONO_ROW, INDEXED8 with index_levels 1, and GRAY with gray_bits 1 */
	uint8_t flags;          /* SVF_TF_RGB16_BIGENDIAN (RGB565, RGB555), SVF_TF_GRAY_LSB_FIRST (GRAY) */
	void (*plot)(void *ctx, int16_t x, int16_t y, uint8_t cov);
	void *ctx;
	uint32_t fg888, bg888; /* XRGB8888, RGB888: 0x00RRGGBB; bits 31..24 of fg888 are XRGB8888's X byte */
	uint8_t index_base;    /* INDEXED8: the ramp's level 0 (the background level) */
	uint8_t index_levels;  /* INDEXED8: ink levels above it, 1..255; index_base + index_levels <= 255 */
	uint8_t gray_bits;     /* GRAY: 1, 2, 4 or 8 (svf_target_init sets 8); checked on every draw, SVF_E_ARG else */
} svf_target;

/* Fills @p t: the clip rectangle is the whole target, fg565 0xFFFF, bg565 0, mono_threshold 128, flags 0, plot and ctx
 * NULL, and, in a build that compiles XRGB8888 or RGB888, fg888 0x00FFFFFF, bg888 0; in a build that compiles INDEXED8,
 * index_base 0, index_levels 255, and in a build that compiles GRAY, gray_bits 8 (a build without the kind leaves those
 * fields 0: it cannot draw the kind).
 * SVF_E_ARG (and @p t all zero) unless w, h > 0, the
 * kind is known and compiled in, and, for every kind but SVF_T_COVERAGE_CB (which needs no buffer), buf is not NULL,
 * stride is at least the bytes of one row of the kind (above; GRAY: ceil(w / 8), the least any gray_bits needs, since the
 * caller sets gray_bits after the call) and buf_len covers rows * stride (rows: h, or ceil(h/8)
 * pages for MONO_PAGE). The fields set after the call (index_base, index_levels, ...) are checked on every draw:
 * INDEXED8 needs index_levels >= 1 and index_base + index_levels <= 255, GRAY needs gray_bits in {1, 2, 4, 8} and stride >=
 * ceil(w * gray_bits / 8), else svf_draw_utf8 returns SVF_E_ARG. */
svf_err svf_target_init(svf_target *t, svf_target_kind k, uint8_t *buf, size_t buf_len, int16_t w, int16_t h,
                        uint16_t stride);

/* A line's extent: the pen's travel (saturated at 32767), and the most rows a drawn cell reaches above and below the
 * baseline (ascent - dy and cell_h - ascent + dy, so a glyph's dy is included); all 0 for a line that draws no cell. */
typedef struct {
	int16_t width, ascent, descent;
} svf_extent;

/* The scratch svf_draw_utf8 needs for @p c, the most any face of the chain needs: at 1/1 its cell (svf_cell_bytes),
 * twice it for a streamed face without a block cache (svf_compose reads each piece into the second half); scaled,
 * 2 * svf_cell_bytes + ceil(cell_w * num / den) * ceil(cell_h * num / den) (the scaled GRAY8 cell after the stored
 * one). Computed in 32 bits (at most 2 * 65025 + 4080 * 4080), SIZE_MAX where size_t is narrower than the need (no
 * buffer is then enough: SVF_E_NOMEM). 0 for a NULL chain, a chain without faces or one with a refused ratio.
 */
size_t svf_chain_scratch_bytes(const svf_chain *c);

/* The extent of @p len bytes of UTF-8 at @p s laid out on @p c (the rules of svf_draw_utf8; no glyph is read), in
 * output pixels at the chain's scale (ascent and descent from each cell's scaled top row and height). SVF_E_ARG for a
 * NULL chain, extent or (with len > 0) text, a chain without faces, or a refused ratio. */
svf_err svf_measure_utf8(const svf_chain *c, const char *s, size_t len, svf_extent *e);

/* Draws @p len bytes of UTF-8 at @p s (never read past len; NUL is a code point) on @p t, the pen starting at @p x on
 * the baseline @p y_baseline (the row below the ascent: a cell's top row is y_baseline - ascent + dy).
 * - Decoding: each maximal invalid subpart (a bad byte, an overlong form, a surrogate, a code point past U+10FFFF, a
 *   truncated sequence) is U+FFFD when a face of the chain has it, else skipped without advancing.
 * - Each code point is drawn by the first face that has it, its cell's left column at the pen, and the pen advances by
 *   the metrics advance (svf_metrics_of: a composed syllable the JSET advance or cell width; no metrics table the cell
 *   width).
 * - A mark (c->is_mark) advances 0 and is drawn at anchorX - originX: anchorX is the pen after the previous non-mark
 *   glyph, which is the current pen (also for a mark first in the line), as in the engine; originX is
 *   min(-bearing_x, cell_w) when its face has flags bit 2 and bearing_x < 0, else 0.
 * - Only pixels inside the clip rectangle, intersected with [0, w) x [0, h) on every call, are written; a cell wholly
 *   outside it is not read.
 * - Scaled (svf_chain scale_num / scale_den), every length above is scaled and each cell is the scaled cell (svf_chain).
 * - @p scratch holds one glyph: scratch_len must be at least svf_chain_scratch_bytes(c) (SVF_E_NOMEM otherwise, nothing
 *   drawn; a NULL @p scratch with scratch_len > 0 is SVF_E_ARG).
 * Returns SVF_E_ARG (nothing drawn) for a NULL chain or text (with len > 0), a chain without faces or with a refused
 * ratio, a target
 * svf_target_init would refuse (re-checked here, buf_len included), or an SVF_T_COVERAGE_CB target without plot. A
 * clip rectangle that is empty or wholly outside the target draws nothing (SVF_OK). A streamed face's failed read
 * (SVF_E_IO) stops drawing that glyph only: the line goes on, every glyph advancing as usual, and SVF_E_IO is returned
 * at the end; otherwise SVF_OK. tools/svfline.py is the reference. */
svf_err svf_draw_utf8(const svf_chain *c, const char *s, size_t len, int16_t x, int16_t y_baseline, svf_target *t,
                      uint8_t *scratch, size_t scratch_len);

#ifdef __cplusplus
}
#endif

#endif /* SVF_H */
