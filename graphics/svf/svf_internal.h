/* svf_internal.h - shared by the library's sources only (not installed).
 *
 * Face byte access: every read of a face's header and tables goes through rd8/rd16/rd32 at f->data, the mapped file
 * or a streamed face's tables buffer (offsets rebased into it), so both sources share one lookup path. A mapped face's
 * bytes are read with SVF_RD8/16LE/32LE (which a build may override, e.g. AVR PROGMEM); a streamed face's bytes are
 * all RAM (tables buffer, header buffer, cell scratch, cache blocks) and read with plain loads. Where SVF_RD8 is not
 * overridden the two are the same load and no branch is compiled; with an override the source is told by f->rd
 * (SVF__STREAMED, the constant 0 under SVF_NO_STREAM).
 * Glyph rows are the one place the sources differ otherwise: svf__expand puts a record's rows into the caller's cell,
 * reading a mapped face's in place and a streamed face's through svf__stream_expand.
 */
#ifndef SVF_INTERNAL_H
#define SVF_INTERNAL_H

#include "svf.h"

#include <stdint.h>

/* The bound every size_t overflow check of the open uses: SIZE_MAX, or -DSVF__SIZE_MAX=0xFFFF for the test build that
 * stands for a 16-bit size_t (AVR). */
#ifndef SVF__SIZE_MAX
#define SVF__SIZE_MAX SIZE_MAX
#endif

enum {
	SVF__HEADER_V3 = 48,
	SVF__CMAP_ENTRY = 8,
	SVF__METRICS_ENTRY = 4,
	SVF__JSET_HEADER = 16, /* the JSET block header (version 2): magic .. reserved; leadClass follows */
	/* the largest valid version 2 JSET block (version 1 blocks reach 12,321 bytes): 12 lead/tail classes, 32/24/16 sets,
	 * every slot an alias (SVF_JSET_BIG: below) */
#if !SVF_JSET_BIG
	SVF__JSET_MAX = 9246,
#endif
	SVF__STREAM_STATE = 8, /* a streamed face's tables buffer starts with u32 reads, u32 cache clock */
	SVF__LAYOUT_U32 = 0x40 /* svf_face.layout: glyph offset table entries are u32 bytes (else u16 in units of 2^REC) */
};

/* SVF_NO_STREAM: the build opens mapped faces only (svf_stream.c compiles to nothing, svf_open_stream, svf_stream_tables_size and
 * svf_stream_reads are not declared) and SVF__STREAMED(f) is the constant 0, so every streamed-face branch folds away (the byte
 * reads below included: with SVF_RD8 overridden, they test no source at run time). */
#ifdef SVF_NO_STREAM
#define SVF__STREAMED(f) (0)
#else
#define SVF__STREAMED(f) ((f)->rd != NULL)
#endif

/* SVF_JSET_BIG: the largest version 2 block (19 x 28 classes, 128/128/64 sets, every slot an alias) is 45,068 bytes, a
 * macro as it does not fit a 16-bit int. */
#if SVF_JSET_BIG
#define SVF__JSET_MAX 45068u
#endif

/* The longest JSET region a streamed face loads whole (svf_stream.c; a longer one only to its 16-byte header, refused):
 * the largest block the build reads: with SVF_JMAP the JMAP cap 65,534, else with SVF_JSET_V1 the largest version 1 block
 * (12,321 bytes: 12 x 12 classes, 32/24/16 sets; SVF_JSET_BIG: 47,373 bytes: 19 x 28 classes, 128/128/64 sets), else
 * SVF__JSET_MAX. */
#if SVF_JMAP
#define SVF__JSET_LOAD_MAX 65534u /* the JMAP region cap (PR-7); a region of 0xFFFF (the clip) is never loaded whole */
#elif SVF_JSET_V1 && SVF_JSET_BIG
#define SVF__JSET_LOAD_MAX 47373u
#elif SVF_JSET_V1
#define SVF__JSET_LOAD_MAX 12321u
#else
#define SVF__JSET_LOAD_MAX ((unsigned)SVF__JSET_MAX)
#endif

