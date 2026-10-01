#include <cxxtest/TestSuite.h>

#include "common/file-cache-stats.h"
#include "common/memstream.h"
#include "engines/scumm/trs_store.h"

/**
 * TrsStore: a .trs body left in its file must answer the bundle's binary
 * searches exactly as comparing the strings in memory did, read in blocks,
 * and keep the strings it hands out.
 */
class TrsStoreTestSuite : public CxxTest::TestSuite {
	/// resStrLen() for v5: 0xFF, a code, and two arguments unless the code is 1, 2, 3 or 8.
	static int strLen(const byte *s) {
		int n = 0;
		byte c;
		while ((c = *s++) != 0) {
			n++;
			if (c == 0xFF) {
				c = *s++;
				n++;
				if (c != 1 && c != 2 && c != 3 && c != 8) {
					s += 2;
					n += 2;
				}
			}
		}
		return n;
	}

	/// ScummEngine::searchTranslatedLine() as it is with the body in memory.
	static const byte *memorySearch(const Common::Array<byte> &body, const Common::Array<Scumm::TrsStore::Line> &lines,
									const Common::Array<uint16> &order, const byte *text, int left, int right,
									bool useIndex) {
		const int textLen = strLen(text);
		while (left <= right) {
			const int mid = (left + right) / 2;
			const int idx = useIndex ? order[mid] : mid;
			const byte *orig = &body[lines[idx].orig];
			const int origLen = strLen(orig);
			const int c = memcmp(text, orig, MIN(textLen + 1, origLen + 1));
			if (c == 0)
				return &body[lines[idx].trans];
			if (c < 0)
				right = mid - 1;
			else
				left = mid + 1;
		}
		return nullptr;
	}

	/// The same over a TrsStore.
	static const byte *storeSearch(Scumm::TrsStore &store, const Common::Array<Scumm::TrsStore::Line> &lines,
								   const Common::Array<uint16> &order, const byte *text, int left, int right,
								   bool useIndex) {
		uint first, last;
		if (!store.find(text, strLen(text), first, last))
			return nullptr;
		while (left <= right) {
			const int mid = (left + right) / 2;
			const uint idx = useIndex ? order[mid] : (uint)mid;
			if (idx >= first && idx <= last)
				return store.string(lines[idx].trans);
			if (idx > last)
				right = mid - 1;
			else
				left = mid + 1;
		}
		return nullptr;
	}

	static void add(Common::Array<byte> &body, const Common::Array<byte> &s) {
		for (uint i = 0; i < s.size(); ++i)
			body.push_back(s[i]);
	}

	/// A string of @p n letters from @p seed, with an escape whose arguments hold 0 now and then.
	static Common::Array<byte> makeString(uint32 seed, int n) {
		Common::Array<byte> s;
		for (int i = 0; i < n; ++i) {
			seed = seed * 1103515245u + 12345u;
			const byte r = (byte)(seed >> 16);
			if (r % 11 == 0) {
				s.push_back(0xFF);
				s.push_back(6);
				s.push_back(r);
				s.push_back(0);
			} else if (r % 13 == 0) {
				s.push_back(0xFF);
				s.push_back(3);	// no arguments
			} else {
				s.push_back('a' + r % 4);
			}
		}
		s.push_back(0);
		return s;
	}

	static bool less(const Common::Array<byte> &a, const Common::Array<byte> &b) {
		return memcmp(a.begin(), b.begin(), MIN(a.size(), b.size())) < 0;
	}

	struct Fixture {
		Common::Array<byte> body;
		Common::Array<Scumm::TrsStore::Line> lines;
		Common::Array<Common::Array<byte> > originals;
		Common::Array<uint16> order;	///< a "script" order: every third line, then the rest, each sorted
	};

	/// Sorted originals, some repeated (with other translations), each followed by its translation.
	static void build(Fixture &f, int n) {
		Common::Array<Common::Array<byte> > o;
		for (int i = 0; i < n; ++i)
			o.push_back(makeString(i * 7919u + 1, 1 + i % 9));
		for (int i = 0; i < n; i += 17)
			o.push_back(o[i]);	// repeated
		// Insertion sort: few enough.
		for (uint i = 1; i < o.size(); ++i)
			for (uint j = i; j > 0 && less(o[j], o[j - 1]); --j) {
				Common::Array<byte> t = o[j];
				o[j] = o[j - 1];
				o[j - 1] = t;
			}
		f.body.push_back(0x07);	// the body does not start with a line
		for (uint i = 0; i < o.size(); ++i) {
			Scumm::TrsStore::Line l;
			l.orig = f.body.size();
			add(f.body, o[i]);
			l.trans = f.body.size();
			Common::Array<byte> t = makeString(i * 31u + 5, 3 + i % 5);
			t.insert_at(0, (byte)'T');
			add(f.body, t);
			f.lines.push_back(l);
		}
		f.originals = o;
		for (uint i = 0; i < o.size(); i += 3)
			f.order.push_back(i);
		for (uint i = 0; i < o.size(); ++i)
			if (i % 3)
				f.order.push_back(i);
	}

