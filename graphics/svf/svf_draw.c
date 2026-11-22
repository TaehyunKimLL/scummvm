/* svf_draw.c - UTF-8 lines over a face chain: decoding, face choice, layout, measure, and the targets they draw on.
 *
 * The rules are spec/renderer-design.md "Line drawing" (engine-derived); tools/svfline.py is the reference, pixel for
 * pixel. One walker lays the line out for both svf_measure_utf8 and svf_draw_utf8, so the two cannot disagree.
 * Scaled lines (svf_chain scale_num / scale_den) scale lengths and cells through svf_scale.c; with SVF_NO_SCALE nothing
 * of it is referred to and any ratio but 1/1 is refused. The optional composed-syllable cache (svf_chain_cache) is
 * here too: it sits between the walker and svf_glyph.
 */
#include "svf_internal.h"

#include <string.h>
#ifdef SVF_FUZZ_POISON
#include <sanitizer/asan_interface.h> /* fuzz builds only: manual poisoning of the scaler's idle scratch */
#endif

#ifdef SVF_NO_SCALE
#define SCALE_LEN(w, v) ((int32_t)(v))
#define SCALE_DIM(w, v) ((uint32_t)(v))
#else
/* A length or cell size of the walker's ratio (num == den only at 1/1: the ratio is reduced). */
#define SCALE_LEN(w, v) ((w)->num == (w)->den ? (int32_t)(v) : svf__scale_len((v), (w)->num, (w)->den))
#define SCALE_DIM(w, v) ((w)->num == (w)->den ? (uint32_t)(v) : svf__scale_dim((v), (w)->num, (w)->den))
#endif

#define CP_INVALID ((uint32_t)0x7FFFFFFFu) /* a maximal invalid subpart (never a decoded code point) */
#define CP_FFFD ((uint32_t)0xFFFDu)
#define PEN_MAX ((int32_t)0x40000000L)       /* the pen's travel saturates here: no overflow whatever the length */
#define WIDTH_MAX ((int32_t)32767)

int svf_is_mark_default(uint32_t cp)
{
	return cp >= 0x300 && cp <= 0x36F;
}

/* ---- UTF-8 ------------------------------------------------------------------------------------------------------ */

/* The code point at s[*i] (*i < len), *i moved past it; CP_INVALID for a maximal invalid subpart (Unicode's "maximal
 * subpart" practice, as Python's errors="replace"): a byte that starts no sequence alone, else the lead byte and the
 * continuation bytes that were valid so far. Never reads s[len]. */
static uint32_t utf8_next(const uint8_t *s, size_t len, size_t *i)
{
	uint8_t b = s[(*i)++], lo = 0x80, hi = 0xBF;
	uint32_t cp;
	int n, k;

	if (b < 0x80)
		return b;
	if (b < 0xC2 || b > 0xF4)
		return CP_INVALID;
	n = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
	cp = b & (0x3Fu >> n);
	if (b == 0xE0)
		lo = 0xA0; /* overlong */
	else if (b == 0xED)
		hi = 0x9F; /* surrogates */
	else if (b == 0xF0)
		lo = 0x90; /* overlong */
	else if (b == 0xF4)
		hi = 0x8F; /* past U+10FFFF */
	for (k = 0; k < n; ++k) {
		uint8_t c;
		if (*i >= len)
			return CP_INVALID;
		c = s[*i];
		if (c < lo || c > hi)
			return CP_INVALID;
		++*i;
		cp = cp << 6 | (c & 0x3Fu);
		lo = 0x80;
		hi = 0xBF;
	}
	return cp;
}

/* ---- layout ----------------------------------------------------------------------------------------------------- */

static int chain_ok(const svf_chain *c)
{
	uint8_t i;

	if (!c || !c->faces || !c->count)
		return 0;
	for (i = 0; i < c->count; ++i)
		if (!c->faces[i])
			return 0;
	return 1;
}

/* The chain's ratio, reduced, in @p num / @p den: 0/0 is 1/1; 0 (refused) unless both are 1..16, or, built with
 * SVF_NO_SCALE, unless it is 1/1. */
static int ratio_of(const svf_chain *c, uint8_t *num, uint8_t *den)
{
	uint8_t n = c->scale_num, d = c->scale_den;

	if (!n && !d)
		n = d = 1;
	if (n < 1 || n > 16 || d < 1 || d > 16)
		return 0;
#ifdef SVF_NO_SCALE
	if (n != d)
		return 0;
	n = d = 1;
#else
	{
		uint8_t a = n, b = d;
		while (b) {
			uint8_t r = a % b;
			a = b;
			b = r;
		}
		n /= a;
		d /= a;
	}
#endif
	*num = n;
	*den = d;
	return 1;
}

/* The index in the chain of the first face that has @p cp, or -1. */
static int face_for(const svf_chain *c, uint32_t cp)
{
	uint8_t i;

	for (i = 0; i < c->count; ++i)
		if (svf_has(c->faces[i], cp))
			return i;
	return -1;
}

/* One drawn cell: its face (and its index in the chain), code point, left column (pen offset from the line's x) and
 * dy. */
typedef struct {
	const svf_face *face;
	uint8_t fi;
	uint32_t cp;
	int32_t dx, dy;
} place;

typedef struct {
	const svf_chain *c;
	const uint8_t *s;
	size_t len, i;
	int32_t pen; /* travel from the line's x, 0..PEN_MAX */
	int fffd;    /* a face of the chain has U+FFFD */
	svf_is_mark_fn is_mark;
	uint8_t num, den; /* the chain's ratio, reduced (ratio_of) */
} walker;

/* The walker of @p c over @p s, or 0 if the chain's ratio is refused. */
static int walk_init(walker *w, const svf_chain *c, const char *s, size_t len)
{
	if (!ratio_of(c, &w->num, &w->den))
		return 0;
	w->c = c;
	w->s = (const uint8_t *)s;
	w->len = len;
	w->i = 0;
	w->pen = 0;
	w->fffd = face_for(c, CP_FFFD) >= 0;
	w->is_mark = c->is_mark ? c->is_mark : svf_is_mark_default;
	return 1;
}

/* Moves the pen by @p adv (an advance in source pixels) scaled. */
static void pen_add(walker *w, uint8_t adv)
{
	int32_t a = SCALE_LEN(w, adv);
	w->pen = w->pen < PEN_MAX - a ? w->pen + a : PEN_MAX;
}

/* The next drawn cell of the line in @p p (1), or 0 at its end; code points no face has only move the pen. Lengths in
 * @p p are scaled (output pixels). */
static int walk_next(walker *w, place *p)
{
	while (w->i < w->len) {
		uint32_t cp = utf8_next(w->s, w->len, &w->i);
		const svf_face *f;
		svf_metrics m;
		int fi;

		if (cp == CP_INVALID) {
			if (!w->fffd)
				continue;
			cp = CP_FFFD;
		}
		fi = face_for(w->c, cp);
		f = fi >= 0 ? w->c->faces[fi] : NULL;
		if (!f || svf_metrics_of(f, cp, &m) < 0) {
			pen_add(w, w->c->missing_advance);
			continue;
		}
		p->face = f;
		p->fi = (uint8_t)fi;
		p->cp = cp;
		p->dy = SCALE_LEN(w, m.dy);
		p->dx = w->pen;
		if (w->is_mark(cp)) {
			/* The engine's anchor (the pen after the previous non-mark glyph) is the current pen here. */
			if ((f->flags & SVF_FLAG_MARKS_AT_ORIGIN) && m.bearing_x < 0)
				p->dx -= SCALE_LEN(w, -m.bearing_x < f->cell_w ? -m.bearing_x : f->cell_w);
		} else {
			pen_add(w, m.advance);
		}
		return 1;
	}
	return 0;
}

