/* svf_stream.c - open a face through a read callback; glyph rows through an optional block cache.
 *
 * Open reads the header (its first min(file_len, 48) bytes), validates it with the mapped open's header half against
 * file_len, reads the code point table, a usable metrics table, the JSET region and (v3) the glyph offset table into the
 * caller's tables buffer, rebases their offsets into it and validates them with the mapped open's tables half. From then
 * on every table lookup reads RAM exactly as a mapped face does; only glyph records come through the callback, each
 * checked when read.
 *
 * Tables buffer: u32 reads (callback calls), u32 cache clock, then the code point table, the metrics table, the JSET
 * region and the glyph offset table. The face struct is const where glyphs are drawn, so this mutable state lives in
 * caller memory.
 *
 * Cache: slots tags (u32 block number, u32 LRU stamp; little-endian) at the front, then slots blocks of block_size
 * bytes. Block b holds the file bytes [b * block_size, b * block_size + block_size), cut at the end of the glyph data
 * (data_off + data_size).
 * A slot being refilled is untagged before the read, so a failed or short read leaves it empty.
 */
#include "svf_internal.h"

#ifndef SVF_NO_STREAM

#include <string.h>

enum {
	HEADER_V3 = SVF__HEADER_V3,
	STATE = SVF__STREAM_STATE,
	TAG = 8,
	MIN_BLOCK_SHIFT = 6,  /* 64 */
	MAX_BLOCK_SHIFT = 12, /* 4096 */
	MIN_SLOTS = 2,
	MAX_SLOTS = 0xFFFF
};

#define NO_BLOCK 0xFFFFFFFFu

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* The stream state at the front of the tables buffer (the caller's, writable: f->data is const only to share the
 * mapped lookup code). */
static uint8_t *state(const svf_face *f)
{
	return (uint8_t *)(uintptr_t)f->data;
}

/* One callback call of exactly @p len bytes, counted; 1 if it delivered them all. */
static int read_all(const svf_face *f, uint32_t off, uint8_t *dst, uint32_t len)
{
	uint8_t *st = state(f);
	put32(st, get32(st) + 1);
	return f->rd(f->ctx, off, dst, len) == (long)len;
}

/* Reads the header into @p hdr (zero past file_len) and validates it into @p f (zeroed, f->data = hdr). */
static svf_err open_header(svf_face *f, svf_read_fn rd, void *ctx, uint32_t file_len, uint8_t hdr[HEADER_V3])
{
	uint32_t n = file_len < HEADER_V3 ? file_len : HEADER_V3;

	memset(f, 0, sizeof *f);
	memset(hdr, 0, HEADER_V3);
	if (n && rd(ctx, 0, hdr, n) != (long)n)
		return SVF_E_IO;
	f->data = hdr;
	f->rd = rd; /* the header is RAM: read with plain loads (svf_internal.h) */
	return svf__open_header(f, file_len);
}

/* Bytes of the code point table, the metrics table and the JSET region a validated header names. A JSET region longer
 * than SVF__JSET_LOAD_MAX (no valid block is) is loaded only to its 16-byte header (svf__jset_check reads no further
 * before refusing it). */
static uint32_t cmap_bytes(const svf_face *f)
{
	return (uint32_t)f->count * SVF__CMAP_ENTRY;
}

static uint32_t metrics_bytes(const svf_face *f)
{
	return svf__has_metrics(f) ? (uint32_t)f->count * SVF__METRICS_ENTRY : 0;
}

/* The JSET bytes the tables buffer holds: the whole region, or only its 16-byte header when the region is longer than
 * SVF__JSET_LOAD_MAX, which depends on the reader build (65,534 with SVF_JMAP; else 12,321 with the version 1 reader, 9,246
 * without; with SVF_JSET_BIG 47,373 and 45,068): the same
 * face needs a different buffer size per build (svf_stream_tables_size). */
static uint32_t jset_bytes(const svf_face *f)
{
	if (!(f->flags & SVF_FLAG_JAMO))
		return 0;
	if (f->jset_len <= SVF__JSET_LOAD_MAX)
		return f->jset_len;
	return SVF__JSET_HEADER;
}

/* The glyph offset table: count + 1 entries of 2 or 4 bytes. */
static uint32_t goff_bytes(const svf_face *f)
{
	return ((uint32_t)f->count + 1) * ((f->layout & SVF__LAYOUT_U32) ? 4 : 2);
}

static uint32_t tables_need(const svf_face *f)
{
	return STATE + cmap_bytes(f) + metrics_bytes(f) + jset_bytes(f) + goff_bytes(f);
}