	static Common::SeekableReadStream *streamOf(const Common::Array<byte> &body, uint32 pad) {
		byte *p = (byte *)malloc(body.size() + pad);
		memset(p, 0x55, pad);
		memcpy(p + pad, body.begin(), body.size());
		return new Common::MemoryReadStream(p, body.size() + pad, DisposeAfterUse::YES);
	}

public:
	void test_finds_what_the_search_in_memory_finds() {
		Fixture f;
		build(f, 300);
		Scumm::TrsStore store;
		TS_ASSERT(store.open(streamOf(f.body, 100), 100, f.body.size(), f.lines.begin(), f.lines.size(), 5, 0, "t.trs",
							 2048));
		// Every original, and strings that are none, over the whole and over ranges.
		Common::Array<Common::Array<byte> > queries = f.originals;
		for (int i = 0; i < 200; ++i)
			queries.push_back(makeString(i * 104729u + 3, 1 + i % 9));
		Common::Array<byte> prefix = f.originals[40];
		prefix[prefix.size() - 2] = 0;	// shorter
		queries.push_back(prefix);
		const int n = f.lines.size();
		int found = 0;
		for (uint q = 0; q < queries.size(); ++q) {
			const byte *text = queries[q].begin();
			const int ranges[][2] = { { 0, n - 1 }, { 10, n / 3 }, { n / 3, n - 1 }, { 5, 5 } };
			for (int r = 0; r < 4; ++r) {
				for (int useIndex = 0; useIndex < 2; ++useIndex) {
					const byte *a = memorySearch(f.body, f.lines, f.order, text, ranges[r][0], ranges[r][1], useIndex);
					const byte *b = storeSearch(store, f.lines, f.order, text, ranges[r][0], ranges[r][1], useIndex);
					TS_ASSERT_EQUALS(a == nullptr, b == nullptr);
					if (a && b) {
						++found;
						const int len = strLen(a);
						TS_ASSERT_EQUALS(strLen(b), len);
						TS_ASSERT_EQUALS(memcmp(a, b, len + 1), 0);
					}
				}
			}
		}
		TS_ASSERT(found > 300);
	}

	void test_keeps_what_it_hands_out_and_reads_blocks() {
		Fixture f;
		build(f, 100);
		Scumm::TrsStore store;
		const uint before = Common::FileCacheRegistry::all().size();
		TS_ASSERT(store.open(streamOf(f.body, 0), 0, f.body.size(), f.lines.begin(), f.lines.size(), 5, 0, "k.trs"));
		TS_ASSERT_EQUALS(Common::FileCacheRegistry::all().size(), before + 1);
		// Opening read the body once, in big blocks.
		const uint32 blocks = (f.body.size() + SCUMM_TRS_SCAN_BLOCK - 1) / SCUMM_TRS_SCAN_BLOCK;
		TS_ASSERT_EQUALS(store.stats().reads, blocks);
		TS_ASSERT_EQUALS(store.stats().readBytes, f.body.size());
		const uint32 reads = store.stats().reads;
		// The same line again and again: one read at most, then hits.
		uint first, last;
		for (int i = 0; i < 50; ++i) {
			TS_ASSERT(store.find(f.originals[60].begin(), strLen(f.originals[60].begin()), first, last));
			TS_ASSERT(store.string(f.lines[first].trans) != nullptr);
		}
		TS_ASSERT(store.stats().reads <= reads + 1);
		TS_ASSERT(store.stats().hits >= 98u);
		TS_ASSERT(store.stats().used <= store.stats().capacity);
		TS_ASSERT_EQUALS(store.stats().kind, Common::String("trs"));
		store.close();
		TS_ASSERT_EQUALS(Common::FileCacheRegistry::all().size(), before);
	}