svf_err svf_measure_utf8(const svf_chain *c, const char *s, size_t len, svf_extent *e)
{
	walker w;
	place p;
	int32_t up = 0, down = 0;

	if (!chain_ok(c) || !e || (!s && len) || !walk_init(&w, c, s, len))
		return SVF_E_ARG;
	while (walk_next(&w, &p)) {
		int32_t asc = SCALE_LEN(&w, p.face->ascent);
		int32_t a = asc - p.dy, d = (int32_t)SCALE_DIM(&w, p.face->cell_h) - asc + p.dy;
		if (a > up)
			up = a;
		if (d > down)
			down = d;
	}
	e->width = (int16_t)(w.pen < WIDTH_MAX ? w.pen : WIDTH_MAX);
	e->ascent = (int16_t)up;
	e->descent = (int16_t)down;
	return SVF_OK;
}

/* The scratch one glyph of @p f needs at the walker's ratio (uint32: at most 2 * 65025 + 4080^2). */
static uint32_t scratch_of(const walker *w, const svf_face *f)
{
	uint32_t n = (uint32_t)f->pitch * f->cell_h;

	if (w->num != w->den)
		return 2 * n + SCALE_DIM(w, f->cell_w) * SCALE_DIM(w, f->cell_h);
	return SVF__STREAMED(f) && !f->slots ? 2 * n : n;
}

/* svf_chain_scratch_bytes in uint32 (0 for a chain without faces or a refused ratio). */
static uint32_t chain_scratch(const svf_chain *c)
{
	uint32_t need = 0;
	walker w;
	uint8_t i;

	if (!chain_ok(c) || !ratio_of(c, &w.num, &w.den))
		return 0;
	for (i = 0; i < c->count; ++i) {
		uint32_t n = scratch_of(&w, c->faces[i]);
		if (n > need)
			need = n;
	}
	return need;
}

size_t svf_chain_scratch_bytes(const svf_chain *c)
{
	uint32_t need = chain_scratch(c);

#if SIZE_MAX < 0xFFFFFFFFu
	if (need > SIZE_MAX)
		return SIZE_MAX;
#endif
	return (size_t)need;
}

/* ---- composed-syllable cache --------------------------------------------------------------------------------------- */

/* A slot: u32 code point (little-endian; CACHE_EMPTY in an empty slot), u8 face index, 3 bytes (the first slot's last
 * two: the replacement cursor, u16 little-endian), then cache_cell bytes of cell. The buffer has no alignment. */
#define SLOT_HEAD 8
#define CACHE_EMPTY ((uint32_t)0xFFFFFFFFu) /* never a code point the walker gives */

static uint8_t *slot_at(const svf_chain *c, uint16_t i)
{
	return c->cache + (size_t)i * (SLOT_HEAD + (size_t)c->cache_cell);
}

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

svf_err svf_chain_cache(svf_chain *c, uint8_t *buf, size_t len)
{
	uint16_t cell = 0, i;
	size_t n;
	uint8_t k;

	if (!c)
		return SVF_E_ARG;
	c->cache = NULL;
	c->cache_slots = 0;
	c->cache_cell = 0;
	if (!chain_ok(c) || (!buf && len))
		return SVF_E_ARG;
	if (!len)
		return SVF_OK;
	for (k = 0; k < c->count; ++k)
		if (c->faces[k]->stride > cell)
			cell = c->faces[k]->stride;
	n = len / (SLOT_HEAD + (size_t)cell);
	if (!n)
		return SVF_E_NOMEM;
	c->cache = buf;
	c->cache_slots = (uint16_t)(n < 0xFFFFu ? n : 0xFFFFu);
	c->cache_cell = cell;
	for (i = 0; i < c->cache_slots; ++i) {
		uint8_t *s = slot_at(c, i);
		put32(s, CACHE_EMPTY);
		s[4] = s[5] = s[6] = s[7] = 0;
	}
	return SVF_OK;
}

/* The cell the cache holds for (face @p fi, @p cp), or NULL. */
static const uint8_t *cache_find(const svf_chain *c, uint8_t fi, uint32_t cp)
{
	uint16_t i;

	for (i = 0; i < c->cache_slots; ++i) {
		const uint8_t *s = slot_at(c, i);
		if (get32(s) == cp && s[4] == fi)
			return s + SLOT_HEAD;
	}
	return NULL;
}

/* Keeps the @p n bytes of @p cell (n <= cache_cell) for (face @p fi, @p cp) in the slot at the cursor; the cursor moves
 * on, round robin. */
static void cache_put(const svf_chain *c, uint8_t fi, uint32_t cp, const uint8_t *cell, uint16_t n)
{
	uint8_t *s0 = c->cache, *s;
	uint16_t next = (uint16_t)(s0[6] | (uint16_t)s0[7] << 8); /* unsigned: no signed shift past 32767 (16-bit int) */

	if (next >= c->cache_slots)
		next = 0;
	s = slot_at(c, next);
	put32(s, cp);
	s[4] = fi;
	memcpy(s + SLOT_HEAD, cell, n);
	if (++next == c->cache_slots)
		next = 0;
	s0[6] = (uint8_t)next;
	s0[7] = (uint8_t)(next >> 8);
}

/* ---- targets ---------------------------------------------------------------------------------------------------- */

/* Non-zero if @p t is a target svf_target_init accepts (its own fields re-checked, so a caller's edits are too); a kind
 * not in SVF_TARGETS is never accepted. @p gray_bits is the pixel width a GRAY target's row is checked for: 1 at init
 * (the least any width needs), t->gray_bits at draw time (which must be 1, 2, 4 or 8). */
static int target_ok(const svf_target *t, uint8_t gray_bits)
{
	uint32_t row = 0, rows = 0; /* set by every kind compiled in; zero keeps a build with no kinds free of an uninitialised-use warning */

	if (!t || t->w <= 0 || t->h <= 0)
		return 0;
	switch (t->kind) {
#if SVF_TARGETS & SVF_TARGET_GRAY8
	case SVF_T_GRAY8: row = (uint32_t)t->w; rows = (uint32_t)t->h; break;
#endif
#if SVF_TARGETS & SVF_TARGET_MONO_PAGE
	case SVF_T_MONO_PAGE: row = (uint32_t)t->w; rows = ((uint32_t)t->h + 7) / 8; break;
#endif
#if SVF_TARGETS & SVF_TARGET_MONO_ROW
	case SVF_T_MONO_ROW: row = ((uint32_t)t->w + 7) / 8; rows = (uint32_t)t->h; break;
#endif
#if SVF_TARGETS & SVF_TARGET_RGB565
	case SVF_T_RGB565: row = 2 * (uint32_t)t->w; rows = (uint32_t)t->h; break;
#endif
#if SVF_TARGETS & SVF_TARGET_RGB555
	case SVF_T_RGB555: row = 2 * (uint32_t)t->w; rows = (uint32_t)t->h; break;
#endif
#if SVF_TARGETS & SVF_TARGET_XRGB8888
	case SVF_T_XRGB8888: row = 4 * (uint32_t)t->w; rows = (uint32_t)t->h; break;
#endif
#if SVF_TARGETS & SVF_TARGET_RGB888
	case SVF_T_RGB888: row = 3 * (uint32_t)t->w; rows = (uint32_t)t->h; break;
#endif
#if SVF_TARGETS & SVF_TARGET_INDEXED8
	case SVF_T_INDEXED8:
		if (t->index_levels < 1 || (unsigned)t->index_base + t->index_levels > 255u)
			return 0;
		row = (uint32_t)t->w;
		rows = (uint32_t)t->h;
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_GRAY
	case SVF_T_GRAY:
		if (gray_bits != 1 && gray_bits != 2 && gray_bits != 4 && gray_bits != 8)
			return 0;
		row = ((uint32_t)t->w * gray_bits + 7) / 8;
		rows = (uint32_t)t->h;
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_CB
	case SVF_T_COVERAGE_CB: return 1;
#endif
	default: (void)gray_bits; return 0; /* unknown, or a kind left out of SVF_TARGETS */
	}
	/* rows * stride < 2^31: no overflow in 32 bits; compared with buf_len in the wider of uint32_t and size_t. */
	return t->buf && t->stride >= row && (uint32_t)rows * t->stride <= t->buf_len;
}

