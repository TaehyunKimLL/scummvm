/* svf_jamo.c - the JSET block: validation, piece lookup and syllable composition (spec/SVF.md "Jamo sets"; block
 * version 2: docs/superpowers/specs/2026-10-23-jset-compact-design.md).
 *
 * Validation follows jamoset.unpack in the spec's order: (1) header length, magic, version, reserved, the five counts,
 * NA; (2) the block length; (3) lead and tail classes; (4) the set table, the final set checked only for tail classes
 * some tail 1..27 belongs to; (5) and (6) row by row: padding, alias bits inside the presence bits, the alias range,
 * every drawn slot's piece below 0xFFFF with a glyph; every alias entry not 0xFFFF; (7) every compat byte. Each check
 * reads only bytes the earlier ones have bounded. pieces and compose follow jamoset.pieces_of and jamoset.compose.
 *
 * Also here: the SVF_JSET_BIG typedefs and limits (LIM_LC below), the version 3 block (JMAP: jmap_valid, jmap_pieces; under
 * SVF_JMAP) and its recognition without the reader (jmap_fallback; SVF_JMAP_RECOG), and the status split (jset_valid).
 *
 * The block holds no aligned data: byte reads only (rd8/rd16). Sums of u16 values are taken in unsigned (each bounded
 * below 65536 where noted) or uint32_t, so a 16-bit int (AVR) never overflows.
 *
 * SVF_JSET_TRUSTED (svf.h; never in the fuzz builds): each reader skips the table-content validation (classes, cells,
 * rows, aliases, compat) and keeps its header length, magic and version checks, records the counts, and keeps the
 * region-length check against the size the counts give (plus the loaded-region bound), so a header of the wrong size
 * is refused and no lookup reads outside the region. The table contents are then trusted: the bounds above hold only
 * for valid blocks.
 */
#include "svf_internal.h"

#include <string.h>

/* A library constant table: in flash and read with pgm_read_byte on AVR (where .rodata would be copied to RAM), else a
 * plain const table. (Face bytes go through SVF__RD8, which may be overridden or test the source; these are the
 * library's own tables.) */
#ifdef __AVR__
#include <avr/pgmspace.h>
#define ROM_TABLE static const uint8_t PROGMEM
#define ROM(a, i) pgm_read_byte((a) + (i))
#else
#define ROM_TABLE static const uint8_t
#define ROM(a, i) ((a)[i])
#endif

enum {
	LEAD_CLASS = SVF__JSET_HEADER, /* 19 bytes */
	TAIL_CLASS = LEAD_CLASS + 19,  /* 28 bytes */
	SETS = TAIL_CLASS + 28,        /* the set table: LC*21*TC cells of W bits */
	COMPAT = 51
};

/* A byte, and a little-endian u16, of face memory (svf_internal.h: SVF__RD8). */
#define RD(p) SVF__RD8(f, (p))

#if SVF_JSET_V2 || SVF_JMAP
static unsigned rd16p(const svf_face *f, const uint8_t *p)
{
	(void)f; /* unused where SVF__RD8 does not test the source */
	return RD(p) | (unsigned)RD(p + 1) << 8;
}

#if SVF_JSET_POP4
/* The bits set in each nibble. */
ROM_TABLE pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

/* The bits set in @p x (below 2^27): a nibble a step. */
static unsigned popcount(uint32_t x)
{
	unsigned n = 0;

	for (; x; x >>= 4)
		n += ROM(pop4, x & 15);
	return n;
}
#else
/* The bits set in @p x (below 2^27): one step a set bit (no table). */
static unsigned popcount(uint32_t x)
{
	unsigned n = 0;

	for (; x; x &= x - 1)
		++n;
	return n;
}
#endif
#endif

/* The limits of LC, TC, NI, NM, NF (header bytes 6..10) of versions 1 and 2: the narrow ones, or with SVF_JSET_BIG the
 * widened ones (spec/SVF.md). One row, read by both readers (a literal in each, in the narrow build, as before). */
#if SVF_JSET_BIG
#define LIM_LC 19
#define LIM_TC 28
#define LIM_NI 128
#define LIM_NM 128
#define LIM_NF 64
/* The type of a bit offset into the set table and of a bit set of classes: the table has up to 223,440 bits and a tail
 * class reaches 27, both beyond a 16-bit unsigned. */
typedef uint32_t svf__bits_t;
/* The set of tail classes some tail 1..27 belongs to (bit c for class c, at most 27 with BIG). The narrow build keeps the
 * 16-bit set it always had, so its object code is unchanged. */
typedef uint32_t svf__cls_t;
#define CLASS_BIT(c) ((uint32_t)1 << (c))
#else
#define LIM_LC 12
#define LIM_TC 12
#define LIM_NI 32
#define LIM_NM 24
#define LIM_NF 16
typedef unsigned svf__bits_t;
typedef uint16_t svf__cls_t;
#define CLASS_BIT(c) (1u << (c))
#endif

#if SVF_JSET_V2
/* slot_piece's value for a clear presence bit: SVF_PIECE_BLANK once cut to 16 bits. */
#define NO_PIECE 0xFFFFFFFFu
/* compat_piece's value for a compat byte that names no present slot: not below 0xFFFF, not NO_PIECE. */
#define BAD_PIECE 0x10000u

#if !SVF_JSET_TRUSTED
/* The limits as a table (version 2 reads them by index). */
ROM_TABLE limits[5] = {LIM_LC, LIM_TC, LIM_NI, LIM_NM, LIM_NF};
#endif
/* The slots of an initial, medial and final set (t = 0, 1, 2). */
#define SLOTS(t) ((t) == 2 ? 27u : 19u + 2u * (t))

/* For the compat consonants U+3131 + k (k < 30): bits 0-4 the lead index, 31 for a cluster consonant; bit 5 set for a
 * lead-capable consonant with no final form (ㄸ ㅃ ㅉ: 36, 40, 45 are then 31 or more, as a cluster's); bits 6-7
 * (k > 7) + (k > 18) + (k > 24): the final slot (tail index - 1) of every other consonant is k minus that. */
ROM_TABLE compat_lead[30] = {0,   1,   31,  2,   31,  31,  3,   36,  69,  95,  95,  95,  95,  95,  95,
                                        95,  70,  71,  104, 159, 137, 138, 139, 140, 173, 206, 207, 208, 209, 210};

/* Row @p s of table @p t (0 initial, 1 medial, 2 final; 3 with s 0: the alias table). Rows are 5, 5 and 6 bytes, twice
 * that with aliases; every offset is below SVF__JSET_MAX (45,068 with SVF_JSET_BIG: beyond a 16-bit int, so offsets are
 * unsigned, and the set table's bit count, 223,440 at most, svf__bits_t). */
