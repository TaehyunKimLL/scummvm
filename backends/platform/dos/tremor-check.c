/* tremor-check: decode Ogg Vorbis clips of one file with Tremor, the decoder
 * SCUMM.EXE links, and report any that do not decode.
 *
 *   tremor-check FILE < clips.txt     clips.txt: "start size" per line
 *
 * Each clip is read whole and opened through ov_open_callbacks() on a memory
 * stream that can seek, as ScummVM's Vorbis decoder opens it. Prints one line
 * "FAIL <n> <start> <why>" per bad clip and, at the end,
 * "clips <n> bad <b> rate <min>-<max> mono <m> pcm <samples>"; exits 1 if any
 * clip is bad. Built by build-deps.sh host (Tremor and libogg for this machine).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbiscodec.h>
#include <tremor/ivorbisfile.h>

typedef struct { const unsigned char *p; long size, pos; } Mem;

static size_t mread(void *dst, size_t sz, size_t n, void *h) {
	Mem *m = (Mem *)h;
	size_t want = sz * n, have = (size_t)(m->size - m->pos);
	if (want > have)
		want = have - have % (sz ? sz : 1);
	memcpy(dst, m->p + m->pos, want);
	m->pos += (long)want;
	return sz ? want / sz : 0;
}

static int mseek(void *h, ogg_int64_t off, int whence) {
	Mem *m = (Mem *)h;
	ogg_int64_t to = whence == SEEK_SET ? off : whence == SEEK_CUR ? m->pos + off : m->size + off;
	if (to < 0 || to > m->size)
		return -1;
	m->pos = (long)to;
	return 0;
}

static long mtell(void *h) { return ((Mem *)h)->pos; }

int main(int argc, char **argv) {
	FILE *f;
	long start, size, n = 0, bad = 0, mono = 0, rmin = 0, rmax = 0;
	long long pcm = 0;
	static const ov_callbacks cb = { mread, mseek, NULL, mtell };
	if (argc != 2) {
		fprintf(stderr, "usage: tremor-check FILE < clips.txt\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 2;
	}
	while (scanf("%ld %ld", &start, &size) == 2) {
		unsigned char *buf = (unsigned char *)malloc(size);
		static char pcmbuf[4096];
		Mem m;
		OggVorbis_File vf;
		const char *why = NULL;
		long got, rate = 0;
		int bs;
		long long total = 0;
		n++;
		if (!buf || fseek(f, start, SEEK_SET) || (long)fread(buf, 1, size, f) != size) {
			printf("FAIL %ld %ld cannot read the clip\n", n - 1, start);
			bad++;
			free(buf);
			continue;
		}
		m.p = buf; m.size = size; m.pos = 0;
		if (ov_open_callbacks(&m, &vf, NULL, 0, cb) < 0) {
			why = "ov_open_callbacks failed";
		} else {
			vorbis_info *vi = ov_info(&vf, -1);
			rate = vi->rate;
			if (vi->channels == 1)
				mono++;
			while ((got = ov_read(&vf, pcmbuf, sizeof pcmbuf, &bs)) > 0)
				total += got / 2 / vi->channels;
			if (got < 0)
				why = "ov_read error";
			else if (total != ov_pcm_total(&vf, -1))
				why = "decoded length differs from ov_pcm_total";
			ov_clear(&vf);
		}
		if (why) {
			printf("FAIL %ld %ld %s\n", n - 1, start, why);
			bad++;
		} else {
			if (!rmin || rate < rmin) rmin = rate;
			if (rate > rmax) rmax = rate;
			pcm += total;
		}
		free(buf);
	}
	fclose(f);
	printf("clips %ld bad %ld rate %ld-%ld mono %ld pcm %lld\n", n, bad, rmin, rmax, mono, pcm);
	return bad ? 1 : 0;
}