svf_err svf_target_init(svf_target *t, svf_target_kind k, uint8_t *buf, size_t buf_len, int16_t w, int16_t h,
                        uint16_t stride)
{
	if (!t)
		return SVF_E_ARG;
	memset(t, 0, sizeof *t);
	t->kind = k;
	t->buf = buf;
	t->buf_len = buf_len;
	t->w = w;
	t->h = h;
	t->stride = stride;
	t->clip_x1 = w;
	t->clip_y1 = h;
	t->fg565 = 0xFFFF;
	t->mono_threshold = 128;
#if SVF_TARGETS & (SVF_TARGET_XRGB8888 | SVF_TARGET_RGB888)
	t->fg888 = 0x00FFFFFFu;
#endif
#if SVF_TARGETS & SVF_TARGET_INDEXED8
	t->index_levels = 255;
#endif
#if SVF_TARGETS & SVF_TARGET_GRAY
	t->gray_bits = 8;
#endif
	if (!target_ok(t, 1)) {
		memset(t, 0, sizeof *t);
		return SVF_E_ARG;
	}
	return SVF_OK;
}

/* The clip rectangle in force: the target's, intersected with [0, w) x [0, h). */
typedef struct {
	int32_t x0, y0, x1, y1;
} rect;

static rect clip_of(const svf_target *t)
{
	rect r;
	r.x0 = t->clip_x0 > 0 ? t->clip_x0 : 0;
	r.y0 = t->clip_y0 > 0 ? t->clip_y0 : 0;
	r.x1 = t->clip_x1 < t->w ? t->clip_x1 : t->w;
	r.y1 = t->clip_y1 < t->h ? t->clip_y1 : t->h;
	return r;
}

#if SVF_TARGETS & SVF_TARGET_RGB565
/* bg and fg blended by @p cov per 5/6/5 channel: (c_bg * (255 - cov) + c_fg * cov + 127) / 255. */
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t cov)
{
	unsigned inv = 255u - cov, r, g, b;

	r = ((bg >> 11 & 31u) * inv + (fg >> 11 & 31u) * cov + 127u) / 255u;
	g = ((bg >> 5 & 63u) * inv + (fg >> 5 & 63u) * cov + 127u) / 255u;
	b = ((bg & 31u) * inv + (fg & 31u) * cov + 127u) / 255u;
	return (uint16_t)(r << 11 | g << 5 | b);
}
#endif

#if SVF_TARGETS & SVF_TARGET_RGB555
/* The low 15 bits of bg and fg blended by @p cov per 5/5/5 channel (blend565's arithmetic); bit 15 of either is
 * ignored and the result's is 0 (the caller ORs fg's bit 15). Products are at most 31 * 255 + 31 * 255 + 127. */
static uint16_t blend555(uint16_t fg, uint16_t bg, uint8_t cov)
{
	unsigned inv = 255u - cov, r, g, b;

	r = ((bg >> 10 & 31u) * inv + (fg >> 10 & 31u) * cov + 127u) / 255u;
	g = ((bg >> 5 & 31u) * inv + (fg >> 5 & 31u) * cov + 127u) / 255u;
	b = ((bg & 31u) * inv + (fg & 31u) * cov + 127u) / 255u;
	return (uint16_t)(r << 10 | g << 5 | b);
}
#endif

#if SVF_TARGETS & (SVF_TARGET_XRGB8888 | SVF_TARGET_RGB888)
/* fg and bg (0x00RRGGBB; bits 31..24 ignored) blended by @p cov per 8-bit channel, (c_bg * (255 - cov) + c_fg * cov +
 * 127) / 255, each channel in uint32_t (a product is up to 255 * 255 * 2 + 127: past 16 bits). Returns 0x00RRGGBB. */
static uint32_t blend888(uint32_t fg, uint32_t bg, uint8_t cov)
{
	uint32_t inv = 255u - cov, r, g, b;

	r = ((bg >> 16 & 255u) * inv + (fg >> 16 & 255u) * cov + 127u) / 255u;
	g = ((bg >> 8 & 255u) * inv + (fg >> 8 & 255u) * cov + 127u) / 255u;
	b = ((bg & 255u) * inv + (fg & 255u) * cov + 127u) / 255u;
	return r << 16 | g << 8 | b;
}
#endif

/* A cell to draw: @p w x @p h pixels of @p bpp, rows @p pitch bytes apart (a stored cell, or a scaled GRAY8 one). */
typedef struct {
	const uint8_t *px;
	int32_t w, h;
	size_t pitch;
	uint8_t bpp;
} cellview;

/* The visible part of a cell: columns [c0, c1) and rows [r0, r1) of it. */
typedef struct {
	int32_t c0, c1, r0, r1;
} cut;

/* Fills @p k with the cell @p cv, top-left at (@p x, @p y), cut by the clip @p r; 0 when nothing of it is visible. */
static int cut_cell(const rect *r, const cellview *cv, int32_t x, int32_t y, cut *k)
{
	k->c0 = r->x0 > x ? r->x0 - x : 0;
	k->c1 = r->x1 - x < cv->w ? r->x1 - x : cv->w;
	k->r0 = r->y0 > y ? r->y0 - y : 0;
	k->r1 = r->y1 - y < cv->h ? r->y1 - y : cv->h;
	return k->c0 < k->c1 && k->r0 < k->r1;
}

/* The loops below run the source a byte at a time: the pixels of source byte @p b (1 bpp: 8, 2 bpp: 4) that fall in
 * [c0, c1) are cols lo..hi-1, pixel col sitting at shift (ppb - 1 - (col - base)) * bpp. Addresses are size_t (y + row
 * and x + col are inside the clip, so inside the buffer). */

#if SVF_TARGETS & (SVF_TARGET_GRAY8 | SVF_TARGET_GRAY)
#if SVF_GRAY8_LUT1
/* The 1 bpp GRAY8 expansion: row v holds the four pixels of a nibble v, byte j is 255 when bit 3 - j of v is set, else 0.
 * A pixel takes max(d, coverage) and max(d, 255) == d | 255, max(d, 0) == d, so a whole source byte is two 4-byte ORs.
 * --gc-sections drops the table where nothing references it. */
