/* svf_scale.c - scaled cells: whole multiples as pixel blocks, other ratios area-averaged (spec/renderer-design.md
 * "Scaling"; tools/svfline.py scale_coverage is the reference).
 *
 * A build with SVF_NO_SCALE compiles nothing here, and svf_draw.c then refers to nothing here (it refuses any ratio but
 * 1/1), so a size-constrained profile carries no scaling code whatever its linker does.
 */
#include "svf_internal.h"

#ifndef SVF_NO_SCALE

#include <string.h>

int32_t svf__scale_len(int32_t v, uint8_t num, uint8_t den)
{
	uint32_t a = (uint32_t)(v < 0 ? -v : v);

	a = (a * num + den / 2u) / den;
	return v < 0 ? -(int32_t)a : (int32_t)a;
}

uint32_t svf__scale_dim(uint8_t v, uint8_t num, uint8_t den)
{
	return ((uint32_t)v * num + den - 1u) / den;
}

/* Whole multiple @p n: each source row becomes n rows, each pixel n bytes. */
static void scale_blocks(const svf_face *f, const uint8_t *cell, uint8_t n, uint8_t *out)
{
	size_t sw = (size_t)f->cell_w * n;
	uint32_t sy, sx, k;

	for (sy = 0; sy < f->cell_h; ++sy) {
		const uint8_t *src = cell + (size_t)sy * f->pitch;
		uint8_t *dst = out + (size_t)sy * n * sw;
		for (sx = 0; sx < f->cell_w; ++sx)
			memset(dst + (size_t)sx * n, svf__coverage(f->bpp, src, (int32_t)sx), n);
		for (k = 1; k < n; ++k)
			memcpy(dst + k * sw, dst, sw);
	}
}

#if SVF_SCALE_FAST

/* The overlap, in 1/num pixel units, of source pixel s with output pixel o. */
static unsigned overlap(unsigned s, unsigned o, uint8_t num, uint8_t den)
{
	unsigned a = s * num > o * den ? s * num : o * den;
	unsigned b = (s + 1) * num < (o + 1) * den ? (s + 1) * num : (o + 1) * den;
	return b - a;
}

/* The weights of one axis, periodic: output pixel o (0 <= o < num) covers source pixels lo[o] .. lo[o] + cnt[o] - 1 of
 * its period with weights w[at[o] + i]; output o + num covers source s + den with the same weights. Returns the number
 * of weights (<= num + den - 1 <= 31). */
static unsigned axis_weights(uint8_t num, uint8_t den, uint8_t lo[16], uint8_t cnt[16], uint8_t at[16], uint8_t w[32])
{
	unsigned o, i, n = 0;

	for (o = 0; o < num; ++o) {
		lo[o] = (uint8_t)(o * den / num);
		cnt[o] = (uint8_t)(((o + 1) * den - 1) / num + 1 - lo[o]);
		at[o] = (uint8_t)n;
		for (i = 0; i < cnt[o]; ++i)
			w[n++] = (uint8_t)overlap(lo[o] + i, o, num, den);
	}
	return n;
}

/* STACK: this function's frame is large (610 B on AVR, 680 B on Cortex-M0+ at -Os: acc[64], cov[255] and the axis tables); a
 * scaled svf_draw_utf8 peaks at 864 B (AVR) / 1008 B (M0+) of stack. SVF_SCALE_FAST=0 builds the direct loop below instead
 * (peak 451 B / 680 B). Any change to these arrays: re-measure with c/cross/stack-usage.py and update svf.h and SIZES.md. */
/* Area average = separable box filter: sum = sum_sy wy * (sum_sx wx * cov), the weights periodic per axis (above); each
 * source row is expanded once per block of 64 output columns, and the sums of the block live in a stack accumulator. */
