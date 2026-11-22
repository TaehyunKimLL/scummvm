/* svf_face.c - open an SVFN v3 face (mapped here; svf_stream.c reuses both halves of open): header, layout and
 * offset validation, code point lookup, metrics, glyph records expanded into the caller's cell.
 *
 * Validation follows jamoset.parse_svfn and the engine's HiResBitmapFont::readLayout (spec/SVF.md): magic, version 3,
 * the layout bytes and the build's supported set, the flag bits (only 0..2 may be set), bpp, a non-empty glyph box,
 * glyph data, code point table, glyph offset table and a JSET block with a size inside the file and after the header,
 * the alignment masks and the offset table, every glyph index below the glyph count, (mapped) every record; a metrics
 * table outside the file or inside the header is dropped (the glyphs stay) and an invalid dy range is ignored (dy 0
 * for every glyph), each with a status bit. A JSET block that fails validation, whose size disagrees with its counts
 * or is 0 with the flag set, leaves a plain face (SVF_FACE_JSET_BAD);
 * only a JSET range outside the file or inside the header is SVF_E_RANGE.
 */
#include "svf_internal.h"

#include <string.h>

enum {
	HEADER_V3 = SVF__HEADER_V3,
	CMAP_ENTRY = SVF__CMAP_ENTRY,
	METRICS_ENTRY = SVF__METRICS_ENTRY
};

/* ---- open ------------------------------------------------------------------------------------------------------- */

/* Non-zero if [off, off + size) lies inside a file of @p len bytes. */
static int fits(uint64_t off, uint64_t size, uint64_t len)
{
	return off <= len && size <= len - off;
}

size_t svf_layouts(const uint16_t **set)
{
#ifdef SVF_LAYOUTS
	if (set)
		*set = svf__layouts;
	return sizeof svf__layouts / sizeof svf__layouts[0];
#else
	if (set)
		*set = NULL;
	return 0;
#endif
}

/* Non-zero if the build reads a face with these units (log2). */
static int layout_ok(unsigned rec, unsigned row, unsigned tab)
{
#ifdef SVF_LAYOUTS
	size_t i;

	for (i = 0; i < sizeof svf__layouts / sizeof svf__layouts[0]; ++i)
		if (svf__layouts[i] == SVF_LAYOUT(rec, row, tab))
			return 1;
	return 0;
#else
	(void)rec;
	(void)row;
	(void)tab;
	return 1;
#endif
}

