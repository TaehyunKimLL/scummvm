#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/endian.h"
#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"

#include "test/system/null_osystem.h"

#include "engines/scumm/korean_text.h"
#include "engines/scumm/trs_bundle.h"

// Developer data: the CP949 bundles of the harness converted by trs_bundle.h against their UTF-8 stage.
//
// With a folder korean-trs/ in the working directory holding <name>.cp949.trs and <name>.utf8.trs (the harness's
// CP949 bundle and the UTF-8 bundle of the same translation: mi2kor/korean.trs.cp949 and korean.trs, mi1ute-kor
// likewise), converting the CP949 one gives the UTF-8 one's translation of every line. <name>.allow, when present,
// holds the number of lines that may differ (MI1 UTE: 1, a stray 0xFA byte of its CP949 stage). Passes with a warning
// without the folder, as korean-vocab/ does.
class ScummKoreanTrsDataTestSuite : public CxxTest::TestSuite {
	struct Bundle {
		Common::Array<byte> body;
		Common::Array<uint32> originals, translations;
	};

	static bool readBundle(Common::SeekableReadStream &in, Bundle &b) {
		if (in.readUint32BE() != MKTAG('S', 'C', 'V', 'M') || in.readUint32BE() != MKTAG('T', 'R', 'S', ' '))
			return false;
		const uint n = in.readUint16LE();
		for (uint i = 0; i < n; i++) {
			in.readUint16LE();
			b.originals.push_back(in.readUint32LE());
			b.translations.push_back(in.readUint32LE());
		}
		const uint rooms = in.readByte();
		for (uint r = 0; r < rooms; r++) {
			in.readByte();
			const uint scripts = in.readUint16LE();
			in.skip(scripts * 8);
		}
		if (in.err())
			return false;
		const uint32 bodyPos = in.pos();
		for (uint i = 0; i < n; i++) {
			if (b.originals[i] < bodyPos || b.translations[i] < bodyPos)
				return false;
			b.originals[i] -= bodyPos;
			b.translations[i] -= bodyPos;
		}
		b.body.resize(in.size() - bodyPos);
		return in.read(b.body.begin(), b.body.size()) == b.body.size();
	}

	static Common::String hex(const Common::String &s, uint from = 0) {
		Common::String r;
		for (uint i = from; i < s.size() && i < from + 40; i++)
			r += Common::String::format("%02X", (byte)s[i]);
		return r;
	}

	static uint firstDifference(const Common::String &a, const Common::String &b) {
		uint i = 0;
		while (i < a.size() && i < b.size() && a[i] == b[i])
			i++;
		return i > 8 ? i - 8 : 0;
	}

	static Common::String text(const Bundle &b, uint32 off) {
		const byte *s = b.body.begin() + off, *end = b.body.begin() + b.body.size();
		return Common::String((const char *)s, Scumm::scummTextEnd(s, end) - s);
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_cp949_bundles_equal_their_utf8_stage_when_given() {
		Common::FSNode dir(Common::Path("korean-trs"));
		Common::FSList files;
		if (!dir.isDirectory() || !dir.getChildren(files, Common::FSNode::kListFilesOnly)) {
			TS_WARN("korean-trs/ is not in the working directory: CP949 bundle check skipped");
			return;
		}
		for (uint i = 0; i < files.size(); i++) {
			const Common::String fname = files[i].getName();
			if (!fname.hasSuffix(".cp949.trs"))
				continue;
			const Common::String name(fname.c_str(), fname.size() - 10);
			Bundle cp, u8;
			Common::ScopedPtr<Common::SeekableReadStream> a(files[i].createReadStream());
			Common::ScopedPtr<Common::SeekableReadStream> b(dir.getChild(name + ".utf8.trs").createReadStream());
			TSM_ASSERT(name.c_str(), a && b);
			if (!a || !b)
				continue;
			TSM_ASSERT(name.c_str(), readBundle(*a, cp) && readBundle(*b, u8));
			TSM_ASSERT_EQUALS(name.c_str(), cp.translations.size(), u8.translations.size());
			if (cp.translations.size() != u8.translations.size())
				continue;
			TSM_ASSERT(name.c_str(), !Scumm::trsBodyIsUtf8(cp.body.begin(), cp.body.size()));

			Common::Array<byte> out;
			uint unmapped = 0;
			TSM_ASSERT(name.c_str(), Scumm::convertTrsBodyToUtf8(cp.body.begin(), cp.body.size(), cp.originals, cp.translations, out, unmapped));
			Bundle conv;
			conv.body = out;
			uint differ = 0;
			for (uint k = 0; k < cp.translations.size(); k++) {
				// out starts with the BOM, the UTF-8 stage's body may too: compare the texts, not the offsets.
				if (text(conv, cp.translations[k]) != text(u8, u8.translations[k])) {
					if (!differ)
						TS_WARN(Common::String::format("%s: line %u differs: engine %s / stage %s", name.c_str(), k,
						        hex(text(conv, cp.translations[k]), firstDifference(text(conv, cp.translations[k]), text(u8, u8.translations[k]))).c_str(),
						        hex(text(u8, u8.translations[k]), firstDifference(text(conv, cp.translations[k]), text(u8, u8.translations[k]))).c_str()).c_str());
					differ++;
				}
			}
			// The originals (the lookup keys) are the game's bytes and must come through untouched.
			uint originalsDiffer = 0;
			for (uint k = 0; k < cp.originals.size(); k++) {
				if (text(conv, cp.originals[k]) != text(u8, u8.originals[k]))
					originalsDiffer++;
			}
			TSM_ASSERT_EQUALS(name.c_str(), originalsDiffer, 0u);
			uint allowed = 0;
			Common::ScopedPtr<Common::SeekableReadStream> allow(dir.getChild(name + ".allow").createReadStream());
			if (allow)
				allowed = atoi(allow->readLine().c_str());
			TSM_ASSERT_LESS_THAN_EQUALS(name.c_str(), differ, allowed);
			TS_WARN(Common::String::format("%s: %u of %u lines differ (allowed %u)", name.c_str(), differ, cp.translations.size(), allowed).c_str());
		}
	}
};