/* svf__merge's words at any address: 1 where a 4-byte memcpy at any alignment is one load or store (x86, WASM, Arm
 * with unaligned access), else 0: a row then merges words only where its piece and cell bytes are congruent mod 4,
 * after a byte head up to the aligned cell byte. By architecture, not SVF_WORD_STORE (the DOS _bytes build keeps its
 * merge); -DSVF__MERGE_UNALIGNED=0 runs the strict-align path on a host (test-kernels-bytes). */
#ifndef SVF__MERGE_UNALIGNED
#if defined(__i386__) || defined(__x86_64__) || defined(__wasm__) || defined(__ARM_FEATURE_UNALIGNED)
#define SVF__MERGE_UNALIGNED 1
#else
#define SVF__MERGE_UNALIGNED 0
#endif
#endif

/* The byte of face memory at @p p (see above: plain for a streamed face, SVF_RD8 for a mapped one). */
#ifdef SVF_RD8_PLAIN
#define SVF__RD8(f, p) SVF_RD8(p)
#else
#define SVF__RD8(f, p) (SVF__STREAMED(f) ? *(const uint8_t *)(p) : SVF_RD8(p))
#endif

static inline uint8_t rd8(const svf_face *f, uint32_t off)
{
	return SVF__RD8(f, f->data + off);
}

static inline uint16_t rd16(const svf_face *f, uint32_t off)
{
#ifndef SVF_RD8_PLAIN
	if (SVF__STREAMED(f))
		return (uint16_t)(f->data[off] | (uint16_t)f->data[off + 1] << 8);
#endif
	return SVF_RD16LE(f->data + off);
}

static inline uint32_t rd32(const svf_face *f, uint32_t off)
{
#ifndef SVF_RD8_PLAIN
	if (SVF__STREAMED(f))
		return (uint32_t)rd16(f, off) | (uint32_t)rd16(f, off + 2) << 16;
#endif
	return SVF_RD32LE(f->data + off);
}

/* The layout units of @p f, log2 (header byte 9 bits 0-2 REC and 3-5 ROW, byte 10 bits 0-2 TAB). With a one-code
 * SVF_LAYOUTS the compiler folds them to constants (svf__layouts has one element); otherwise REC and ROW come from
 * f->layout and TAB, which only the open needs, from header byte 10 (so SVF__TAB is for svf__open_header). */
#ifdef SVF_LAYOUTS
static const uint16_t svf__layouts[] = {SVF_LAYOUTS};
#define SVF__ONE_LAYOUT (sizeof svf__layouts / sizeof svf__layouts[0] == 1)
#else
#define SVF__ONE_LAYOUT 0
#endif

static inline unsigned svf__rec(const svf_face *f)
{
#ifdef SVF_LAYOUTS
	if (SVF__ONE_LAYOUT)
		return svf__layouts[0] & 7;
#endif
	return f->layout & 7;
}

static inline unsigned svf__row(const svf_face *f)
{
#ifdef SVF_LAYOUTS
	if (SVF__ONE_LAYOUT)
		return svf__layouts[0] >> 3 & 7;
#endif
	return f->layout >> 3 & 7;
}

static inline unsigned svf__tab(const svf_face *f)
{
#ifdef SVF_LAYOUTS
	if (SVF__ONE_LAYOUT)
		return svf__layouts[0] >> 8 & 7;
#endif
	return rd8(f, 10) & 7;
}

#define SVF__REC(f) svf__rec(f)
#define SVF__ROW(f) svf__row(f)
#define SVF__TAB(f) svf__tab(f)

/* svf_face.c: the two halves of open, shared by both sources. The caller zeroes @p f and points f->data at the file
 * (mapped) or at its first min(len, 48) bytes (streamed). open_header validates the header against a file of @p len
 * bytes (version, layout bytes, the build's supported set, bpp, box, every table's range and alignment) and sets the
 * table offsets (file offsets), the layout, the dy range, SVF_FACE_METRICS_DROPPED and the JSET region; it reads header
 * bytes only. open_tables then validates the code point table, the glyph offset table (against @p data_size, header
 * word 28; the table is at f->goff_off, loaded where the source needs it), for a mapped face every record, and the JSET
 * region where f's offsets name them. */