size_t svf_stream_tables_size(svf_read_fn rd, void *ctx, uint32_t file_len, svf_err *err)
{
	svf_face f;
	uint8_t hdr[HEADER_V3];
	svf_err e = rd ? open_header(&f, rd, ctx, file_len, hdr) : SVF_E_ARG;
	uint32_t need = e == SVF_OK ? tables_need(&f) : 0;

	/* At most 8 + 65535 * 12 + SVF__JSET_LOAD_MAX + 65536 * 4: beyond a 16-bit size_t only (the mask is 0 for a wider
	 * one). */
	if (e == SVF_OK && (need & ~(uint32_t)SVF__SIZE_MAX)) {
		e = SVF_E_NOMEM;
		need = 0;
	}
	if (err)
		*err = e;
	return (size_t)need;
}

/* Reads @p len bytes at file offset @p from into the tables at @p to; a table of 0 bytes is not read. */
static int load(svf_face *f, uint32_t from, uint32_t to, uint32_t len)
{
	return !len || read_all(f, from, (uint8_t *)(uintptr_t)f->data + to, len);
}

/* Reads the tables of a face whose header is validated into its tables buffer and rebases their offsets. */
static svf_err load_tables(svf_face *f)
{
	uint32_t at = STATE;

	if (!load(f, f->cmap_off, at, cmap_bytes(f)))
		return SVF_E_IO;
	f->cmap_off = at;
	at += cmap_bytes(f);
	if (metrics_bytes(f)) {
		if (!load(f, f->metrics_off, at, metrics_bytes(f)))
			return SVF_E_IO;
		f->metrics_off = at;
		at += metrics_bytes(f);
	}
	if (f->flags & SVF_FLAG_JAMO) {
		if (!load(f, f->jset_off, at, jset_bytes(f)))
			return SVF_E_IO;
		f->jset_off = at;
		at += jset_bytes(f);
	}
	if (goff_bytes(f)) {
		if (!load(f, f->goff_off, at, goff_bytes(f)))
			return SVF_E_IO;
		f->goff_off = at;
	}
	return SVF_OK;
}

svf_err svf_open_stream(svf_face *f, svf_read_fn rd, void *ctx, uint32_t file_len, uint8_t *tables, size_t tables_len,
                        uint8_t *cache, size_t cache_len, uint16_t block_size)
{
	uint8_t hdr[HEADER_V3];
	uint8_t shift = 0;
	size_t slots = 0, i;
	svf_err e;

	if (!f)
		return SVF_E_ARG;
	memset(f, 0, sizeof *f);
	if (!rd || (!tables && tables_len) || (!cache && cache_len))
		return SVF_E_ARG;
	if (cache_len) {
		while (shift <= MAX_BLOCK_SHIFT && (1u << shift) != block_size)
			++shift;
		if (shift < MIN_BLOCK_SHIFT || shift > MAX_BLOCK_SHIFT)
			return SVF_E_ARG;
		slots = cache_len / ((size_t)block_size + TAG);
		if (slots < MIN_SLOTS)
			return SVF_E_ARG;
		if (slots > MAX_SLOTS)
			slots = MAX_SLOTS;
	}

	e = open_header(f, rd, ctx, file_len, hdr);
	if (e == SVF_OK && tables_len < tables_need(f))
		e = SVF_E_NOMEM;
	if (e != SVF_OK) {
		memset(f, 0, sizeof *f);
		return e;
	}

	f->data = tables;
	f->rd = rd;
	f->ctx = ctx;
	put32(tables, 1); /* the header read */
	put32(tables + 4, 0);
	if (slots) {
		f->cache = cache;
		f->slots = (uint16_t)slots;
		f->block_shift = shift;
		for (i = 0; i < slots; ++i) {
			put32(cache + i * TAG, NO_BLOCK);
			put32(cache + i * TAG + 4, 0);
		}
	}
	e = load_tables(f);
	if (e == SVF_OK)
		e = svf__open_tables(f, get32(hdr + 28));
	if (e != SVF_OK)
		memset(f, 0, sizeof *f); /* a face that failed to open holds nothing */
	return e;
}

uint32_t svf_stream_reads(const svf_face *f)
{
	return f && f->rd ? get32(f->data) : 0;
}

/* The next LRU stamp. On wrap every held slot restarts at 1: the order is lost once, the bytes stay right. */
static uint32_t tick(const svf_face *f)
{
	uint8_t *st = state(f);
	uint32_t c = get32(st + 4) + 1;
	uint16_t i;

	if (c == 0) {
		for (i = 0; i < f->slots; ++i)
			if (get32(f->cache + (size_t)i * TAG) != NO_BLOCK)
				put32(f->cache + (size_t)i * TAG + 4, 1);
		c = 2;
	}
	put32(st + 4, c);
	return c;
}