svf_err svf__open_header(svf_face *f, uint32_t len)
{
	uint32_t metrics_off, data_off, data_size, cmap_off, goff_off, jset_off, jset_size, tm, rm;
	unsigned b9, b10, rec, row, tab;
	int dy_min, dy_max, metrics_ok;

	if (len < 4 || rd8(f, 0) != 'S' || rd8(f, 1) != 'V' || rd8(f, 2) != 'F' || rd8(f, 3) != 'N')
		return SVF_E_MAGIC;
	if (len < 6)
		return SVF_E_RANGE;
	if (rd16(f, 4) != 3)
		return SVF_E_VERSION;
	if (len < HEADER_V3)
		return SVF_E_RANGE;

	b9 = rd8(f, 9);
	b10 = rd8(f, 10);
	rec = b9 & 7;
	row = b9 >> 3 & 7;
	tab = b10 & 7;
	/* Layout bytes: units 0..6 (log2), ROW not above REC, reserved bits 0; then the build's supported set. */
	if ((b9 & 0xC0) || (b10 & 0xF0) || rec > 6 || row > 6 || tab > 6 || row > rec)
		return SVF_E_LAYOUT;
	if (!layout_ok(rec, row, tab))
		return SVF_E_LAYOUT;
	f->layout = (uint8_t)(rec | row << 3 | ((b10 & 8) ? SVF__LAYOUT_U32 : 0));

	f->flags = rd16(f, 6);
	/* Header flag bits other than 0..2 are 0 (spec/SVF.md): a face with any other bit set is a malformed header. */
	if (f->flags & ~(uint16_t)(SVF_FLAG_METRICS | SVF_FLAG_JAMO | SVF_FLAG_MARKS_AT_ORIGIN))
		return SVF_E_LAYOUT;
	f->bpp = rd8(f, 8);
	f->count = rd16(f, 12);
	f->cell_w = rd8(f, 14);
	f->cell_h = rd8(f, 15);
	f->ascent = rd8(f, 16);
	metrics_off = rd32(f, 20);
	data_off = rd32(f, 24);
	data_size = rd32(f, 28);
	cmap_off = rd32(f, 32);

	if (f->bpp != 1 && f->bpp != 2 && f->bpp != 8)
		return SVF_E_BPP;
	if (!f->cell_w || !f->cell_h || !f->count)
		return SVF_E_RANGE;
	f->pitch = (uint8_t)(((unsigned)f->cell_w * f->bpp + 7) / 8);
	/* At most 255 * 255; computed in 32 bits, as an int product could overflow where int is 16 bits (AVR). */
	f->stride = (uint16_t)((uint32_t)f->pitch * f->cell_h);

	/* Every table starts after the header. */
	jset_off = rd32(f, 36);
	jset_size = rd32(f, 40);
	goff_off = rd32(f, 44);
	if (data_off < HEADER_V3 || !fits(data_off, data_size, len))
		return SVF_E_RANGE;
	if (cmap_off < HEADER_V3 || !fits(cmap_off, ((uint64_t)f->count << 3), len))
		return SVF_E_RANGE;
	if (goff_off < HEADER_V3 ||
	    !fits(goff_off, (f->layout & SVF__LAYOUT_U32) ? ((uint64_t)f->count + 1) << 2 : ((uint64_t)f->count + 1) << 1,
	         len))
		return SVF_E_RANGE;
	/* A JSET range is checked only when there is a block to read: size 0 with the flag is a plain face. */
	if ((f->flags & SVF_FLAG_JAMO) && jset_size && (jset_off < HEADER_V3 || !fits(jset_off, jset_size, len)))
		return SVF_E_RANGE;
	/* The alignment masks: the five offsets of bytes 20..47 to the table unit (the glyph data also to the record
	 * unit), the data size to the record unit. */
	tm = ((uint32_t)1 << SVF__TAB(f)) - 1;
	rm = ((uint32_t)1 << SVF__REC(f)) - 1;
	if (((metrics_off | cmap_off | jset_off | goff_off) & tm) || (data_off & (tm | rm)) || (data_size & rm))
		return SVF_E_LAYOUT;

	metrics_ok = 0;
	if (f->flags & SVF_FLAG_METRICS) {
		if (metrics_off >= HEADER_V3 && fits(metrics_off, ((uint64_t)f->count << 2), len))
			metrics_ok = 1;
		else
			f->status |= SVF_FACE_METRICS_DROPPED;
	}

	dy_min = (int8_t)rd8(f, 17);
	dy_max = (int8_t)rd8(f, 18);
	if ((dy_min || dy_max) &&
	    (!metrics_ok || dy_min > 0 || dy_max < 0 || -dy_min > f->cell_h || dy_max > f->cell_h)) {
		f->status |= SVF_FACE_DY_IGNORED;
		dy_min = dy_max = 0;
	}
	f->dy_min = (int8_t)dy_min;
	f->dy_max = (int8_t)dy_max;

	f->cmap_off = cmap_off;
	f->metrics_off = metrics_off;
	f->data_off = data_off;
	f->goff_off = goff_off;

	if (f->flags & SVF_FLAG_JAMO) {
		/* The JSET block is where the header says (a size past 0xFFFF is no valid block: the clip keeps it above
		 * SVF__JSET_LOAD_MAX). */
		f->jset_off = jset_off;
		f->jset_len = (uint16_t)(jset_size > 0xFFFFu ? 0xFFFFu : jset_size);
	}
	return SVF_OK;
}

/* The glyph offset table entry @p i in bytes from the glyph data. */
static uint32_t goff_entry(const svf_face *f, uint32_t i)
{
	if (f->layout & SVF__LAYOUT_U32)
		return rd32(f, f->goff_off + 4 * i);
	return (uint32_t)rd16(f, f->goff_off + 2 * i) << SVF__REC(f);
}

svf_err svf__record(const svf_face *f, uint16_t g, uint32_t *at, uint32_t *len)
{
	uint32_t a, b;

	if (g >= f->count)
		return SVF_E_RANGE;
	a = goff_entry(f, g);
	b = goff_entry(f, (uint32_t)g + 1);
	*at = f->data_off + a;
	*len = b - a;
	return SVF_OK;
}