static const uint8_t svf__lut1n[16][4] = {
	{0, 0, 0, 0},       {0, 0, 0, 255},     {0, 0, 255, 0},     {0, 0, 255, 255},
	{0, 255, 0, 0},     {0, 255, 0, 255},   {0, 255, 255, 0},   {0, 255, 255, 255},
	{255, 0, 0, 0},     {255, 0, 0, 255},   {255, 0, 255, 0},   {255, 0, 255, 255},
	{255, 255, 0, 0},   {255, 255, 0, 255}, {255, 255, 255, 0}, {255, 255, 255, 255}
};

/* ORs the nibble row @p n into the four bytes at @p d. */
static void lut1_or4(uint8_t *d, unsigned n)
{
#if SVF_WORD_STORE
	uint32_t a, m;
	memcpy(&a, d, 4);
	memcpy(&m, svf__lut1n[n], 4);
	a |= m;
	memcpy(d, &a, 4);
#else
	d[0] |= svf__lut1n[n][0];
	d[1] |= svf__lut1n[n][1];
	d[2] |= svf__lut1n[n][2];
	d[3] |= svf__lut1n[n][3];
#endif
}
#endif

/* GRAY8: a pixel takes max(existing, coverage). */
/* With both GRAY8 and GRAY compiled in, always_inline keeps the one call of the dispatch inlined at -O2 as well (aa_syl_bwelk
 * on the Pico was 1.5 % slower than a GRAY8-only build without it; c/bench/MEASUREMENTS.md). Not on AVR: the size is what counts there. */
#if defined(__GNUC__) && !defined(__AVR__) && (SVF_TARGETS & SVF_TARGET_GRAY8) && (SVF_TARGETS & SVF_TARGET_GRAY)
__attribute__((always_inline)) inline
#endif
static void blit_gray8(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	int32_t row, col, b;

	for (row = k->r0; row < k->r1; ++row) {
		const uint8_t *src = cv->px + (size_t)row * cv->pitch;
		uint8_t *dst = t->buf + (size_t)(y + row) * t->stride; /* x + col >= 0: x itself may be < 0 */

		switch (cv->bpp) {
		case 1:
			for (b = k->c0 >> 3; b <= (k->c1 - 1) >> 3; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 8 > k->c0 ? b * 8 : k->c0, hi = b * 8 + 8 < k->c1 ? b * 8 + 8 : k->c1;
				if (!v)
					continue;
#if SVF_GRAY8_LUT1
				if (lo == b * 8 && hi == b * 8 + 8) {
					lut1_or4(dst + x + b * 8, v >> 4);
					lut1_or4(dst + x + b * 8 + 4, v & 15u);
					continue;
				}
#endif
				for (col = lo; col < hi; ++col)
					if (v >> (7 - (col - b * 8)) & 1)
						dst[x + col] = 255;
			}
			break;
		case 2:
			for (b = k->c0 >> 2; b <= (k->c1 - 1) >> 2; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 4 > k->c0 ? b * 4 : k->c0, hi = b * 4 + 4 < k->c1 ? b * 4 + 4 : k->c1;
				if (!v)
					continue;
				for (col = lo; col < hi; ++col) {
					uint8_t g = (uint8_t)((v >> (6 - 2 * (col - b * 4)) & 3) * 85);
					if (g > dst[x + col])
						dst[x + col] = g;
				}
			}
			break;
		default:
			for (col = k->c0; col < k->c1; ++col) {
				uint8_t v = src[col];
				if (v > dst[x + col])
					dst[x + col] = v;
			}
			break;
		}
	}
}
#endif

#if SVF_TARGETS & (SVF_TARGET_INDEXED8 | SVF_TARGET_GRAY)
/* The ramp level of coverage @p cov (0..@p levels): (cov * levels + 127) / 255 for @p levels >= 2; for one level the
 * mono rule, 1 when cov >= thr (a thr of 0 counting as 1). The product is formed in unsigned (at most 255 * 255 + 127
 * = 65152, which fits 16 bits; test_blend16.c models it): never two promoted uint8_t operands, whose int is 16 bits
 * signed on AVR. */
static uint8_t index_level(uint8_t cov, uint8_t levels, uint8_t thr)
{
	if (levels == 1)
		return (uint8_t)(cov >= (thr ? (unsigned)thr : 1u));
	return (uint8_t)(((unsigned)cov * (unsigned)levels + 127u) / 255u);
}
#endif

#if SVF_TARGETS & SVF_TARGET_GRAY
/* GRAY with gray_bits 1, 2 or 4 (8 goes through blit_gray8): the pixel x is the field of bits bits at bit position
 * x * bits of its row, from the high end of its byte (MSB first) or the low end (SVF_TF_GRAY_LSB_FIRST); a pixel takes
 * max(field, L), L = index_level(cov, 2^bits - 1, mono_threshold) (bits 1: the mono rule), L = 0 leaves it. Only the
 * pixel's own bits are written, so padding bits and bytes stay. A 1 bpp pixel is coverage 255, a 2 bpp level j 85 * j. */
static void blit_gray_packed(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	const uint8_t bits = t->gray_bits, top = (uint8_t)((1u << bits) - 1u) /* bits 8 never reaches here: blit_gray8 */, thr = t->mono_threshold;
	const int lsb = (t->flags & SVF_TF_GRAY_LSB_FIRST) != 0;
	int32_t row, col;

	for (row = k->r0; row < k->r1; ++row) {
		const uint8_t *src = cv->px + (size_t)row * cv->pitch;
		uint8_t *dst = t->buf + (size_t)(y + row) * t->stride; /* x + col >= 0: x itself may be < 0 */

		for (col = k->c0; col < k->c1; ++col) {
			uint8_t cov = svf__coverage(cv->bpp, src, col), L, f, s;
			uint32_t px = (uint32_t)(x + col) << (bits >> 1); /* bits is 1, 2 or 4: bits >> 1 is its log2; no multiply */
			uint8_t *d = dst + (px >> 3);

			if (!cov || !(L = index_level(cov, top, thr)))
				continue;
			s = lsb ? (uint8_t)(px & 7u) : (uint8_t)(8u - bits - (px & 7u));
			f = (uint8_t)(*d >> s & top);
			if (L > f)
				*d = (uint8_t)((*d & ~(top << s)) | (L << s));
		}
	}
}
#endif

#if SVF_TARGETS & SVF_TARGET_INDEXED8

#if SVF_WORD_STORE
/* Four bytes at @p d each become the greater of themselves and @p g: one load, four byte maxima, one store (the
 * switch is 1 only where a 4-byte memcpy at any alignment is one access). */
static void max4(uint8_t *d, uint8_t g)
{
	uint8_t q[4];
	unsigned m;

	memcpy(q, d, 4);
	for (m = 0; m < 4; ++m)
		q[m] = q[m] > g ? q[m] : g;
	memcpy(d, q, 4);
}
#endif

/* INDEXED8: a pixel takes the greater of its byte and index_base + level (the GRAY8 max rule on palette indices);
 * level 0 leaves it. A 1 bpp pixel is coverage 255 (level index_levels), a 2 bpp level j is coverage 85 * j. */