static const uint8_t *row_at(const svf_face *f, unsigned t, unsigned s)
{
	const uint8_t *b = f->data + f->jset_off;
	/* the set table: at most 12 * 21 * 12 * 14 = 42,336 bits (BIG: 19 * 21 * 28 * 20 = 223,440) */
	unsigned u, r;
	svf__bits_t off = SETS + (((svf__bits_t)f->jset_lc * 21u * f->jset_tc *
	                           ((unsigned)f->jset_w[0] + f->jset_w[1] + f->jset_w[2]) + 7u) >> 3);

	for (u = 0;; ++u) {
		r = (u == 2 ? 6u : 5u) << f->jset_na;
		if (u == t)
			return b + off + s * r;
		off += RD(b + 8 + u) * r;
	}
}

/* The set number of table @p t (0 initial, 1 medial, 2 final) in cell @p cell of the set table: its field read from two
 * bytes (w + 7 <= 12 bits), both inside the block (the table is followed by at least 16 bytes of rows). */
static unsigned cell_set(const svf_face *f, unsigned cell, unsigned t)
{
	const uint8_t *w = f->jset_w;
	/* at most 3,023 * 14 + 9 bits (BIG: 11,171 * 20 + 14) */
	unsigned u;
	svf__bits_t bit = (svf__bits_t)cell * ((unsigned)w[0] + w[1] + w[2]);
	const uint8_t *p;

	for (u = 0; u < t; ++u) /* the fields below table t's */
		bit += w[u];
	p = f->data + f->jset_off + SETS + (bit >> 3);
	return rd16p(f, p) >> (bit & 7) & ((1u << w[t]) - 1); /* w[t] + 7 <= 14 bits (BIG: 7 + 7) */
}

/* The @p n byte little-endian word at @p p. */
static uint32_t word(const svf_face *f, const uint8_t *p, unsigned n)
{
	uint32_t w = 0;

	(void)f; /* unused where SVF__RD8 does not test the source */
	while (n--)
		w = w << 8 | RD(p + n);
	return w;
}

/* The piece of slot @p j (any bit of the presence bytes) of set @p s of table @p t: NO_PIECE for a clear
 * presence bit; with its alias bit set the alias entry abase + the alias bits below j, else base + the fresh bits below
 * j, computed in 32 bits. BAD_PIECE for an alias bit without its presence bit or an alias entry at or past NA: so every
 * read stays inside a block whose length matches its counts, and validation refuses what it returns at 0xFFFF or
 * more. */
static uint32_t slot_piece(const svf_face *f, unsigned t, unsigned s, unsigned j)
{
	unsigned pb = t == 2 ? 4 : 3, alias;
	const uint8_t *row = row_at(f, t, s), *pres = row + (2u << f->jset_na);
	uint32_t pw = word(f, pres, pb), aw = f->jset_na ? word(f, pres + pb, pb) : 0, below = ((uint32_t)1 << j) - 1, i;

	if (!(pw >> j & 1))
		return aw >> j & 1 ? BAD_PIECE : NO_PIECE;
	alias = aw >> j & 1;
	/* base (row bytes 0-1) or abase (2-3), plus the fresh or the alias bits below j */
	i = (uint32_t)rd16p(f, row + 2 * alias) + popcount((alias ? aw : pw & ~aw) & below);
	if (!alias)
		return i;
	if (i >= rd16p(f, f->data + f->jset_off + 12))
		return BAD_PIECE;
	return rd16p(f, row_at(f, 3, 0) + 2 * i);
}

/* The piece compat byte @p k (U+3131 + k) names: NO_PIECE for 0xFF, BAD_PIECE for a byte that names no present slot
 * (bits 5-6 set unless SVF_JSET_BIG, a set at or past its table's count, bit 7 on a vowel, a cluster or ㄸ ㅃ ㅉ, a clear presence bit). */
static uint32_t compat_piece(const svf_face *f, unsigned k)
{
	const uint8_t *b = f->data + f->jset_off;
	unsigned c = RD(b + f->jset_len - COMPAT + k), m = 31, t = 1, j = k - 30;
	uint32_t p;

	if (c == 0xFF)
		return NO_PIECE;
	if (k < 30) {
		m = ROM(compat_lead, k);
		t = 2;
		j = k - (m >> 6);
		m &= 63;
		if (m != 31 && !(c & 0x80)) {
			t = 0;
			j = m & 31;
		}
	}
	/* bit 7 only on a lead-capable consonant with a final form (m below 31) */
#if SVF_JSET_BIG
	/* the set is bits 0-6 (up to 128 sets), bit 7 the final switch */
	if (((c & 0x80) && m >= 31) || (c & 0x7F) >= RD(b + 8 + t))
		return BAD_PIECE;
	p = slot_piece(f, t, c & 0x7F, j);
#else
	if ((c & 0x60) || ((c & 0x80) && m >= 31) || (c & 31) >= RD(b + 8 + t))
		return BAD_PIECE;
	p = slot_piece(f, t, c & 31, j);
#endif
	return p == NO_PIECE ? BAD_PIECE : p;
}

#endif

#if SVF_JSET_V1
/* Version 1 (SVF_JSET_V1; spec/SVF.md "Jamo sets"): byte-per-cell set tables and u16 piece tables. Its header is
 * 12 bytes; the counts are at the same offsets as in version 2. */
enum {
	V1_HEADER = 12,
	V1_LEAD_CLASS = V1_HEADER,
	V1_TAIL_CLASS = V1_LEAD_CLASS + 19,
	V1_SETS = V1_TAIL_CLASS + 28 /* initSet, medSet, finSet: LC*21*TC bytes each */
};

/* Bytes of one set table. */
static uint32_t set_cells(const svf_face *f)
{
	return (uint32_t)f->jset_lc * 21 * f->jset_tc;
}

/* Offset in the file of the piece tables (initPiece, medPiece, finPiece, compatPiece: contiguous u16). */
static uint32_t pieces_off(const svf_face *f)
{
	return f->jset_off + V1_SETS + 3 * set_cells(f);
}

/* Validates a version 1 block; 1 if it is usable. LC, TC, the advance and NI, NM, NF (in jset_w: a version 1 face has
 * no field widths) are recorded in @p f as they are read (cleared by the caller on 0). */