svf_err svf__rec_parse(const svf_face *f, const uint8_t *h, uint32_t len, svf__rec_t *r)
{
	unsigned row = SVF__ROW(f);

	if (len < 4)
		return SVF_E_RANGE;
	r->x0 = h[0];
	r->y0 = h[1];
	r->wb = h[2];
	r->h = h[3];
	r->p = 4;
	r->rp = 0;
	if ((unsigned)r->x0 + r->wb > f->pitch || (unsigned)r->y0 + r->h > f->cell_h)
		return SVF_E_RANGE;
	if (!r->wb || !r->h) {
		r->h = 0; /* blank */
		return SVF_OK;
	}
	r->rp = (uint16_t)((r->wb + (1u << row) - 1) & ~((1u << row) - 1));
	/* The phase: a full-width record of an aligned file starts its rows so they sit where they go in the cell, mod 4. */
	if (SVF__REC(f) >= 2 && !row && r->wb == f->pitch)
		r->p = (uint16_t)(r->p + (uint16_t)(((uint32_t)r->y0 * f->pitch) & 3));
	if ((uint32_t)r->p + (uint32_t)r->h * r->rp > len)
		return SVF_E_RANGE;
	return SVF_OK;
}

/* A mapped record's header and box, checked. */
static svf_err mapped_record(const svf_face *f, uint16_t g, uint32_t *at, svf__rec_t *r)
{
	uint8_t h[4];
	uint32_t len;
	svf_err e = svf__record(f, g, at, &len);
	unsigned i;

	if (e != SVF_OK)
		return e;
	if (len < 4)
		return SVF_E_RANGE;
	for (i = 0; i < 4; ++i)
		h[i] = rd8(f, *at + i);
	return svf__rec_parse(f, h, len, r);
}

svf_err svf__open_tables(svf_face *f, uint32_t data_size)
{
	uint32_t prev = 0, i, mask;
	uint16_t k;

	for (k = 0; k < f->count; ++k) {
		uint32_t cp = rd32(f, f->cmap_off + (uint32_t)k * CMAP_ENTRY);
		if (rd32(f, f->cmap_off + (uint32_t)k * CMAP_ENTRY + 4) >= f->count)
			return SVF_E_RANGE;
		if (k && cp <= prev)
			f->status |= SVF_FACE_CMAP_LINEAR;
		prev = cp;
	}
	/* The offset table: count + 1 entries, each a multiple of the record unit, none below the one before, the
	 * last the glyph data size. */
	mask = ((uint32_t)1 << SVF__REC(f)) - 1;
	prev = 0;
	for (i = 0; i <= f->count; ++i) {
		uint32_t o = goff_entry(f, i);
		if ((o & mask) || o < prev)
			return SVF_E_LAYOUT;
		prev = o;
	}
	if (prev != data_size)
		return SVF_E_LAYOUT;
	/* A mapped face has its records checked here; a streamed one when a record is read. */
	if (!SVF__STREAMED(f))
		for (k = 0; k < f->count; ++k) {
			svf__rec_t r;
			uint32_t at;
			svf_err e = mapped_record(f, k, &at, &r);
			if (e != SVF_OK)
				return e;
		}
	if (f->flags & SVF_FLAG_JAMO)
		svf__jset_check(f);
	return SVF_OK;
}

svf_err svf_open_mapped(svf_face *f, const uint8_t *data, size_t len)
{
	svf_err e;

	if (!f)
		return SVF_E_ARG;
	memset(f, 0, sizeof *f);
	if (!data && len)
		return SVF_E_ARG;
	/* Offsets are u32 and so is every sum of them: a file must fit in 32 bits (no repo face is near it). On a 16- or
	 * 32-bit size_t the cast constant is SIZE_MAX and the test is never true. */
	if (len > (size_t)0xFFFFFFFFu)
		return SVF_E_RANGE;
	f->data = data;
	e = svf__open_header(f, (uint32_t)len);
	if (e == SVF_OK)
		e = svf__open_tables(f, rd32(f, 28));
	if (e != SVF_OK)
		memset(f, 0, sizeof *f); /* a face that failed to open holds nothing */
	return e;
}

int svf__has_metrics(const svf_face *f)
{
	return (f->flags & SVF_FLAG_METRICS) && !(f->status & SVF_FACE_METRICS_DROPPED);
}

/* ---- accessors ------------------------------------------------------------------------------------------------- */