svf_err svf__open_header(svf_face *f, uint32_t len);
svf_err svf__open_tables(svf_face *f, uint32_t data_size);

/* A record's rows (spec "Glyph record"): the box, the offset of row 0 from the record start and the row pitch. */
typedef struct {
	uint8_t x0, y0, wb, h; /* h 0: blank */
	uint16_t p, rp;
} svf__rec_t;

/* svf_face.c: the file offset and byte length of the record of glyph @p g (from the offset table). SVF_E_RANGE for a
 * glyph at or past the count. */
svf_err svf__record(const svf_face *f, uint16_t g, uint32_t *at, uint32_t *len);

/* svf_face.c: reads the box from the 4 header bytes @p h (plain memory) of a record of @p len bytes and checks it: the
 * box inside the cell and, for a non-blank record, its rows (counting the last row's padding) inside @p len.
 * SVF_E_RANGE otherwise, also for a record shorter than its header (then @p h is not read). */
svf_err svf__rec_parse(const svf_face *f, const uint8_t *h, uint32_t len, svf__rec_t *r);

/* svf_face.c: puts the rows of a parsed, non-blank record @p r, row 0 at @p rows in memory read like face memory (a
 * mapped file, or RAM for a streamed face), into @p cell: copied, or merged (svf__merge). A full-width record is one
 * run of pitch * h bytes, any other one a run of wb bytes per row. A copy goes to a clear cell (svf_glyph clears it). */
void svf__put_rows(const svf_face *f, uint8_t *cell, const uint8_t *rows, const svf__rec_t *r, int merge);

/* svf_face.c: puts the rows of the record of glyph @p g into @p cell (svf_cell_bytes long, clear for a copy; nothing in
 * it is cleared for a mapped face, a streamed face's cell is complete): copied, or with @p merge merged with the max
 * rule (svf__merge). A mapped face's records were checked at open and are read here unchecked; a streamed face's are
 * checked as they are read.
 * @p scratch (svf_cell_bytes long) is used by a streamed face without a cache when merging. SVF_E_RANGE for a record
 * that breaks its box or length, SVF_E_IO for a read that fails. */
svf_err svf__expand(const svf_face *f, uint16_t g, uint8_t *cell, uint8_t *scratch, int merge);

/* svf_stream.c: svf__expand of a streamed face for the record at file offset @p at, @p len bytes. */
svf_err svf__stream_expand(const svf_face *f, uint32_t at, uint32_t len, uint8_t *cell, uint8_t *scratch, int merge);

/* svf_face.c: non-zero if the face has a usable metrics table. */
int svf__has_metrics(const svf_face *f);

/* svf_jamo.c: merges @p h rows of @p n bytes of piece rows at @p src (face memory of @p f, read with SVF__RD8; rows
 * @p src_pitch apart) into @p dst (rows @p dst_pitch apart): the maximum coverage at each pixel (OR at 1 bpp, per 2-bit
 * field at 2 bpp, per byte at 8 bpp). Words where SVF_MERGE_WORDS allows (see SVF__MERGE_UNALIGNED), bytes otherwise,
 * so any run of a cell can be merged (one run: h 1). */
void svf__merge(const svf_face *f, uint8_t *dst, uint16_t dst_pitch, const uint8_t *src, uint16_t src_pitch, uint16_t n,
                uint8_t h);

/* svf_stream.c: brings @p n bytes at file offset @p at of a streamed face to @p dst: copied, or with @p merge merged
 * (svf__merge, a cache required: the bytes are in a cache block). Through the cache's blocks, or without one a single
 * read straight into @p dst (copy only). SVF_E_IO if a read fails or is short. */
svf_err svf__stream_bytes(const svf_face *f, uint32_t at, uint8_t *dst, uint32_t n, int merge);

/* svf_face.c: the glyph index of @p cp in the code point table, or -1. */
int32_t svf__glyph_index(const svf_face *f, uint32_t cp);
/* The midpoint of the cmap search, lo <= hi, without a division and without overflow where int is 16 bits (AVR): lo + hi
 * may exceed 65535 (count up to 65535), hi - lo cannot. U is the unsigned type the difference is shifted in (tests/test_kernels.c
 * passes uint16_t to emulate a 16-bit int). */