static int jset_valid_v1(svf_face *f)
{
	uint32_t at = f->jset_off;

	/* jset_part checked the length (12), the magic and the version */
	f->jset_lc = rd8(f, at + 6);
	f->jset_tc = rd8(f, at + 7);
	f->jset_w[0] = rd8(f, at + 8);
	f->jset_w[1] = rd8(f, at + 9);
	f->jset_w[2] = rd8(f, at + 10);
	f->jset_adv = rd8(f, at + 11);
	{
	uint32_t cells, n, size;
#if !SVF_JSET_TRUSTED
	uint32_t i;
	svf__cls_t final_classes = 0;
	int w;
#endif

#if SVF_JSET_TRUSTED
	/* validated counts bound the size below SVF__JSET_LOAD_MAX; trusted ones do not */
	if (f->jset_len > SVF__JSET_LOAD_MAX)
		return 0;
#endif
	if (f->jset_lc < 1 || f->jset_lc > LIM_LC || f->jset_tc < 1 || f->jset_tc > LIM_TC || f->jset_w[0] < 1 ||
	    f->jset_w[0] > LIM_NI || f->jset_w[1] < 1 || f->jset_w[1] > LIM_NM || f->jset_w[2] < 1 || f->jset_w[2] > LIM_NF)
		return 0;
	cells = set_cells(f);
	n = (uint32_t)f->jset_w[0] * 19 + (uint32_t)f->jset_w[1] * 21 + (uint32_t)f->jset_w[2] * 27 + COMPAT;
	size = V1_SETS + 3 * cells + 2 * n;
	if (f->jset_len != size)
		return 0;
#if SVF_JSET_TRUSTED
	return 1; /* svf.h: the table contents are trusted */
#else

	for (i = 0; i < 19; ++i)
		if (rd8(f, at + V1_LEAD_CLASS + i) >= f->jset_lc)
			return 0;
	for (i = 0; i < 28; ++i) {
		uint8_t c = rd8(f, at + V1_TAIL_CLASS + i);
		if (c >= f->jset_tc)
			return 0;
		if (i)
			final_classes = (svf__cls_t)(final_classes | CLASS_BIT(c));
	}

	for (w = 0; w < 3; ++w)
		for (i = 0; i < cells; ++i) {
			/* A final set is read only for syllables with a final: a cell of a tail class no tail 1..27 is in
			 * stays unread, whatever it holds. */
			if (w == 2 && !(final_classes & CLASS_BIT(i % f->jset_tc)))
				continue;
			if (rd8(f, at + V1_SETS + (uint32_t)w * cells + i) >= f->jset_w[w])
				return 0;
		}

	at = pieces_off(f);
	for (i = 0; i < n; ++i) {
		uint16_t p = rd16(f, at + 2 * i);
		if (p != SVF_PIECE_BLANK && svf__glyph_index(f, SVF_PIECE_BASE + p) < 0)
			return 0;
	}
	return 1;
#endif
	}
}

static int pieces_v1(const svf_face *f, uint32_t cp, uint16_t out[3])
{
	uint32_t at, cells, cell, s, lead, vowel, tail, med_off, fin_off;
	uint16_t p;

	at = pieces_off(f);
	med_off = at + 2u * 19 * f->jset_w[0];
	fin_off = med_off + 2u * 21 * f->jset_w[1];
	if (cp >= 0x3131 && cp < 0x3131 + COMPAT) {
		p = rd16(f, fin_off + 2u * 27 * f->jset_w[2] + 2 * (cp - 0x3131));
		if (p == SVF_PIECE_BLANK)
			return 0;
		out[0] = p;
		return 1;
	}
	if (cp < 0xAC00 || cp > 0xD7A3)
		return 0;
	s = cp - 0xAC00;
#if SVF_JSET_V2
	/* with both readers, the version 2 reader's two multiply-shifts (exact for every s below 11,172): no libgcc
	 * division call on a version 1 face either */
	lead = s * 14267u >> 23;
	tail = s - lead * 588u;
	vowel = tail * 2341u >> 16;
	tail -= vowel * 28u;
#else
	/* the version 1 reader alone keeps its divisions (smaller on AVR, its default) */
	lead = s / 588;
	vowel = (s / 28) % 21;
	tail = s % 28;
#endif
	cells = set_cells(f);
	cell = ((uint32_t)rd8(f, f->jset_off + V1_LEAD_CLASS + lead) * 21 + vowel) * f->jset_tc +
	       rd8(f, f->jset_off + V1_TAIL_CLASS + tail);
	out[0] = rd16(f, at + 2 * ((uint32_t)rd8(f, f->jset_off + V1_SETS + cell) * 19 + lead));
	out[1] = rd16(f, med_off + 2 * ((uint32_t)rd8(f, f->jset_off + V1_SETS + cells + cell) * 21 + vowel));
	if (!tail)
		return 2;
	out[2] = rd16(f, fin_off + 2 * ((uint32_t)rd8(f, f->jset_off + V1_SETS + 2 * cells + cell) * 27 + tail - 1));
	return 3;
}
#endif

/* Validates a version 2 block (rules (1)-(7)); 1 if it is usable. LC, TC, the advance, the alias flag and the field
 * widths are recorded in @p f as they are read (cleared by the caller on 0). SVF_JSET_TRUSTED: only the header length,
 * magic, version and the block size the counts give against the region length are checked (the count limits are not,
 * for this reader), the rest is recorded as it is. */