static void blit_indexed8(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	const uint8_t n = t->index_levels, base = t->index_base, thr = t->mono_threshold;
	const uint8_t top = (uint8_t)(base + n); /* target_ok: base + n <= 255 */
	uint8_t idx[4] = {0, 0, 0, 0}; /* 2 bpp: the byte a level draws, 0 for none */
	uint8_t j;
	int32_t row, col, b;

	if (cv->bpp == 2) {
		for (j = 1; j < 4; ++j) {
			uint8_t l = index_level((uint8_t)(85u * j), n, thr);
			idx[j] = l ? (uint8_t)(base + l) : 0;
		}
	}
	for (row = k->r0; row < k->r1; ++row) {
		const uint8_t *src = cv->px + (size_t)row * cv->pitch;
		uint8_t *dst = t->buf + (size_t)(y + row) * t->stride; /* x + col >= 0: x itself may be < 0 */

		switch (cv->bpp) {
		case 1:
			for (b = k->c0 >> 3; b <= (k->c1 - 1) >> 3; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 8 > k->c0 ? b * 8 : k->c0, hi = b * 8 + 8 < k->c1 ? b * 8 + 8 : k->c1;
				if (!v)
					continue;
				/* No word path for 1 bpp: the nibble path measured 4.7 % slower than these byte loops on MARUS24 (1 bpp,
				 * 24 px, DOSBox-X 486, c/bench/MEASUREMENTS.md), so it was removed. */
				for (col = lo; col < hi; ++col)
					if (v >> (7 - (col - b * 8)) & 1 && top > dst[x + col])
						dst[x + col] = top;
			}
			break;
		case 2:
			for (b = k->c0 >> 2; b <= (k->c1 - 1) >> 2; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 4 > k->c0 ? b * 4 : k->c0, hi = b * 4 + 4 < k->c1 ? b * 4 + 4 : k->c1;
				if (!v)
					continue;
#if SVF_WORD_STORE
				if (v == 0xFF && b * 4 >= k->c0 && b * 4 + 4 <= k->c1) { /* four level-3 pixels inside the cut */
					max4(dst + (size_t)(x + b * 4), idx[3]);
					continue;
				}
#endif
				for (col = lo; col < hi; ++col) {
					uint8_t g = idx[v >> (6 - 2 * (col - b * 4)) & 3];
					if (g > dst[x + col])
						dst[x + col] = g;
				}
			}
			break;
		default:
			for (col = k->c0; col < k->c1; ++col) {
				uint8_t l = index_level(src[col], n, thr), g;
				if (!l)
					continue;
				g = (uint8_t)(base + l);
				if (g > dst[x + col])
					dst[x + col] = g;
			}
			break;
		}
	}
}
#endif

#if SVF_TARGETS & (SVF_TARGET_RGB565 | SVF_TARGET_RGB555)
/* RGB565 and RGB555: a covered pixel is written over the existing one (the later cell wins) in the target's byte
 * order. pix[j] holds the two bytes of level j (j = 3 the 1 bpp ink) in that order; @p blend gives the colour of a
 * covered 8 bpp pixel (1 and 2 bpp use pix), @p or_mask is ORed to it (RGB555: fg's bit 15; no masking here, RGB565
 * uses all 16 bits). */
static void blit_rgb16(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y,
                       const uint8_t pix[4][2], uint16_t (*blend)(uint16_t, uint16_t, uint8_t), uint16_t or_mask)
{
	int be = (t->flags & SVF_TF_565_BIGENDIAN) != 0;
	int32_t row, col, b;
#if SVF_WORD_STORE
	uint8_t pair[4][4][4]; /* [a][b]: the 4 bytes of the pixel of level a, then that of level b (1 and 2 bpp only) */

	if (cv->bpp == 1) { /* the ink pair only */
		memcpy(pair[3][3], pix[3], 2);
		memcpy(pair[3][3] + 2, pix[3], 2);
	} else if (cv->bpp == 2) {
		unsigned la, lb;
		for (la = 0; la < 4; ++la)
			for (lb = 0; lb < 4; ++lb) {
				memcpy(pair[la][lb], pix[la], 2);
				memcpy(pair[la][lb] + 2, pix[lb], 2);
			}
	}
#endif

	for (row = k->r0; row < k->r1; ++row) {
		const uint8_t *src = cv->px + (size_t)row * cv->pitch;
		uint8_t *dst = t->buf + (size_t)(y + row) * t->stride; /* x + col >= 0: x itself may be < 0 */
		size_t i;

		switch (cv->bpp) {
		case 1:
			for (b = k->c0 >> 3; b <= (k->c1 - 1) >> 3; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 8 > k->c0 ? b * 8 : k->c0, hi = b * 8 + 8 < k->c1 ? b * 8 + 8 : k->c1;
				if (!v)
					continue;
#if SVF_WORD_STORE
				/* A byte inside the cut: each 2-bit group 11 is one 4-byte pair, a lone bit 2 bytes. The first and
				 * last byte of a row (and any byte the cut crosses) take the loop below. */
				if (lo == b * 8 && hi == b * 8 + 8) {
					unsigned g;
					for (g = 0; g < 4; ++g) {
						unsigned gb = (unsigned)(v >> (6 - 2 * g)) & 3u;
						uint8_t *d = dst + 2 * (size_t)(x + b * 8 + (int32_t)(2 * g));
						if (gb == 3u) {
							memcpy(d, pair[3][3], 4);
						} else if (gb) {
							memcpy(d + 2 * (gb & 1u), pix[3], 2);
						}
					}
					continue;
				}
#endif
				for (col = lo; col < hi; ++col)
					if (v >> (7 - (col - b * 8)) & 1) {
						i = 2 * (size_t)(x + col);
						dst[i] = pix[3][0];
						dst[i + 1] = pix[3][1];
					}
			}
			break;
		case 2:
			for (b = k->c0 >> 2; b <= (k->c1 - 1) >> 2; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 4 > k->c0 ? b * 4 : k->c0, hi = b * 4 + 4 < k->c1 ? b * 4 + 4 : k->c1;
				if (!v)
					continue;
#if SVF_WORD_STORE
				/* A byte inside the cut: each pair of pixels with both levels non-zero is one 4-byte pair, a lone
				 * pixel 2 bytes; edge bytes take the loop below. */
				if (lo == b * 4 && hi == b * 4 + 4) {
					unsigned g;
					for (g = 0; g < 2; ++g) {
						unsigned la = (unsigned)(v >> (6 - 4 * g)) & 3u, lb = (unsigned)(v >> (4 - 4 * g)) & 3u;
						uint8_t *d = dst + 2 * (size_t)(x + b * 4 + (int32_t)(2 * g));
						if (la && lb) {
							memcpy(d, pair[la][lb], 4);
						} else {
							if (la)
								memcpy(d, pix[la], 2);
							if (lb)
								memcpy(d + 2, pix[lb], 2);
						}
					}
					continue;
				}
#endif
				for (col = lo; col < hi; ++col) {
					unsigned lv = v >> (6 - 2 * (col - b * 4)) & 3;
					if (lv) {
						i = 2 * (size_t)(x + col);
						dst[i] = pix[lv][0];
						dst[i + 1] = pix[lv][1];
					}
				}
			}
			break;
		default:
			for (col = k->c0; col < k->c1; ++col) {
				uint8_t v = src[col];
				uint16_t c;
				if (!v)
					continue;
				c = (uint16_t)(blend(t->fg565, t->bg565, v) | or_mask);
				i = 2 * (size_t)(x + col);
				dst[i + be] = (uint8_t)c;
				dst[i + !be] = (uint8_t)(c >> 8);
			}
			break;
		}
	}
}