	static void countSeen(void *ctx, const byte *s, uint32 size) {
		Common::Array<Common::Array<byte> > &seen = *(Common::Array<Common::Array<byte> > *)ctx;
		Common::Array<byte> copy;
		for (uint32 i = 0; i < size; ++i)
			copy.push_back(s[i]);
		seen.push_back(copy);
	}

	// Each translation is handed over, end included, while the body is read
	// for the index: no second pass over the file.
	void test_open_hands_over_every_translation_in_one_pass() {
		Fixture f;
		build(f, 80);
		Common::Array<Common::Array<byte> > seen;
		Scumm::TrsStore store;
		TS_ASSERT(store.open(streamOf(f.body, 0), 0, f.body.size(), f.lines.begin(), f.lines.size(), 5, 0, "p.trs",
							 SCUMM_TRS_CACHE_KB * 1024, &countSeen, &seen));
		TS_ASSERT_EQUALS(seen.size(), f.lines.size());
		for (uint i = 0; i < seen.size() && i < f.lines.size(); ++i) {
			const byte *t = &f.body[f.lines[i].trans];
			TS_ASSERT_EQUALS(seen[i].size(), (uint)strLen(t) + 1);
			TS_ASSERT_EQUALS(memcmp(seen[i].begin(), t, seen[i].size()), 0);
		}
		TS_ASSERT_EQUALS(store.stats().reads, (f.body.size() + SCUMM_TRS_SCAN_BLOCK - 1) / SCUMM_TRS_SCAN_BLOCK);
		// Back to small blocks afterwards.
		TS_ASSERT(store.stats().used <= store.stats().capacity);
	}

	void test_a_small_cache_keeps_to_its_size() {
		Fixture f;
		build(f, 300);
		Scumm::TrsStore store;
		TS_ASSERT(store.open(streamOf(f.body, 0), 0, f.body.size(), f.lines.begin(), f.lines.size(), 5, 0, "s.trs", 512));
		for (uint i = 0; i < f.lines.size(); ++i)
			TS_ASSERT(store.string(f.lines[i].trans) != nullptr);
		TS_ASSERT(store.stats().used <= 512u + 2 * SCUMM_TRS_READ_BLOCK);
	}

	void test_refuses_unsorted_originals() {
		Fixture f;
		build(f, 50);
		Scumm::TrsStore::Line t = f.lines[3];
		f.lines[3] = f.lines[30];
		f.lines[30] = t;
		Scumm::TrsStore store;
		TS_ASSERT(!store.open(streamOf(f.body, 0), 0, f.body.size(), f.lines.begin(), f.lines.size(), 5, 0, "u.trs"));
		TS_ASSERT(!store.isOpen());
	}

	void test_refuses_a_string_past_the_body() {
		Fixture f;
		build(f, 20);
		Scumm::TrsStore store;
		TS_ASSERT(!store.open(streamOf(f.body, 0), 0, f.lines.back().orig + 1, f.lines.begin(), f.lines.size(), 5, 0, "c.trs"));
	}

	class FlakyStream : public Common::MemoryReadStream {
	public:
		FlakyStream(byte *p, uint32 n, bool &fail) : Common::MemoryReadStream(p, n, DisposeAfterUse::YES), _fail(fail) {}
		uint32 read(void *dataPtr, uint32 dataSize) override {
			return _fail ? 0 : Common::MemoryReadStream::read(dataPtr, dataSize);
		}

	private:
		bool &_fail;
	};

	// A read that fails is told apart from "no such line", so the engine
	// can leave the line untranslated rather than look elsewhere.
	void test_a_failed_read_is_reported_not_taken_for_absent() {
		Fixture f;
		build(f, 200);
		bool fail = false;
		byte *p = (byte *)malloc(f.body.size());
		memcpy(p, f.body.begin(), f.body.size());
		Scumm::TrsStore store;
		TS_ASSERT(store.open(new FlakyStream(p, f.body.size(), fail), 0, f.body.size(), f.lines.begin(), f.lines.size(),
							 5, 0, "f.trs", 512));
		TS_ASSERT(!store.takeReadFailure());
		fail = true;
		uint first, last;
		const byte *text = f.originals[150].begin();
		TS_ASSERT(!store.find(text, strLen(text), first, last));
		TS_ASSERT(store.takeReadFailure());
		TS_ASSERT(!store.takeReadFailure());	// taken
		fail = false;
		TS_ASSERT(store.find(text, strLen(text), first, last));
		TS_ASSERT(!store.takeReadFailure());
	}
};