#if SVF_JSET_V2
static int jset_valid_v2(svf_face *f)
{
	const uint8_t *b = f->data + f->jset_off;
	unsigned i, j, n, na;
#if !SVF_JSET_TRUSTED
	const uint8_t *row;
	unsigned t, s, tcl, slots = 0;
	svf__cls_t final_classes = 0;
	uint32_t p;
#endif

	/* (1): jset_part checked the magic and the version */
	if (f->jset_len < SVF__JSET_HEADER)
		return 0;
#if !SVF_JSET_TRUSTED
	if (rd16p(f, b + 14))
		return 0;
#endif
	for (i = 0; i < 5; ++i) {
		n = RD(b + 6 + i);
#if !SVF_JSET_TRUSTED
		if (n - 1u >= ROM(limits, i))
			return 0;
#endif
		if (i >= 2) {
			for (j = 0; (1u << j) < n; ++j) /* n at most 255 */
				;
			f->jset_w[i - 2] = (uint8_t)j;
#if !SVF_JSET_TRUSTED
			slots += n * SLOTS(i - 2); /* at most 1,544 (BIG: 6,848) */
#endif
		}
	}
	f->jset_lc = RD(b + 6);
	f->jset_tc = RD(b + 7);
	f->jset_adv = RD(b + 11);
	na = rd16p(f, b + 12);
#if !SVF_JSET_TRUSTED
	if (na > slots)
		return 0;
#endif
	f->jset_na = na != 0;
	/* (2), also with SVF_JSET_TRUSTED: no lookup then leaves the region the header names (a streamed face loads no
	 * more than SVF__JSET_LOAD_MAX of it) */
#if SVF_JSET_TRUSTED
	if (f->jset_len > SVF__JSET_LOAD_MAX) /* validated counts bound the size below SVF__JSET_MAX; trusted ones do not */
		return 0;
#endif
	if (f->jset_len != (unsigned)(row_at(f, 3, 0) - b) + 2u * na + COMPAT)
		return 0;
#if SVF_JSET_TRUSTED
	return 1; /* svf.h: the table contents are trusted */
#else

	/* (3) */
	for (i = 0; i < 19 + 28; ++i) {
		n = RD(b + LEAD_CLASS + i);
		if (n >= (i < 19 ? f->jset_lc : f->jset_tc))
			return 0;
		if (i > 19)
			final_classes = (svf__cls_t)(final_classes | CLASS_BIT(n));
	}

	/* (4): cell by cell, the tail class counted alongside (no division); a final set is read only for syllables
	 * with a final, so a cell of a tail class no tail 1..27 is in stays unread, whatever it holds. */
	n = (unsigned)f->jset_lc * 21u * f->jset_tc;
	for (i = 0, tcl = 0; i < n; ++i) {
		for (t = 0; t < 3; ++t)
			if (cell_set(f, i, t) >= RD(b + 8 + t) && (t < 2 || (final_classes >> tcl & 1)))
				return 0;
		if (++tcl == f->jset_tc)
			tcl = 0;
	}

	/* (5) and (6), row by row: abase <= NA; then every bit of the presence bytes: no alias bit without its
	 * presence bit, no alias entry at or past NA (with abase <= NA: abase + the alias bits <= NA), no presence bit
	 * in the padding, every piece below 0xFFFF (32 bits) with a glyph */
	for (t = 0; t < 3; ++t)
		for (s = 0; s < RD(b + 8 + t); ++s) {
			row = row_at(f, t, s);
			if (na && rd16p(f, row + 2) > na)
				return 0;
			for (j = 0; j < 32; ++j) { /* past the presence bytes of an initial or medial row: clear */
				p = slot_piece(f, t, s, j);
				if (p != NO_PIECE &&
				    (j >= SLOTS(t) || p >= 0xFFFFu || svf__glyph_index(f, SVF_PIECE_BASE + p) < 0))
					return 0;
			}
		}
	row = row_at(f, 3, 0);
	for (i = 0; i < na; ++i)
		if (rd16p(f, row + 2 * i) == SVF_PIECE_BLANK)
			return 0;

	/* (7) */
	for (i = 0; i < COMPAT; ++i)
		if (compat_piece(f, i) == BAD_PIECE)
			return 0;
	return 1;
#endif
}
#endif

#if SVF_JMAP
/* Version 3 (SVF_JMAP; spec/SVF.md "Version 3: mapping table (JMAP)"): a direct map from a syllable to the pieces of its
 * three planes. Offsets of the block (the spec's table). */
enum {
	JM_FLAGS = 6,    /* u8: 1 4B entries, 2 SPARSE, 4 FALLBACK, 8 LIST */
	JM_ADV = 7,      /* u8 advance */
	JM_NS = 8,       /* u16 */
	JM_COUNTS = 10,  /* u16 PI, PM, PF */
	JM_COMPAT = 16,  /* u16[51] */
	JM_DENSE = 118,  /* the entries of a dense block, which is also the shortest header any block has */
	JM_RANK = 118,   /* SPARSE: u16[44] */
	JM_BITMAP = 206, /* SPARSE: u8[1397] */
	JM_PAD = 1603,   /* SPARSE: the pad byte */
	JM_SPARSE = 1604 /* the entries of a SPARSE block */
#if SVF_JMAP_LIST
	, JM_LIST = 118  /* the entries of a LIST block: NS x (u16 key, i, m, f), sorted by key */
#endif
};

/* The fields i, m, f of entry @p idx (3 or 4 bytes; 4B: b0 holds the high bits, i 0-2, m 3-5, f 6-7). */
static void jmap_fields(const svf_face *f, uint32_t idx, unsigned e[3])
{
	uint8_t fl = f->jmap_flags;
	const uint8_t *p = f->data + f->jmap_off + (fl & 2 ? (uint32_t)JM_SPARSE : (uint32_t)JM_DENSE) + idx * (fl & 1 ? 4u : 3u);
	unsigned hi = 0;

	if (fl & 1)
		hi = RD(p++);
	e[0] = (hi & 7u) << 8 | RD(p);
	e[1] = (hi >> 3 & 7u) << 8 | RD(p + 1);
	e[2] = (hi >> 6) << 8 | RD(p + 2);
}

#if SVF_JMAP_LIST
/* The LIST entry of syllable @p s (binary search, spec "LIST"): 0 for a miss, 2 or 3 for a hit (the entry's fields are final piece
 * numbers; a final of SVF_PIECE_BLANK is not returned). Every probe is below 118 + 8 NS, which jmap_valid's R4 bounded. */
static int jlist_find(const svf_face *f, unsigned s, uint16_t out[3])
{
	const uint8_t *b = f->data + f->jmap_off;
	unsigned lo = 0, hi = RD(b + JM_NS), mid;	/* L3: NS is 1..255 and byte 9 is 0 */

	while (lo < hi) {
		const uint8_t *e = b + JM_LIST + 8u * (mid = (lo + hi) >> 1);
		unsigned k = rd16p(f, e);

		if (k == s) {
			out[0] = (uint16_t)rd16p(f, e + 2);
			out[1] = (uint16_t)rd16p(f, e + 4);
			return (out[2] = (uint16_t)rd16p(f, e + 6)) == SVF_PIECE_BLANK ? 2 : 3;
		}
		if (k < s)
			lo = mid + 1;
		else
			hi = mid;
	}
	return 0;
}
#endif

/* The pieces of @p cp from the JMAP: 0 for a syllable the table does not list (and for a blank compat entry), 1 for a
 * compat jamo, 2 or 3 for a syllable (the entry decides the count; in a dense or SPARSE entry a 0 field is SVF_PIECE_BLANK, in a LIST entry
 * a field is a piece number and SVF_PIECE_BLANK is 0xFFFF itself: jlist_find). */