/* The two bytes of the 2 bpp levels 1..3 (coverage 85 j) in the target's byte order; level 3 is also the 1 bpp ink. */
static void rgb16_pix(const svf_target *t, const cellview *cv, uint8_t pix[4][2],
                      uint16_t (*blend)(uint16_t, uint16_t, uint8_t), uint16_t or_mask)
{
	int be = (t->flags & SVF_TF_565_BIGENDIAN) != 0, j;

	for (j = 0; j < 4; ++j) {
		uint16_t c = j && cv->bpp != 8 ? (uint16_t)(blend(t->fg565, t->bg565, (uint8_t)(j * 85)) | or_mask) : 0;
		pix[j][be] = (uint8_t)c;
		pix[j][!be] = (uint8_t)(c >> 8);
	}
}

#if SVF_TARGETS & SVF_TARGET_RGB565
static void blit_rgb565(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	uint8_t pix[4][2];

	rgb16_pix(t, cv, pix, blend565, 0);
	blit_rgb16(t, cv, k, x, y, pix, blend565, 0);
}
#endif

#if SVF_TARGETS & SVF_TARGET_RGB555
static void blit_rgb555(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	uint8_t pix[4][2];
	uint16_t hi = (uint16_t)(t->fg565 & 0x8000u);

	rgb16_pix(t, cv, pix, blend555, hi);
	blit_rgb16(t, cv, k, x, y, pix, blend555, hi);
}
#endif
#endif

#if SVF_TARGETS & (SVF_TARGET_XRGB8888 | SVF_TARGET_RGB888)
/* XRGB8888 (@p n = 4: B G R X) and RGB888 (@p n = 3: B G R): a covered pixel is written over the existing one (the later
 * cell wins). px[j] holds the n bytes of level j (j = 3 the 1 bpp ink); 8 bpp blends per pixel. A pixel is stored by
 * put888, whose two memcpy calls have constant sizes (4 and 3): a size known to the compiler becomes one 32-bit store
 * (XRGB8888) or a 16-bit and an 8-bit store (RGB888) where the target has them: x86 (DJGPP, host gcc and clang) and
 * WASM inline them, and AVR as byte stores; Cortex-M0+ has no unaligned word stores, so it still calls memcpy for the 4
 * or 3 bytes (other targets, ESP32-S3 among them, were not checked); a run-time size would be a byte loop (DJGPP) or a
 * library call. The bytes are the same everywhere. */
static void put888(uint8_t *d, const uint8_t *p, unsigned n)
{
	if (n == 4)
		memcpy(d, p, 4);
	else
		memcpy(d, p, 3);
}

static void blit_888(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y,
                     const uint8_t px[4][4], unsigned n)
{
	int32_t row, col, b;

	for (row = k->r0; row < k->r1; ++row) {
		const uint8_t *src = cv->px + (size_t)row * cv->pitch;
		uint8_t *dst = t->buf + (size_t)(y + row) * t->stride; /* x + col >= 0: x itself may be < 0 */

		switch (cv->bpp) {
		case 1:
			for (b = k->c0 >> 3; b <= (k->c1 - 1) >> 3; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 8 > k->c0 ? b * 8 : k->c0, hi = b * 8 + 8 < k->c1 ? b * 8 + 8 : k->c1;
				if (!v)
					continue;
				for (col = lo; col < hi; ++col)
					if (v >> (7 - (col - b * 8)) & 1)
						put888(dst + n * (size_t)(x + col), px[3], n);
			}
			break;
		case 2:
			for (b = k->c0 >> 2; b <= (k->c1 - 1) >> 2; ++b) {
				uint8_t v = src[b];
				int32_t lo = b * 4 > k->c0 ? b * 4 : k->c0, hi = b * 4 + 4 < k->c1 ? b * 4 + 4 : k->c1;
				if (!v)
					continue;
				for (col = lo; col < hi; ++col) {
					unsigned lv = v >> (6 - 2 * (col - b * 4)) & 3;
					if (lv)
						put888(dst + n * (size_t)(x + col), px[lv], n);
				}
			}
			break;
		default:
			for (col = k->c0; col < k->c1; ++col) {
				uint8_t v = src[col], q[4];
				uint32_t c;
				if (!v)
					continue;
				c = blend888(t->fg888, t->bg888, v);
				q[0] = (uint8_t)c;
				q[1] = (uint8_t)(c >> 8);
				q[2] = (uint8_t)(c >> 16);
				q[3] = (uint8_t)(t->fg888 >> 24);
				put888(dst + n * (size_t)(x + col), q, n);
			}
			break;
		}
	}
}

/* The B G R X bytes of the 2 bpp levels 1..3 (coverage 85 j); level 3 is also the 1 bpp ink. */
static void px888(const svf_target *t, const cellview *cv, uint8_t px[4][4])
{
	int j;

	for (j = 0; j < 4; ++j) {
		uint32_t c = j && cv->bpp != 8 ? blend888(t->fg888, t->bg888, (uint8_t)(j * 85)) : 0;
		px[j][0] = (uint8_t)c;
		px[j][1] = (uint8_t)(c >> 8);
		px[j][2] = (uint8_t)(c >> 16);
		px[j][3] = (uint8_t)(t->fg888 >> 24);
	}
}
#endif

#if SVF_TARGETS & SVF_TARGET_XRGB8888
static void blit_xrgb8888(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	uint8_t px[4][4];

	px888(t, cv, px);
	blit_888(t, cv, k, x, y, px, 4);
}
#endif

#if SVF_TARGETS & SVF_TARGET_RGB888
static void blit_rgb888(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	uint8_t px[4][4];

	px888(t, cv, px);
	blit_888(t, cv, k, x, y, px, 3);
}
#endif

#if SVF_TARGETS & (SVF_TARGET_MONO_ROW | SVF_TARGET_MONO_PAGE)
/* The least 2 bpp level at or above the threshold: 1..3 (thr 0 counts as 1). */
static uint8_t least_level(uint8_t thr)
{
	return (uint8_t)(((thr ? thr : 1u) + 84u) / 85u);
}

/* Pixels 8i..8i+7 of a row as one MSB-first byte, for 2 and 8 bpp only (1 bpp loads its byte directly, in
 * mono_src and blit_mono_page): 2 bpp level >= least, 8 bpp byte >= thr; pixels past the row's width (and any i
 * outside the row) are 0. */
static uint8_t pack8(const cellview *cv, const uint8_t *row, int32_t i, uint8_t least, uint8_t thr)
{
	int32_t n = cv->w - 8 * i, j;
	unsigned v = 0;

	if (i < 0 || n <= 0)
		return 0;
	if (n > 8)
		n = 8;
	if (cv->bpp == 2) {
		for (j = 0; j < n; ++j)
			v = v << 1 | (unsigned)((row[2 * i + j / 4] >> (6 - 2 * (j & 3)) & 3u) >= least);
	} else {
		for (j = 0; j < n; ++j)
			v = v << 1 | (unsigned)(row[8 * i + j] >= thr);
	}
	return (uint8_t)(v << (8 - n));
}
#endif

#if SVF_TARGETS & SVF_TARGET_MONO_ROW
/* Source byte @p j of a row (0 outside the row). 1 bpp is the stored byte itself: its padding bits past the width, if
 * set, land only in target columns >= x + w >= x + c1, which the last-byte mask drops. 2 and 8 bpp pack once per 8. */