void svf__scale_cell(const svf_face *f, const uint8_t *cell, uint8_t num, uint8_t den, uint8_t *out)
{
	unsigned sw = (unsigned)svf__scale_dim(f->cell_w, num, den), sh = (unsigned)svf__scale_dim(f->cell_h, num, den);
	uint32_t den2 = (uint32_t)den * den;
	unsigned oy, ox0;
	uint8_t xlo[16], xcnt[16], xat[16], xw[32];
	uint8_t ylo[16], ycnt[16], yat[16], yw[32];
	uint16_t acc[64];
	uint8_t cov[255];

	if (den == 1) {
		scale_blocks(f, cell, num, out);
		return;
	}
	axis_weights(num, den, xlo, xcnt, xat, xw);
	axis_weights(num, den, ylo, ycnt, yat, yw);
	{
		unsigned qy = 0, oyo = 0;

		for (oy = 0; oy < sh; ++oy) {
			for (ox0 = 0; ox0 < sw; ox0 += 64) {
				unsigned n = sw - ox0 < 64 ? sw - ox0 : 64, k, i, sy, sx;
				unsigned qx = ox0 / num, oxo = ox0 % num, c0, c1;

				/* the source columns the block reads: from its first pixel's first to its last pixel's last */
				c0 = qx * den + xlo[oxo];
				c1 = (ox0 + n - 1) / num * den + xlo[(ox0 + n - 1) % num] + xcnt[(ox0 + n - 1) % num];
				if (c1 > f->cell_w)
					c1 = f->cell_w;
				for (k = 0; k < n; ++k)
					acc[k] = 0;
				for (i = 0; i < ycnt[oyo]; ++i) {
					const uint8_t *src;
					unsigned qq = qx, oo = oxo;

					sy = qy * den + ylo[oyo] + i;
					if (sy >= f->cell_h)
						break;
					src = cell + (size_t)sy * f->pitch;
					for (sx = c0; sx < c1; ++sx)
						cov[sx] = svf__coverage(f->bpp, src, (int32_t)sx);
					for (k = 0; k < n; ++k) {
						unsigned row = 0, m = xcnt[oo];
						const uint8_t *wp = xw + xat[oo];
						const uint8_t *cp;

						sx = qq * den + xlo[oo];
						/* unsigned, cannot wrap: sx is where output pixel ox starts in the source (ox * den / num), and ox < sw
						 * keeps that inside the cell, so sx <= cell_w */
						if (m > f->cell_w - sx)
							m = f->cell_w - sx;
						for (cp = cov + sx; m; --m)
							row += *wp++ * *cp++;
						acc[k] = (uint16_t)(acc[k] + row * yw[yat[oyo] + i]);
						if (++oo == num) {
							oo = 0;
							++qq;
						}
					}
				}
				/* sum <= 255 * den^2 <= 65280: the 16.16 mean fits 32 bits; rounded half up. */
				for (k = 0; k < n; ++k)
					*out++ = (uint8_t)((((uint32_t)acc[k] << 16) / den2 + 0x8000u) >> 16);
			}
			if (++oyo == num) {
				oyo = 0;
				++qy;
			}
		}
	}
}

#else /* !SVF_SCALE_FAST: the direct 2-D area average, small stack frame (the code of 4539e42) */

/* Output pixel o covers [o * den, (o + 1) * den) of a source axis in 1/num pixel units: source pixels lo..hi - 1 (cut
 * to the n pixels of the cell; the rest of the square counts 0). */
static void span(uint32_t o, uint8_t num, uint8_t den, uint32_t n, uint32_t *lo, uint32_t *hi)
{
	*lo = o * den / num;
	*hi = ((o + 1) * den - 1) / num + 1;
	if (*hi > n)
		*hi = n;
}

/* The overlap, in 1/num pixel units, of source pixel s with output pixel o. */
static uint32_t overlap(uint32_t s, uint32_t o, uint8_t num, uint8_t den)
{
	uint32_t a = s * num > o * den ? s * num : o * den;
	uint32_t b = (s + 1) * num < (o + 1) * den ? (s + 1) * num : (o + 1) * den;
	return b - a;
}

void svf__scale_cell(const svf_face *f, const uint8_t *cell, uint8_t num, uint8_t den, uint8_t *out)
{
	uint32_t sw = svf__scale_dim(f->cell_w, num, den), sh = svf__scale_dim(f->cell_h, num, den);
	uint32_t den2 = (uint32_t)den * den, ox, oy;

	if (den == 1) {
		scale_blocks(f, cell, num, out);
		return;
	}
	for (oy = 0; oy < sh; ++oy) {
		uint32_t y0, y1;
		span(oy, num, den, f->cell_h, &y0, &y1);
		for (ox = 0; ox < sw; ++ox) {
			uint32_t x0, x1, sx, sy, sum = 0;
			span(ox, num, den, f->cell_w, &x0, &x1);
			for (sy = y0; sy < y1; ++sy) {
				const uint8_t *src = cell + (size_t)sy * f->pitch;
				uint32_t row = 0;
				for (sx = x0; sx < x1; ++sx)
					row += svf__coverage(f->bpp, src, (int32_t)sx) * overlap(sx, ox, num, den);
				sum += row * overlap(sy, oy, num, den);
			}
			/* sum <= 255 * den^2 <= 65280: the 16.16 mean fits 32 bits; rounded half up. */
			*out++ = (uint8_t)(((sum << 16) / den2 + 0x8000u) >> 16);
		}
	}
}

#endif /* SVF_SCALE_FAST */

#else

typedef int svf__scale_left_out; /* ISO C wants a declaration in every translation unit */

#endif