static int jmap_pieces(const svf_face *f, uint32_t cp, uint16_t out[3])
{
	const uint8_t *b = f->data + f->jmap_off;
	uint32_t s, idx, k;
	unsigned e[3], c;

	if (cp - 0x3131u < COMPAT) {
		c = rd16p(f, b + JM_COMPAT + 2 * (cp - 0x3131u));
		if (c == SVF_PIECE_BLANK)
			return 0;
		out[0] = (uint16_t)c;
		return 1;
	}
	if (cp - 0xAC00u > 0xD7A3u - 0xAC00u)
		return 0;
	s = cp - 0xAC00u;
#if SVF_JMAP_LIST
	if (f->jmap_flags & 8)
		return jlist_find(f, (unsigned)s, out);
#endif
	if (f->jmap_flags & 2) {
		unsigned x = RD(b + JM_BITMAP + (s >> 3));

		if (!(x >> (s & 7) & 1))
			return 0;
		/* the rank of its 256 block, the set bits of the bytes before its byte, the set bits below it in its byte */
		idx = rd16p(f, b + JM_RANK + 2 * (s >> 8));
		for (k = (s >> 8) * 32; k < s >> 3; ++k)
			idx += popcount(RD(b + JM_BITMAP + k));
		idx += popcount(x & ((1u << (s & 7)) - 1));
	} else
		idx = s;
	jmap_fields(f, idx, e);
	out[0] = e[0] ? (uint16_t)(e[0] - 1u) : SVF_PIECE_BLANK;
	out[1] = e[1] ? (uint16_t)(f->jmap_pi + e[1] - 1u) : SVF_PIECE_BLANK;
	if (!e[2])
		return 2;
	out[2] = (uint16_t)(f->jmap_pi + f->jmap_pm + e[2] - 1u);
	return 3;
}
#endif

/* The version word of the block at jset_off; 0 if the region is shorter than 12 bytes or the magic is not JSET. */
static unsigned jset_version(const svf_face *f)
{
	uint32_t at = f->jset_off;

	if (f->jset_len < 12 || rd8(f, at) != 'J' || rd8(f, at + 1) != 'S' || rd8(f, at + 2) != 'E' ||
	    rd8(f, at + 3) != 'T')
		return 0;
	return rd16(f, at + 4);
}

/* The version 1 or 2 block at jset_off with the reader of its version: 1 usable, 0 malformed (or a version that is neither),
 * -1 well formed but its reader is not compiled. */
static int jset_part(svf_face *f, unsigned v)
{
	(void)f; /* unused with neither reader (the KS-only build) */
#if SVF_JSET_V1
	if (v == 1)
		return jset_valid_v1(f);
#endif
#if SVF_JSET_V2
	if (v == 2)
		return jset_valid_v2(f);
#endif
	return v == 1 || v == 2 ? -1 : 0;
}

/* The JSET part off: nothing is composed from the JSET tables. */
static void jset_clear(svf_face *f)
{
	f->jset_lc = f->jset_tc = f->jset_adv = f->jset_na = 0;
	f->jset_w[0] = f->jset_w[1] = f->jset_w[2] = 0;
}

#if SVF_JMAP
/* Validates a version 3 block (JMAP), rules R1-R8 of spec/SVF.md in jamoset.unpack_jmap's order, each reading only bytes
 * the earlier ones bounded (jset_version checked 12 bytes, the magic and the version word). Records the block in
 * jmap_off/jmap_flags/jmap_pi/jmap_pm and, with FALLBACK, re-points jset_off/jset_len at the fallback and validates it
 * with the compiled reader of its version. A block that fails a rule, its fallback included, sets SVF_FACE_JMAP_BAD
 * and leaves a plain face (the one verdict). Returns what jset_valid does for the JSET part: 1 (nothing for
 * svf__jset_check to do), or -1 for a well-formed fallback whose reader is not compiled (the JMAP part stays usable). With
 * SVF_JSET_TRUSTED only R1-R4 are checked (the fallback keeps its own trusted rules). A LIST block in a build without
 * SVF_JMAP_LIST gets R1', L2, L3, R4 and then only its fallback (SVF_FACE_JMAP_UNREAD, jmap_off 0, as SPARSE in a
 * RECOG build); a failed rule or fallback is the same JMAP_BAD. */
#if !SVF_JSET_BIG && (SVF_JSET_V1 || SVF_JSET_V2)
/* 1 if the version 1 or 2 header at jset_off has every count within the BIG row (19, 28, 128, 128, 64) and one over this
 * build's (the version 1 and 2 headers hold LC, TC, NI, NM, NF at the same offsets). Without a JSET reader the fallback is
 * JSET_UNREAD anyway. */
static int jset_over_narrow(const svf_face *f)
{
	const uint8_t *b = f->data + f->jset_off;
	unsigned lc = RD(b + 6), tc = RD(b + 7), ni = RD(b + 8), nm = RD(b + 9), nf = RD(b + 10);

	if (lc - 1u >= 19 || tc - 1u >= 28 || ni - 1u >= 128 || nm - 1u >= 128 || nf - 1u >= 64)
		return 0;
	return lc > LIM_LC || tc > LIM_TC || ni > LIM_NI || nm > LIM_NM || nf > LIM_NF;
}
#endif

#if SVF_JMAP_LIST && !SVF_JSET_TRUSTED
/* L5, L6, L7 of a LIST block whose header rules passed (jmap_off is set; the keys and fields lie below 118 + 8 NS <= the region):
 * 1 if every compat entry and entry field is SVF_PIECE_BLANK or a piece with a glyph and the keys strictly increase to at most 11,171. */
static int jlist_content(const svf_face *f, unsigned ns)
{
	const uint8_t *b = f->data + f->jmap_off;
	unsigned i, j, k, last = 0;

	for (i = 0; i < 51; ++i) {
		k = rd16p(f, b + JM_COMPAT + 2 * i);
		if (k != SVF_PIECE_BLANK && svf__glyph_index(f, SVF_PIECE_BASE + k) < 0)
			return 0;
	}
	for (i = 0; i < ns; ++i) {
		const uint8_t *e = b + JM_LIST + 8u * i;

		k = rd16p(f, e);
		if (i && k <= last)
			return 0;
		last = k;
		for (j = 2; j < 8; j += 2) {
			k = rd16p(f, e + j);
			if (k != SVF_PIECE_BLANK && svf__glyph_index(f, SVF_PIECE_BASE + k) < 0)
				return 0;
		}
	}
	return last <= 11171u;
}
#endif