static uint8_t mono_src(const cellview *cv, const uint8_t *row, int32_t j, int32_t nb, uint8_t least, uint8_t thr)
{
	if (j < 0 || j >= nb)
		return 0;
	return cv->bpp == 1 ? row[j] : pack8(cv, row, j, least, thr);
}

/* MONO_ROW: target byte B takes the source bytes B-q-1 and B-q shifted by s = x mod 8 (x = 8q + s). */
static void blit_mono_row(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	uint8_t thr = t->mono_threshold ? t->mono_threshold : 1, least = least_level(thr);
	int32_t s = ((x % 8) + 8) % 8, q = (x - s) / 8, nb = (cv->w + 7) / 8;
	int32_t b0 = (x + k->c0) >> 3, b1 = (x + k->c1 - 1) >> 3, row, B;
	uint8_t m0 = (uint8_t)(0xFFu >> ((x + k->c0) & 7)), m1 = (uint8_t)(0xFFu << (7 - ((x + k->c1 - 1) & 7)));

	for (row = k->r0; row < k->r1; ++row) {
		const uint8_t *src = cv->px + (size_t)row * cv->pitch;
		uint8_t *dst = t->buf + (size_t)(y + row) * t->stride;
		unsigned prev = s ? mono_src(cv, src, b0 - q - 1, nb, least, thr) : 0;
		for (B = b0; B <= b1; ++B) {
			unsigned cur = mono_src(cv, src, B - q, nb, least, thr), v = cur >> s;

			if (s) /* s == 0: no earlier byte contributes */
				v |= (prev << (8 - s)) & 0xFFu;
			prev = cur;
			if (B == b0)
				v &= m0;
			if (B == b1)
				v &= m1;
			if (v)
				dst[B] |= (uint8_t)v;
		}
	}
}
#endif

#if SVF_TARGETS & SVF_TARGET_MONO_PAGE
/* out[c] bit k = b[k] bit (7 - c): 8 glyph-row bytes to 8 page-column bytes (two uint32 words, three swap stages). */
static void transpose8(const uint8_t b[8], uint8_t out[8])
{
	uint32_t x = (uint32_t)b[7] << 24 | (uint32_t)b[6] << 16 | (uint32_t)b[5] << 8 | b[4];
	uint32_t y = (uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0];
	uint32_t t;

	t = (x ^ (x >> 7)) & 0x00AA00AAu;
	x ^= t ^ (t << 7);
	t = (y ^ (y >> 7)) & 0x00AA00AAu;
	y ^= t ^ (t << 7);
	t = (x ^ (x >> 14)) & 0x0000CCCCu;
	x ^= t ^ (t << 14);
	t = (y ^ (y >> 14)) & 0x0000CCCCu;
	y ^= t ^ (t << 14);
	t = (x & 0xF0F0F0F0u) | ((y >> 4) & 0x0F0F0F0Fu);
	y = ((x << 4) & 0xF0F0F0F0u) | (y & 0x0F0F0F0Fu);
	x = t;
	out[0] = (uint8_t)(x >> 24);
	out[1] = (uint8_t)(x >> 16);
	out[2] = (uint8_t)(x >> 8);
	out[3] = (uint8_t)x;
	out[4] = (uint8_t)(y >> 24);
	out[5] = (uint8_t)(y >> 16);
	out[6] = (uint8_t)(y >> 8);
	out[7] = (uint8_t)y;
}

/* MONO_PAGE (bit k of a page byte = row 8p + k): per page and source byte column, the 8 glyph-row bytes of the page
 * are transposed into 8 page-column bytes. Rows outside the cut feed 0; a byte is stored only inside columns c0..c1. */
static void blit_mono_page(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	uint8_t thr = t->mono_threshold ? t->mono_threshold : 1, least = least_level(thr);
	int32_t p, i, bit, c;
	int32_t p0 = (y + k->r0) >> 3, p1 = (y + k->r1 - 1) >> 3, i0 = k->c0 >> 3, i1 = (k->c1 - 1) >> 3;

	for (p = p0; p <= p1; ++p) {
		uint8_t *page = t->buf + (size_t)p * t->stride;
		int32_t base = 8 * p - y, bl = k->r0 - base, bh = k->r1 - base;

		bl = bl < 0 ? 0 : bl;
		bh = bh > 8 ? 8 : bh;
		for (i = i0; i <= i1; ++i) {
			uint8_t b[8], out[8];
			int32_t lo = k->c0 - 8 * i, hi = k->c1 - 8 * i;
			const uint8_t *src;
			unsigned any = 0;

			for (bit = 0; bit < bl; ++bit)
				b[bit] = 0;
			for (src = cv->px + (size_t)(base + bl) * cv->pitch; bit < bh; ++bit, src += cv->pitch) {
				b[bit] = cv->bpp == 1 ? src[i] : pack8(cv, src, i, least, thr); /* i <= (c1 - 1) >> 3 < pitch: inside the row */
				any |= b[bit];
			}
			for (; bit < 8; ++bit)
				b[bit] = 0;
			if (!any)
				continue;
			transpose8(b, out);
			if (lo < 0)
				lo = 0;
			if (hi > 8)
				hi = 8;
			for (c = lo; c < hi; ++c)
				if (out[c])
					page[x + 8 * i + c] |= out[c];
		}
	}
}
#endif

#if SVF_TARGETS & SVF_TARGET_CB
/* COVERAGE_CB: every covered pixel to the callback. */
static void blit_cb(const svf_target *t, const cellview *cv, const cut *k, int32_t x, int32_t y)
{
	uint8_t bpp = cv->bpp;
	int32_t row, col;

	for (row = k->r0; row < k->r1; ++row) {
		const uint8_t *src = cv->px + (size_t)row * cv->pitch;
		for (col = k->c0; col < k->c1; ++col) {
			uint8_t v = svf__coverage(bpp, src, col);
			if (v)
				t->plot(t->ctx, (int16_t)(x + col), (int16_t)(y + row), v);
		}
	}
}
#endif

/* Draws the cell @p cv with its top-left pixel at (@p x, @p y), inside @p r: every pixel with coverage goes to the
 * target by the rules of its kind (svf.h). The kind (and in the GRAY8 and RGB565 loops the bpp) is decided once per
 * cell, so the pixel loops carry no dispatch. */