/* The file offset one past the last byte of glyph data: the last offset table entry. */
static uint32_t data_end(const svf_face *f)
{
	uint32_t at, len;

	if (svf__record(f, (uint16_t)(f->count - 1), &at, &len) != SVF_OK)
		return f->data_off;
	return at + len;
}

/* The bytes of block @p b in the cache, read into the least recently used slot on a miss; NULL if that read fails. */
static const uint8_t *block(const svf_face *f, uint32_t b)
{
	uint8_t *blocks = f->cache + (size_t)f->slots * TAG, *tag;
	uint32_t size = 1u << f->block_shift, start = b << f->block_shift, end, oldest = 0xFFFFFFFFu;
	uint16_t i, victim = 0;

	/* A hit first, reading the tags only; on a miss the least recently used slot (an empty slot has stamp 0, so it is
	 * taken before any held one). */
	for (i = 0; i < f->slots; ++i)
		if (get32(f->cache + (size_t)i * TAG) == b) {
			put32(f->cache + (size_t)i * TAG + 4, tick(f));
			return blocks + ((size_t)i << f->block_shift);
		}
	for (i = 0; i < f->slots; ++i) {
		uint32_t stamp = get32(f->cache + (size_t)i * TAG + 4);
		if (stamp < oldest) {
			oldest = stamp;
			victim = i;
		}
	}

	/* The glyph data ends inside the file (validated at open); a block stops there. */
	end = data_end(f);
	if (end - start < size)
		size = end - start;
	tag = f->cache + (size_t)victim * TAG;
	put32(tag, NO_BLOCK);
	put32(tag + 4, 0);
	if (!read_all(f, start, blocks + ((size_t)victim << f->block_shift), size))
		return NULL;
	put32(tag, b);
	put32(tag + 4, tick(f));
	return blocks + ((size_t)victim << f->block_shift);
}

svf_err svf__stream_bytes(const svf_face *f, uint32_t at, uint8_t *dst, uint32_t n, int merge)
{
	uint32_t k = 0;

	if (!f->slots)
		return read_all(f, at, dst, n) ? SVF_OK : SVF_E_IO;
	while (k < n) {
		uint32_t off = at + k, o = off & ((1u << f->block_shift) - 1), m = (1u << f->block_shift) - o;
		const uint8_t *p = block(f, off >> f->block_shift);

		if (!p)
			return SVF_E_IO;
		if (m > n - k)
			m = n - k;
		if (merge)
			svf__merge(f, dst + k, 0, p + o, 0, (uint16_t)m, 1);
		else
			memcpy(dst + k, p + o, m);
		k += m;
	}
	return SVF_OK;
}

/* The rows of @p r, read to row 0 at @p rows in the cell (the copy case, the first row of the record at cell offset
 * @p src), moved to their places in the same cell: rows whose source lies at or after their place first, in order, the
 * others after them from the last, so no row is written over one not yet moved (rows are at least wb apart on both
 * sides and wb <= pitch). Then everything that is not a row is cleared. */
static void place_rows(uint8_t *cell, uint16_t stride, uint16_t src, const svf__rec_t *r, uint16_t pitch)
{
	uint16_t d0 = (uint16_t)(r->y0 * pitch + r->x0), n, row;

	if (r->wb == pitch && r->rp == r->wb) { /* one run of pitch * h bytes */
		n = (uint16_t)(r->h * pitch);
		if (src != d0)
			memmove(cell + d0, cell + src, n);
		memset(cell, 0, d0);
		memset(cell + d0 + n, 0, stride - d0 - n);
		return;
	}
	for (row = 0; row < r->h; ++row)
		if ((uint16_t)(src + row * r->rp) >= (uint16_t)(d0 + row * pitch))
			memmove(cell + d0 + row * pitch, cell + src + row * r->rp, r->wb);
	for (row = r->h; row-- > 0;)
		if ((uint16_t)(src + row * r->rp) < (uint16_t)(d0 + row * pitch))
			memmove(cell + d0 + row * pitch, cell + src + row * r->rp, r->wb);
	memset(cell, 0, d0);
	for (row = 0; row + 1 < r->h; ++row)
		memset(cell + d0 + row * pitch + r->wb, 0, pitch - r->wb);
	n = (uint16_t)(d0 + (uint16_t)(r->h - 1) * pitch + r->wb);
	memset(cell + n, 0, stride - n);
}