static int jmap_valid(svf_face *f)
{
	const uint8_t *b = f->data + f->jset_off;
	uint32_t n = f->jset_len, fb, total;
	unsigned fl, ns, pi, pm, pf, v = 0;
#if !SVF_JSET_TRUSTED
	uint32_t i;
	unsigned e[3];
#endif
	int r = 1;

	/* R1' (bits 4-7 clear, and LIST, bit 3, excludes 4B and SPARSE; every build applies the header rules to a LIST block) */
	if (n < JM_DENSE || ((fl = RD(b + JM_FLAGS)) & 0xF0) || ((fl & 8) && (fl & 3)))
		goto bad;
	/* R2 (L2 with LIST: no planes) */
	pi = rd16p(f, b + JM_COUNTS);
	pm = rd16p(f, b + JM_COUNTS + 2);
	pf = rd16p(f, b + JM_COUNTS + 4);
	total = (uint32_t)pi + pm + pf;
	if (fl & 8) {
		if (total)
			goto bad;
	} else {
		if (fl & 1 ? pi > 2047 || pm > 2047 || pf > 1023 : pi > 255 || pm > 255 || pf > 255)
			goto bad;
		if (!total)
			goto bad;
	}
	/* R3 (L3 with LIST: NS 1..255) */
	ns = rd16p(f, b + JM_NS);
	if (ns - 1u >= (fl & 8 ? 255u : 11172u) || (!(fl & 10) && ns != 11172u))
		goto bad;
	/* R4 */
	if (n > 65534u)
		goto bad;
	fb = (fl & 2 ? (uint32_t)JM_SPARSE : (uint32_t)JM_DENSE) + (uint32_t)ns * (fl & 8 ? 8u : 3u + (fl & 1));
	if (!(fl & 4)) {
		if (n != fb)
			goto bad;
	} else {
		if (n < fb + 12u)
			goto bad;
		/* the fallback header: magic, version 1 or 2, and its advance (byte 11 of both headers) equal to the JMAP's, in
		 * every build, before any reader is consulted */
		if (RD(b + fb) != 'J' || RD(b + fb + 1) != 'S' || RD(b + fb + 2) != 'E' || RD(b + fb + 3) != 'T')
			goto bad;
		v = rd16p(f, b + fb + 4);
		if ((v != 1 && v != 2) || RD(b + fb + 11) != RD(b + JM_ADV))
			goto bad;
	}
#if !SVF_JMAP_LIST
	if (fl & 8) {
		/* a LIST block this build does not read: like the RECOG build, JMAP_UNREAD and its fallback alone (the header
		 * rules above held; the content rules L5-L7 are not applied) */
		f->status |= SVF_FACE_JMAP_UNREAD;
		/* with no fallback jset_off / jset_len still name the LIST region and are left so: nothing reads them, jmap_off and
		 * jset_lc are 0 and svf_is_jamo gates every lookup (clearing them would cost bytes in the gated rows) */
		goto fallback;
	}
#endif
	f->jmap_off = f->jset_off;
	f->jmap_flags = (uint8_t)fl;
	f->jmap_pi = (uint16_t)pi;
	f->jmap_pm = (uint16_t)pm;
#if !SVF_JSET_TRUSTED
#if SVF_JMAP_LIST
	if (fl & 8) {
		if (!jlist_content(f, ns))
			goto bad;
		goto fallback;
	}
#endif
	/* R5 */
	for (i = 0; i < 51; ++i) {
		unsigned c = rd16p(f, b + JM_COMPAT + 2 * i);
		if (c != SVF_PIECE_BLANK && c >= total)
			goto bad;
	}
	/* R6: the pad, the bitmap's tail bits, each rank against the set bits so far (at most byte 1396 = offset 1602), the total */
	if (fl & 2) {
		uint32_t run = 0, k;

		if (RD(b + JM_PAD) || (RD(b + JM_BITMAP + 1396) & 0xF0))
			goto bad;
		for (i = 0; i < 44; ++i) {
			if (rd16p(f, b + JM_RANK + 2 * i) != run)
				goto bad;
			for (k = i * 32; k < i * 32 + 32 && k < 1397; ++k)
				run += popcount(RD(b + JM_BITMAP + k));
		}
		if (run != ns)
			goto bad;
	}
	/* R7 */
	for (i = 0; i < ns; ++i) {
		jmap_fields(f, i, e);
		if (e[0] > pi || e[1] > pm || e[2] > pf)
			goto bad;
	}
	/* R8: every piece number below PI + PM + PF has a glyph (the compat entries are below it by R5) */
	for (i = 0; i < total; ++i)
		if (svf__glyph_index(f, SVF_PIECE_BASE + i) < 0)
			goto bad;
#endif
#if !SVF_JMAP_LIST || !SVF_JSET_TRUSTED
fallback:
#endif
	if (fl & 4) {
		/* the fallback JSET: its own verdict (its pieces with a glyph included); malformed -> the block is bad */
		f->jset_off += fb;
		f->jset_len = (uint16_t)(n - fb);
		r = jset_part(f, v);
#if !SVF_JSET_BIG && (SVF_JSET_V1 || SVF_JSET_V2)
		/* a fallback whose counts fit the BIG row but not this build's is a table this build is not compiled to read, like
		 * one whose reader is missing: JSET_UNREAD, the JMAP part stays usable (a stand-alone JSET of such counts stays
		 * JSET_BAD, PR-4) */
		if (!r && jset_over_narrow(f))
			r = -1;
#endif
		if (!r)
			goto bad;
	}
	return r;
bad:
	f->jset_off = 0;	/* a fallback re-pointed these; nothing reads them once jmap_off and jset_lc are 0 (svf_is_jamo gates every lookup) */
	f->jset_len = 0;
	f->jmap_off = 0;
	f->jmap_flags = 0;
	f->jmap_pi = f->jmap_pm = 0;
	jset_clear(f);
	f->status = (uint16_t)((f->status & ~SVF_FACE_JMAP_UNREAD) | SVF_FACE_JMAP_BAD);
	return 1;
}
#endif

#if !SVF_JMAP && SVF_JMAP_RECOG
/* A version 3 block (JMAP) in a build that does not read it (PR-6, ruling R-M): sets SVF_FACE_JMAP_UNREAD. A block the
 * JMAP rules would refuse on its header (R1' reserved flag bits or LIST with 4B or SPARSE, R3 / L3 NS, R4 a region of
 * 0xFFFF, the clip of one over the 65,534 cap) is SVF_FACE_JMAP_BAD instead (returns 0). A LIST block (flag 0x08: entries
 * of 8 bytes, F = 118 + 8 NS, NS 1..255) is read through its fallback like any other: its planes counts (L2) and the
 * entries are not read here. With the
 * FALLBACK flag: the fallback's advance byte must equal the JMAP's (R4: JMAP_BAD instead), and the JSET region is
 * re-pointed at the fallback (returns 1) for the compiled readers to validate; a region that ends before the 12 byte
 * fallback header (F + 12 > region) is a malformed fallback (an empty region: JSET_BAD, returns 1). Without the flag, or
 * for a streamed region longer than SVF__JSET_LOAD_MAX (only its 16 header bytes are loaded; a mapped region is whole in
 * memory), the face stays plain with JMAP_UNREAD (returns 0). Reads bytes 6-9 of the header (never 10-15), then one byte
 * at F + 11 once F + 12 <= region (F is below 46,293 as NS is at most 11,172; for a LIST F = 118 + 8 NS is at most 2,158). */