static void blit(const svf_target *t, const rect *r, const cellview *cv, int32_t x, int32_t y)
{
	cut k;

	if (!cut_cell(r, cv, x, y, &k))
		return;
	switch (t->kind) {
#if !defined(__AVR__) && (SVF_TARGETS & (SVF_TARGET_GRAY8 | SVF_TARGET_GRAY))
	/* GRAY8 and GRAY at 8 bits share one call of blit_gray8: two call sites keep gcc -Os from inlining it, which
	 * slows the GRAY8 draw by 4 % in a build with both kinds (Cortex-M0+). Not on AVR, where the merge costs size
	 * (P1* +166 B, P4+GRAY +220 B) and speed is not the question: the separate cases below. */
#if SVF_TARGETS & SVF_TARGET_GRAY
	case SVF_T_GRAY:
		if (t->gray_bits != 8) {
			blit_gray_packed(t, cv, &k, x, y);
			break;
		}
#endif
#if SVF_TARGETS & SVF_TARGET_GRAY8
		/* bits 8 is GRAY8 */
		/* fall through */
	case SVF_T_GRAY8:
#endif
		blit_gray8(t, cv, &k, x, y);
		break;
#endif
#if defined(__AVR__)
	/* the same dispatch as the non-AVR block above, as separate cases: keep the two in step (a change of what GRAY or GRAY8
	 * does goes into both; the host gcc and clang all-mask -Werror gate does not compile these cases, build-all.sh's gray-werror
	 * line does, for SVF_TARGETS 0x01, 0x200, 0x201 and 0x3FF on avr-gcc; there is no behavioural AVR GRAY run, see SIZES.md) */
#if SVF_TARGETS & SVF_TARGET_GRAY8
	case SVF_T_GRAY8:
		blit_gray8(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_GRAY
	case SVF_T_GRAY:
		if (t->gray_bits == 8)
			blit_gray8(t, cv, &k, x, y);
		else
			blit_gray_packed(t, cv, &k, x, y);
		break;
#endif
#endif
#if SVF_TARGETS & SVF_TARGET_MONO_PAGE
	case SVF_T_MONO_PAGE:
		blit_mono_page(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_MONO_ROW
	case SVF_T_MONO_ROW:
		blit_mono_row(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_RGB565
	case SVF_T_RGB565:
		blit_rgb565(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_RGB555
	case SVF_T_RGB555:
		blit_rgb555(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_XRGB8888
	case SVF_T_XRGB8888:
		blit_xrgb8888(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_RGB888
	case SVF_T_RGB888:
		blit_rgb888(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_INDEXED8
	case SVF_T_INDEXED8:
		blit_indexed8(t, cv, &k, x, y);
		break;
#endif
#if SVF_TARGETS & SVF_TARGET_CB
	case SVF_T_COVERAGE_CB:
		blit_cb(t, cv, &k, x, y);
		break;
#endif
	default: /* a kind left out of SVF_TARGETS: target_ok refused it, so this is not reached */
		break;
	}
}

svf_err svf_draw_utf8(const svf_chain *c, const char *s, size_t len, int16_t x, int16_t y_baseline, svf_target *t,
                      uint8_t *scratch, size_t scratch_len)
{
	svf_err rc = SVF_OK;
	uint32_t need;
	walker w;
	place p;
	rect r;

	if (!chain_ok(c) || (!s && len) || !target_ok(t, t ? t->gray_bits : 0) || (t->kind == SVF_T_COVERAGE_CB && !t->plot) ||
	    !walk_init(&w, c, s, len))
		return SVF_E_ARG;
	need = chain_scratch(c);
#if SIZE_MAX < 0xFFFFFFFFu
	if (need > SIZE_MAX)
		return SVF_E_NOMEM; /* a 16-bit size_t: no buffer is enough */
#endif
	if (!scratch && scratch_len)
		return SVF_E_ARG;
	if (!scratch || scratch_len < (size_t)need)
		return SVF_E_NOMEM;
	r = clip_of(t);
	while (walk_next(&w, &p)) {
		const svf_face *f = p.face;
		int32_t cx = (int32_t)x + p.dx, cy = (int32_t)y_baseline - SCALE_LEN(&w, f->ascent) + p.dy;
		const uint8_t *hit;
		cellview cv;
		svf_err e;
		int cached;

		cv.px = scratch;
		cv.w = (int32_t)SCALE_DIM(&w, f->cell_w);
		cv.h = (int32_t)SCALE_DIM(&w, f->cell_h);
		cv.pitch = f->pitch;
		cv.bpp = f->bpp;
		if (r.x0 >= r.x1 || r.y0 >= r.y1 || cx >= r.x1 || cy >= r.y1 || cx + cv.w <= r.x0 || cy + cv.h <= r.y0)
			continue; /* wholly clipped: not read */
		/* A composed cell goes through the cache (when the chain has one): a hit is copied, a miss composed and kept;
		 * a failed composition is not kept. */
		cached = c->cache && c->cache_slots && f->stride <= c->cache_cell && svf__composed(f, p.cp);
		hit = cached ? cache_find(c, p.fi, p.cp) : NULL;
		if (hit) {
			memcpy(scratch, hit, f->stride);
		} else {
			/* The stored cell and, composing a streamed face without a block cache, the piece read after it: never
			 * more than 2 cells, so a scaled cell after them is not written here. */
			uint32_t two = 2u * (uint32_t)f->stride; /* in 32 bits: 2 * 65025 wraps a 16-bit size_t */
			size_t lim = scratch_len;
#if SIZE_MAX < 0xFFFFFFFFu
			if (two <= SIZE_MAX && (size_t)two < lim)
#else
			if ((size_t)two < lim)
#endif
				lim = (size_t)two;
			e = svf_glyph(f, p.cp, scratch, lim);
			if (e != SVF_OK) {
				rc = e; /* SVF_E_IO: this glyph only (its cell is zero), the line goes on */
				continue;
			}
			if (cached)
				cache_put(c, p.fi, p.cp, scratch, f->stride);
		}
#ifndef SVF_NO_SCALE
		if (w.num != w.den) {
			/* The stored cell in scratch[0, 2 * cell bytes), the scaled GRAY8 cell after it. */
			uint8_t *scaled = scratch + 2 * (size_t)f->pitch * f->cell_h;
#ifdef SVF_FUZZ_POISON
			/* Fuzz builds only: the second cell's worth between the stored cell and the scaled one is idle here, so
			 * ASan sees a scaler that reads beyond its source cell (the one allocation would hide it). */
			ASAN_POISON_MEMORY_REGION(scratch + (size_t)f->pitch * f->cell_h, (size_t)f->pitch * f->cell_h);
#endif
			svf__scale_cell(f, scratch, w.num, w.den, scaled);
#ifdef SVF_FUZZ_POISON
			ASAN_UNPOISON_MEMORY_REGION(scratch + (size_t)f->pitch * f->cell_h, (size_t)f->pitch * f->cell_h);
#endif
			cv.px = scaled;
			cv.pitch = (size_t)cv.w;
			cv.bpp = 8;
		}
#endif
		blit(t, &r, &cv, cx, cy);
	}
	return rc;
}

#ifdef SVF_TEST_HOOKS
#include <stdio.h>
/* Test-only (tests/test_pad.c; the library build never defines SVF_TEST_HOOKS): blit a stored cell at (x, y) on @p t
 * clipped to its clip rectangle, with the cell bytes exactly as the caller built them (padding bits included: the
 * normal path gets cells whose padding svf_glyph has cleared). */
void svf_test_blit(const svf_target *t, const uint8_t *px, int32_t w, int32_t h, size_t pitch, uint8_t bpp, int32_t x,
                   int32_t y);
void svf_test_blit(const svf_target *t, const uint8_t *px, int32_t w, int32_t h, size_t pitch, uint8_t bpp, int32_t x,
                   int32_t y)
{
	rect r = clip_of(t);
	cellview cv;

#if SVF_TARGETS & SVF_TARGET_GRAY
	if (t->kind == SVF_T_GRAY && !target_ok(t, t->gray_bits)) {
		/* the public path refuses it with SVF_E_ARG (bits not 1, 2, 4 or 8, or a stride too short for them): the hook
		 * draws nothing, and says so, so a mistyped bits constant in a test is not a silent byte difference */
		fprintf(stderr, "svf_test_blit: GRAY target refused (gray_bits %u, stride %u)\n", (unsigned)t->gray_bits,
		        (unsigned)t->stride);
		return;
	}
#endif
	cv.px = px;
	cv.w = w;
	cv.h = h;
	cv.pitch = pitch;
	cv.bpp = bpp;
	blit(t, &r, &cv, x, y);
}
#endif