/* A NULL face reads as a zeroed one (what a failed open leaves): every accessor 0. */
uint8_t svf_bpp(const svf_face *f) { return f ? f->bpp : 0; }
uint8_t svf_cell_w(const svf_face *f) { return f ? f->cell_w : 0; }
uint8_t svf_cell_h(const svf_face *f) { return f ? f->cell_h : 0; }
uint8_t svf_ascent(const svf_face *f) { return f ? f->ascent : 0; }
uint16_t svf_flags(const svf_face *f) { return f ? f->flags : 0; }
uint16_t svf_status(const svf_face *f) { return f ? f->status : 0; }
uint16_t svf_glyph_count(const svf_face *f) { return f ? f->count : 0; }

size_t svf_cell_bytes(const svf_face *f)
{
	return f ? f->stride : 0;
}

uint32_t svf_cmap_cp(const svf_face *f, uint16_t i)
{
	return f && i < f->count ? rd32(f, f->cmap_off + (uint32_t)i * CMAP_ENTRY) : 0xFFFFFFFFu;
}

/* ---- lookup ---------------------------------------------------------------------------------------------------- */

/* The glyph index of @p cp, or -1. Binary search over a strictly increasing table; otherwise (unsorted, or a code point
 * listed twice) a scan from the end, so the LAST entry for a code point wins, as in jamoset.parse_svfn. */
int32_t svf__glyph_index(const svf_face *f, uint32_t cp)
{
	uint32_t at;

	if (f->status & SVF_FACE_CMAP_LINEAR) {
		uint16_t i;
		for (i = f->count; i-- > 0;) {
			at = f->cmap_off + (uint32_t)i * CMAP_ENTRY;
			if (rd32(f, at) == cp)
				return (int32_t)rd32(f, at + 4);
		}
	} else {
		uint16_t lo = 0, hi = f->count;
		while (lo < hi) {
			uint16_t mid = SVF__CMAP_MID(lo, hi, unsigned);
			uint32_t v;
			at = f->cmap_off + (uint32_t)mid * CMAP_ENTRY;
			v = rd32(f, at);
			if (v == cp)
				return (int32_t)rd32(f, at + 4);
			if (v < cp)
				lo = (uint16_t)(mid + 1);
			else
				hi = mid;
		}
	}
	return -1;
}

int svf_has(const svf_face *f, uint32_t cp)
{
	if (!f)
		return 0;
	return svf__composed(f, cp) || svf__glyph_index(f, cp) >= 0;
}

int svf_metrics_of(const svf_face *f, uint32_t cp, svf_metrics *m)
{
	int32_t g;
	uint32_t at;
	int dy;

	if (!f || !m)
		return -1;
	if (svf__composed(f, cp)) {
		/* A composed glyph fills its cell from the pen (the engine's JamoGlyphSource::metrics). */
		m->advance = f->jset_adv ? f->jset_adv : f->cell_w;
		m->bearing_x = 0;
		m->ink_w = f->cell_w;
		m->dy = 0;
		return 1;
	}
	g = svf__glyph_index(f, cp);
	if (g < 0)
		return -1;
	if (!svf__has_metrics(f)) {
		m->advance = f->cell_w;
		m->bearing_x = 0;
		m->ink_w = f->cell_w;
		m->dy = 0;
		return 0;
	}
	at = f->metrics_off + (uint32_t)g * METRICS_ENTRY;
	m->advance = rd8(f, at);
	m->bearing_x = (int8_t)rd8(f, at + 1);
	m->ink_w = rd8(f, at + 2);
	dy = (int8_t)rd8(f, at + 3);
	m->dy = (int8_t)(dy >= f->dy_min && dy <= f->dy_max ? dy : 0);
	return 1;
}

void svf__mask_cell(const svf_face *f, uint8_t *cell)
{
	unsigned used = ((unsigned)f->cell_w * f->bpp) % 8;
	uint8_t y;

	if (!used)
		return;
	for (y = 0; y < f->cell_h; ++y)
		cell[(uint16_t)((uint16_t)y * f->pitch + f->pitch - 1)] &= (uint8_t)(0xFFu << (8 - used));
}