static int jmap_fallback(svf_face *f)
{
	const uint8_t *b = f->data + f->jset_off;
	unsigned fl = RD(b + 6), ns = RD(b + 8) | (unsigned)RD(b + 9) << 8;
	uint32_t fb = fl & 2 ? 1604u : 118u;

	f->status |= SVF_FACE_JMAP_UNREAD;
	if ((fl & 0xF0) || ((fl & 8) && (fl & 3)) ||
	    (fl & 8 ? ns - 1u >= 255u : fl & 2 ? ns - 1u >= 11172u : ns != 11172u) || f->jset_len == 0xFFFFu) {
		f->status = (uint16_t)((f->status & ~SVF_FACE_JMAP_UNREAD) | SVF_FACE_JMAP_BAD);
		return 0;
	}
	if (!(fl & 4) || (SVF__STREAMED(f) && f->jset_len > SVF__JSET_LOAD_MAX))
		return 0;
	fb += ns * (fl & 8 ? 8u : fl & 1 ? 4u : 3u);
	if (fb + 12u > f->jset_len) {
		f->jset_off = 0;
		f->jset_len = 0;
		return 1;
	}
	if (RD(b + 7) != RD(b + fb + 11)) {
		f->status = (uint16_t)((f->status & ~SVF_FACE_JMAP_UNREAD) | SVF_FACE_JMAP_BAD);
		return 0;
	}
	f->jset_off += fb;
	f->jset_len = (uint16_t)(f->jset_len - fb);
	return 1;
}
#endif

/* The JSET part of the region: 1 usable, 0 bad (a header shorter than 12 bytes, wrong magic, a version other than 1, 2
 * or 3, or a block its reader refuses), -1 unread (a well-formed header of a version 1 or 2 whose reader is not
 * compiled; PR-4). A version 3 block with no fallback to read has no JSET part: 1, its own bit set. With SVF_JMAP a
 * version 3 block is jmap_valid's (which sets JMAP_BAD itself). */
static int jset_valid(svf_face *f)
{
	unsigned v = jset_version(f);

#if SVF_JMAP
	if (v == 3)
		return jmap_valid(f);
#elif SVF_JMAP_RECOG
	if (v == 3) {
		if (!jmap_fallback(f))
			return 1;
		v = jset_version(f);
	}
#endif
	return jset_part(f, v);
}

void svf__jset_check(svf_face *f)
{
	int r = jset_valid(f);

	if (r != 1) {
		f->status |= r < 0 ? SVF_FACE_JSET_UNREAD : SVF_FACE_JSET_BAD;
		jset_clear(f);
	}
#if SVF_JMAP
	/* the advance of a usable JMAP is the face's, whether or not its fallback is */
	if (f->jmap_off)
		f->jset_adv = RD(f->data + f->jmap_off + JM_ADV);
#endif
}

int svf_is_jamo(const svf_face *f)
{
#if SVF_JMAP
	if (f && f->jmap_off)
		return 1;
#endif
	return f && f->jset_lc != 0;
}

#if SVF_JSET_V2
static int pieces_v2(const svf_face *f, uint32_t cp, uint16_t out[3])
{
	const uint8_t *b = f->data + f->jset_off;
	unsigned s, lead, vowel, tail, cell, t, n, j[3];
	uint32_t p;

	if (cp - 0x3131u < COMPAT) {
		p = compat_piece(f, (unsigned)(cp - 0x3131u));
		if (p >= 0xFFFFu)
			return 0;
		out[0] = (uint16_t)p;
		return 1;
	}
	if (cp - 0xAC00u > 0xD7A3u - 0xAC00u)
		return 0;
	/* two multiply-shifts, exact for every s below 11,172 (tools/test_c_renderer.py test_decomposition_constants) */
	s = (unsigned)(cp - 0xAC00u);
	lead = (unsigned)((uint32_t)s * 14267u >> 23);
	tail = s - lead * 588u;
	vowel = (unsigned)((uint32_t)tail * 2341u >> 16);
	tail -= vowel * 28u;
	cell = ((unsigned)RD(b + LEAD_CLASS + lead) * 21u + vowel) * f->jset_tc + RD(b + TAIL_CLASS + tail);
	j[0] = lead;
	j[1] = vowel;
	j[2] = tail - 1;
	n = tail ? 3 : 2;
	/* NO_PIECE cuts to SVF_PIECE_BLANK */
	for (t = 0; t < n; ++t)
		out[t] = (uint16_t)slot_piece(f, t, cell_set(f, cell, t), j[t]);
	return (int)n;
}
#endif

int svf_jamo_pieces(const svf_face *f, uint32_t cp, uint16_t out[3])
{
	if (!out || !svf_is_jamo(f))
		return 0;
#if SVF_JMAP
	{
		/* the JMAP first (PR-5; a face without a usable one, jmap_off 0, goes straight to the JSET part); a miss is
		 * the JSET part's, when there is one */
		int n = f->jmap_off ? jmap_pieces(f, cp, out) : 0;

		if (n || !f->jset_lc)
			return n;
	}
#endif
#if !SVF_JSET_V1 && !SVF_JSET_V2
	return 0; /* the KS-only build: no JSET part is ever usable */
#else
#if SVF_JSET_V1 && SVF_JSET_V2
	/* the low byte of the version word decides: validation (jset_valid) accepts exactly 1 and 2, trusted included;
	 * a third version must change this test along with jset_valid */
	if (RD(f->data + f->jset_off + 4) != 1)
		return pieces_v2(f, cp, out);
#endif
#if SVF_JSET_V1
	return pieces_v1(f, cp, out);
#else
	return pieces_v2(f, cp, out);
#endif
#endif
}

int svf__composed(const svf_face *f, uint32_t cp)
{
	uint16_t p[3];

	if (!svf_is_jamo(f))
		return 0;
#if SVF_JMAP
	if (!f->jset_lc) /* a JMAP alone: only what it lists */
		return svf_jamo_pieces(f, cp, p) > 0;
#endif
	if (cp >= 0xAC00 && cp <= 0xD7A3)
		return 1;
	return svf_jamo_pieces(f, cp, p) > 0;
}