svf_err svf__stream_expand(const svf_face *f, uint32_t at, uint32_t len, uint8_t *cell, uint8_t *scratch, int merge)
{
	uint16_t stride = f->stride, pitch = f->pitch, n, row;
	uint8_t head[4], *buf = merge ? scratch : cell, *rows;
	svf__rec_t r;
	svf_err e;

	if (f->slots) {
		/* Through the cache: a block is looked up once for the record's header and once for each further block its
		 * rows lie in (a record nearly always lies in one block). A pointer into a block is good until the next
		 * lookup, which may refill any other slot. */
		uint32_t mask = ((uint32_t)1 << f->block_shift) - 1, o = at & mask, cur;
		const uint8_t *p;

		if (len < 4)
			return SVF_E_RANGE;
		if (o + 4 <= mask + 1) {
			cur = at >> f->block_shift;
			p = block(f, cur);
			if (!p)
				return SVF_E_IO;
			e = svf__rec_parse(f, p + o, len, &r);
			if (e != SVF_OK || !r.h)
				return e;
			if (o + len <= mask + 1) {
				svf__put_rows(f, cell, p + o + r.p, &r, merge);
				return SVF_OK;
			}
		} else {
			e = svf__stream_bytes(f, at, head, 4, 0);
			if (e == SVF_OK)
				e = svf__rec_parse(f, head, len, &r);
			if (e != SVF_OK || !r.h)
				return e;
			cur = NO_BLOCK;
			p = NULL;
		}
		if (r.wb == pitch && r.rp == r.wb)
			return svf__stream_bytes(f, at + r.p, cell + (uint16_t)(r.y0 * pitch), (uint16_t)(r.h * pitch), merge);
		for (row = 0; row < r.h; ++row) {
			uint32_t off = at + r.p + (uint32_t)row * r.rp;
			uint8_t *dst = cell + (uint16_t)((r.y0 + row) * pitch) + r.x0;

			if ((off & mask) + r.wb > mask + 1) {
				/* A row across two blocks: through svf__stream_bytes, whose lookups may refill p's slot. */
				e = svf__stream_bytes(f, off, dst, r.wb, merge);
				if (e != SVF_OK)
					return e;
				cur = NO_BLOCK;
				continue;
			}
			if (off >> f->block_shift != cur) {
				cur = off >> f->block_shift;
				p = block(f, cur);
				if (!p)
					return SVF_E_IO;
			}
			if (merge)
				svf__merge(f, dst, 0, p + (off & mask), 0, r.wb, 1);
			else
				memcpy(dst, p + (off & mask), r.wb);
		}
		return SVF_OK;
	}
	if (len > stride) {
		/* The header first: the rows are then read where they fit. */
		if (len < 4)
			return SVF_E_RANGE;
		if (!read_all(f, at, head, 4))
			return SVF_E_IO;
		e = svf__rec_parse(f, head, len, &r);
		if (e != SVF_OK)
			return e;
	}

	/* No cache. A record that fits the cell: one read, into the end of the cell (a copy) or the scratch (a merge). */
	if (len <= stride) {
		if (len < 4)
			return SVF_E_RANGE;
		rows = merge ? buf : buf + stride - len;
		if (!read_all(f, at, rows, len))
			return SVF_E_IO;
		e = svf__rec_parse(f, rows, len, &r);
		if (e != SVF_OK)
			return e;
		rows += r.p;
	} else {
		/* Longer than the cell (header read above): if its rows fit the cell, one read of them, else one a row. */
		if (!r.h)
			return SVF_OK;
		n = (uint16_t)(r.h * r.rp);
		if (n > stride) {
			for (row = 0; row < r.h; ++row) {
				uint8_t *dst = cell + (uint16_t)((r.y0 + row) * pitch) + r.x0;
				uint8_t *to = merge ? scratch : dst;

				if (!read_all(f, at + r.p + row * r.rp, to, r.wb))
					return SVF_E_IO;
				if (merge)
					svf__merge(f, dst, 0, scratch, 0, r.wb, 1);
			}
			return SVF_OK;
		}
		rows = merge ? buf : buf + stride - n;
		if (!read_all(f, at + r.p, rows, n))
			return SVF_E_IO;
	}
	if (merge) {
		if (r.h)
			svf__put_rows(f, cell, rows, &r, 1);
	} else if (r.h) {
		place_rows(cell, stride, (uint16_t)(rows - cell), &r, pitch);
	} else {
		memset(cell, 0, stride);
	}
	return SVF_OK;
}

#endif /* !SVF_NO_STREAM */