void svf__put_rows(const svf_face *f, uint8_t *cell, const uint8_t *rows, const svf__rec_t *r, int merge)
{
	/* unsigned (the int width) counters and strides: no truncation in the loops; every value fits 16 bits */
	unsigned h = r->h, pitch = f->pitch, n = r->wb, rp = r->rp, k;
	uint8_t *dst = cell + (uint16_t)(r->y0 * pitch) + r->x0;

	if (n == pitch && rp == n) {
		/* Full width: one run of pitch * h bytes. */
		n *= h;
		h = 1;
	}
	if (merge) {
		if (f->bpp != 1) {
			svf__merge(f, dst, (uint16_t)pitch, rows, (uint16_t)rp, (uint16_t)n, (uint8_t)h);
			return;
		}
		/* 1 bpp, the common case: the OR in place, no call; a one-byte column (a narrow jamo piece) without the
		 * inner loop. */
		/* The pointers advance between rows, never after the last: none ends past one past the end of its cell. */
		if (n == 1)
			while (h) {
				*dst |= SVF__RD8(f, rows);
				if (!--h)
					break;
				dst += pitch, rows += rp;
			}
		else
			while (h) {
				for (k = 0; k < n; ++k)
					dst[k] |= SVF__RD8(f, rows + k);
				if (!--h)
					break;
				dst += pitch, rows += rp;
			}
		return;
	}
	/* A copy, row by row (SVF__RD8 reads a streamed face's RAM plainly); a narrow row without a call. */
	while (h) {
#ifdef SVF_RD8_PLAIN
		if (n > 4)
			memcpy(dst, rows, n);
		else
			for (k = 0; k < n; ++k)
				dst[k] = SVF__RD8(f, rows + k);
#else
		if (SVF__STREAMED(f)) /* a streamed face's rows are RAM: a plain copy whatever SVF_RD8 is */
			memcpy(dst, rows, n);
		else
			for (k = 0; k < n; ++k)
				dst[k] = SVF__RD8(f, rows + k);
#endif
		if (!--h)
			break;
		dst += pitch, rows += rp;
	}
}

svf_err svf__expand(const svf_face *f, uint16_t g, uint8_t *cell, uint8_t *scratch, int merge)
{
	svf__rec_t r;
	uint32_t at;
	unsigned row;

#ifndef SVF_NO_STREAM
	if (f->rd) {
		uint32_t len;
		svf_err e = svf__record(f, g, &at, &len);
		return e != SVF_OK ? e : svf__stream_expand(f, at, len, cell, scratch, merge);
	}
#endif
	(void)scratch;
	/* A mapped face had every record checked at open (svf__open_tables): its header is read here without the checks
	 * of svf__rec_parse, which would give the same box. */
	if (g >= f->count)
		return SVF_E_RANGE;
	/* goff_entry, written out: -Os keeps that one a call */
	if (f->layout & SVF__LAYOUT_U32)
		at = rd32(f, f->goff_off + 4 * (uint32_t)g);
	else
		at = (uint32_t)rd16(f, f->goff_off + 2 * (uint32_t)g) << SVF__REC(f);
	at += f->data_off;
	r.wb = rd8(f, at + 2);
	r.h = rd8(f, at + 3);
	if (!r.wb || !r.h)
		return SVF_OK; /* blank */
	r.x0 = rd8(f, at);
	r.y0 = rd8(f, at + 1);
	row = SVF__ROW(f);
	r.rp = (uint16_t)((r.wb + (1u << row) - 1) & ~((1u << row) - 1));
	r.p = 4;
	if (SVF__REC(f) >= 2 && !row && r.wb == f->pitch)
		r.p = (uint16_t)(r.p + (uint16_t)(((uint32_t)r.y0 * f->pitch) & 3));
	svf__put_rows(f, cell, f->data + at + r.p, &r, merge);
	return SVF_OK;
}

svf_err svf_glyph(const svf_face *f, uint32_t cp, uint8_t *cell, size_t cell_len)
{
	int32_t g;
	svf_err e;

	if (!f || (!cell && cell_len))
		return SVF_E_ARG;
	if (svf__composed(f, cp))
		return svf_compose(f, cp, cell, cell_len);
	g = svf__glyph_index(f, cp);
	if (g < 0)
		return SVF_E_RANGE;
	if (!cell || cell_len < f->stride)
		return SVF_E_NOMEM;
	memset(cell, 0, f->stride);
	e = svf__expand(f, (uint16_t)g, cell, NULL, 0);
	if (e != SVF_OK) {
		memset(cell, 0, f->stride);
		return e;
	}
	svf__mask_cell(f, cell);
	return SVF_OK;
}