#if SVF_MERGE_WORDS && (SVF__MERGE_UNALIGNED || defined(__GNUC__))
#define SVF__MERGE_W 1
/* The larger 2-bit level of each of the 16 pairs of a and b: svf__merge's byte formula on a word. No bit moves across a
 * byte boundary into the result (gt << 1 and sel >> 1 land on bits the 0xAA mask drops or that are clear), so each byte
 * is exactly the byte formula's and the byte order does not matter. */
static uint32_t svf__max2w(uint32_t a, uint32_t b)
{
	uint32_t gt = a & ~b, eq = ~(a ^ b), sel = (gt | (eq & gt << 1)) & 0xAAAAAAAAu;

	sel |= sel >> 1;
	return (a & sel) | (b & ~sel);
}
#if SVF__MERGE_UNALIGNED
/* Any address is a word: every row of 4 bytes or more, no head. */
#define SVF__MERGE_MIN 4u
#define SVF__CONGRUENT(d, s) 1
#define SVF__HEAD(d) 0u
#define SVF__A4(p) (p)
#else
/* Strict alignment: runs of 8 bytes or more (a full-width piece; the rows of a boxed piece are mostly shorter and line up
 * by chance only), and only where the cell and piece bytes are congruent mod 4 (the phase of an aligned file makes a
 * full-width piece so in a 4-aligned cell), after the bytes up to the aligned cell byte; the compiler is told so (plain
 * word loads and stores, no memcpy call). */
#define SVF__MERGE_MIN 8u
#define SVF__CONGRUENT(d, s) (!(((uintptr_t)(d) ^ (uintptr_t)(s)) & 3u))
#define SVF__HEAD(d) ((unsigned)(-(uintptr_t)(d) & 3u))
#define SVF__A4(p) __builtin_assume_aligned((p), 4)
#endif

/* svf__merge of 1 bpp (@p two 0) or 2 bpp (@p two 1) rows with words (n >= SVF__MERGE_MIN; face memory read plainly,
 * the caller has checked that it may be). Each row from k = 0: a byte while k is below e (the head up to the aligned
 * cell byte, or the whole row when it has no words) or fewer than 4 bytes are left, else a word; one merge for both (a
 * byte is a word with three clear bytes, which neither OR nor svf__max2w lets into the result), skipped when the
 * piece's bytes are 0. */
static void svf__merge_words(int two, uint8_t *dst, uint16_t dst_pitch, const uint8_t *src, uint16_t src_pitch,
                             unsigned n, uint8_t h)
{
	unsigned k, e, w;
	uint32_t a, b;

	while (h) {
		e = SVF__CONGRUENT(dst, src) ? SVF__HEAD(dst) : n;
		for (k = 0; k < n; k += w) {
			w = k >= e && k + 4 <= n ? 4 : 1;
			if (w == 4)
				memcpy(&b, SVF__A4(src + k), 4);
			else
				b = src[k];
			if (!b)
				continue;
			if (w == 4)
				memcpy(&a, SVF__A4(dst + k), 4);
			else
				a = dst[k];
			a = two ? svf__max2w(a, b) : a | b;
			if (w == 4)
				memcpy(SVF__A4(dst + k), &a, 4);
			else
				dst[k] = (uint8_t)a;
		}
		if (!--h)
			break;
		dst += dst_pitch, src += src_pitch;
	}
}
#else
#define SVF__MERGE_W 0
#endif

void svf__merge(const svf_face *f, uint8_t *dst, uint16_t dst_pitch, const uint8_t *src, uint16_t src_pitch, uint16_t n,
                uint8_t h)
{
	unsigned k;

#if SVF__MERGE_W
	/* Words read face memory plainly: only where every face's bytes are plain memory or the face is streamed (RAM). */
#ifdef SVF_RD8_PLAIN
	if (f->bpp != 8 && n >= SVF__MERGE_MIN) {
#else
	if (f->bpp != 8 && n >= SVF__MERGE_MIN && SVF__STREAMED(f)) {
#endif
		svf__merge_words(f->bpp == 2, dst, dst_pitch, src, src_pitch, n, h);
		return;
	}
#endif
	/* One loop per bpp around the rows: no call and no dispatch per row. */
	if (f->bpp == 1) {
		while (h) {
			for (k = 0; k < n; ++k)
				dst[k] |= SVF__RD8(f, src + k);
			if (!--h)
				break;
			dst += dst_pitch, src += src_pitch;
		}
	} else if (f->bpp == 8) {
		while (h) {
			for (k = 0; k < n; ++k) {
				uint8_t b = SVF__RD8(f, src + k);
				if (b > dst[k])
					dst[k] = b;
			}
			if (!--h)
				break;
			dst += dst_pitch, src += src_pitch;
		}
	} else {
		/* Four 2-bit levels a byte, the leftmost in the top bits: the larger of each pair, all four at once. In each
		 * pair's top bit: a's level is larger if its top bit is (gt), or the top bits are equal (eq) and its low bit
		 * is; that bit and the one below it then take a's pair, the others b's. */
		while (h) {
			for (k = 0; k < n; ++k) {
				uint8_t a = dst[k], b = SVF__RD8(f, src + k), gt, eq, sel;
				if (!b)
					continue;
				gt = (uint8_t)(a & ~b);
				eq = (uint8_t)~(a ^ b);
				sel = (uint8_t)((gt | (eq & gt << 1)) & 0xAA);
				sel = (uint8_t)(sel | sel >> 1);
				dst[k] = (uint8_t)((a & sel) | (b & ~sel));
			}
			if (!--h)
				break;
			dst += dst_pitch, src += src_pitch;
		}
	}
}

svf_err svf_compose(const svf_face *f, uint32_t cp, uint8_t *cell, size_t cell_len)
{
	uint16_t pieces[3];
	int n, i;

	if (!f || (!cell && cell_len))
		return SVF_E_ARG;
	n = svf_jamo_pieces(f, cp, pieces);
	if (!n)
		return SVF_E_RANGE;
	/* A streamed face without a cache reads each piece into the second half of the cell. */
	if (!cell || cell_len < f->stride || (SVF__STREAMED(f) && !f->slots && cell_len / 2 < f->stride))
		return SVF_E_NOMEM;
	memset(cell, 0, f->stride);
	for (i = 0; i < n; ++i) {
		int32_t g;
		svf_err e;

		if (pieces[i] == SVF_PIECE_BLANK)
			continue;
		/* Every piece number has a glyph (checked at open). */
		g = svf__glyph_index(f, SVF_PIECE_BASE + pieces[i]);
		if (g < 0)
			continue;
		e = svf__expand(f, (uint16_t)g, cell, cell + f->stride, 1);
		if (e != SVF_OK) {
			memset(cell, 0, f->stride);
			return e;
		}
	}
	svf__mask_cell(f, cell);
	return SVF_OK;
}