#define SVF__CMAP_MID(lo, hi, U) ((uint16_t)((lo) + (U)((U)((hi) - (lo)) >> 1)))

/* svf_face.c: clears the bits past the cell width in the last byte of every row of @p cell (svf_cell_bytes long). */
void svf__mask_cell(const svf_face *f, uint8_t *cell);

/* svf_jamo.c: validates the JSET region of an opened face whose flags name one, with the reader of its version (version
 * 1 with SVF_JSET_V1: today's rules; version 2 with SVF_JSET_V2: the rules (1)-(7) of jamoset.unpack, every drawn
 * slot's piece number with a glyph; a version the build does not read is SVF_FACE_JSET_UNREAD, a malformed block or a
 * version other than 1, 2, 3 is SVF_FACE_JSET_BAD; a version 3 block is JSET_BAD in a build with
 * neither SVF_JMAP nor SVF_JMAP_RECOG; with SVF_JMAP_RECOG (and without SVF_JMAP) it is SVF_FACE_JMAP_UNREAD (SVF_FACE_JMAP_BAD
 * when its header or its fallback's advance breaks a JMAP rule) and its fallback JSET, if any, is checked in its place; a LIST
 * block (flags bit 3) is read with SVF_JMAP_LIST; with SVF_JMAP and without it the header rules R1', L2, L3, R4 are applied: a
 * failure (its fallback's included) is SVF_FACE_JMAP_BAD, a pass is SVF_FACE_JMAP_UNREAD with the fallback JSET, if any, read in its
 * place. On success records the counts in
 * the face; on failure the face stays plain. Reads nothing past the 16-byte block header
 * (version 1: 12 bytes) before checking the region's length against the block size (a streamed face loads only that
 * header of a region longer than SVF__JSET_LOAD_MAX). With SVF_JSET_TRUSTED (svf.h: faces built into flash only) the
 * table contents are not validated; the header length, magic, version, the region-length bound (SVF__JSET_LOAD_MAX) and
 * the block size the counts give are still checked (version 1 also its count limits; version 2 trusts its counts), so a
 * streamed face loads no more than its region. Reads through corrupt counts, classes or pieces are not bounded
 * (svf.h). */
void svf__jset_check(svf_face *f);

/* svf_jamo.c: non-zero if svf_glyph draws @p cp by composing (a valid JSET and a syllable, or a compat jamo with a
 * piece). */
int svf__composed(const svf_face *f, uint32_t cp);

/* The coverage (0..255) of pixel @p x of a cell row at @p row: 1 bpp ink 255, 2 bpp level k * 85, 8 bpp the byte. */
static inline uint8_t svf__coverage(uint8_t bpp, const uint8_t *row, int32_t x)
{
	if (bpp == 1)
		return (row[x >> 3] >> (7 - (x & 7)) & 1) ? 255 : 0;
	if (bpp == 2)
		return (uint8_t)((row[x >> 2] >> (6 - 2 * (x & 3)) & 3) * 85);
	return row[x];
}

#ifndef SVF_NO_SCALE
/* svf_scale.c (left out of a build with SVF_NO_SCALE: svf_draw.c then refers to none of it). The ratio num/den is
 * reduced, num and den 1..16, not 1/1. */

/* @p v scaled: (|v| * num + den / 2) / den, with the sign of v (round half up, symmetric). */
int32_t svf__scale_len(int32_t v, uint8_t num, uint8_t den);

/* ceil(v * num / den): a scaled cell's width or height (at most 255 * 16). */
uint32_t svf__scale_dim(uint8_t v, uint8_t num, uint8_t den);

/* The stored cell of @p f at @p cell scaled into @p out as GRAY8 coverage, svf__scale_dim(cell_w) bytes a row,
 * svf__scale_dim(cell_h) rows: a whole multiple N (den 1) as N x N blocks, any other ratio area-averaged (svf.h). */
void svf__scale_cell(const svf_face *f, const uint8_t *cell, uint8_t num, uint8_t den, uint8_t *out);
#endif

#endif /* SVF_INTERNAL_H */
