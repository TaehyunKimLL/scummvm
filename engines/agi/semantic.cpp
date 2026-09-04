/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "agi/semantic.h"

#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace Agi {

// "AGISEM\0\2"
static const byte kMagic[8] = { 'A', 'G', 'I', 'S', 'E', 'M', 0, 2 };

// U+2581 LOWER ONE EIGHTH BLOCK, the Metaspace replacement character, in UTF-8.
static const char *const kMeta = "\xE2\x96\x81";
static const uint kMetaLen = 3;

// Longest token we will try to match in the Viterbi pass, in bytes.
static const uint kMaxTokenBytes = 48;

// Score used when a byte cannot be covered by any known token.
static const float kUnknownLogP = -20.0f;

// Korean endings stripped to find a stem, longest first. Mirrors the offline
// reference implementation exactly.
static const char *const kEndings[] = {
	"\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4", // 습니다
	"\xEC\x96\xB4\xEB\x9D\xBC",             // 어라
	"\xEC\x95\x84\xEB\x9D\xBC",             // 아라
	"\xEC\x84\xB8\xEC\x9A\x94",             // 세요
	"\xEC\xA4\x84\xEB\x9E\x98",             // 줄래
	"\xED\x95\x98\xEB\x8B\xA4",             // 하다
	"\xED\x95\x9C\xEB\x8B\xA4",             // 한다
	"\xEB\xB3\xB4\xEC\x9E\x90",             // 보자
	"\xEC\x9E\x90",                         // 자
	"\xEB\x8B\xA4",                         // 다
	"\xEC\x96\xB4",                         // 어
	"\xEC\x95\x84",                         // 아
	"\xEC\x97\xAC",                         // 여
	"\xED\x95\xB4",                         // 해
	"\xEC\xA4\x98",                         // 줘
	"\xEB\x9D\xBC",                         // 라
	"\xEC\x9A\x94",                         // 요
	"\xEC\x95\xBC",                         // 야
	"\xEA\xB2\x8C",                         // 게
	"\xEC\x9D\x84",                         // 을
	"\xEB\xA5\xBC",                         // 를
	"\xEC\x9D\xB4",                         // 이
	"\xEA\xB0\x80",                         // 가
	"\xEC\x9D\x80",                         // 은
	"\xEB\x8A\x94",                         // 는
	"\xEC\x97\x90",                         // 에
	"\xEC\x9D\x98",                         // 의
	"\xEB\x8F\x84",                         // 도
	"\xEB\xA7\x8C",                         // 만
	"\xEB\xA1\x9C"                          // 로
};

SemanticParser::SemanticParser() : _loaded(false), _dim(0), _scale(0.0f) {
}

SemanticParser::~SemanticParser() {
}

bool SemanticParser::load(const Common::Path &path) {
	_loaded = false;

	// An absolute path is read directly; a bare name is looked up in the
	// game directory through SearchMan like any other engine data file.
	Common::SeekableReadStream *f = nullptr;
	Common::FSNode node(path);
	if (node.exists() && !node.isDirectory()) {
		f = node.createReadStream();
	} else {
		Common::File *file = new Common::File();
		if (file->open(path)) {
			f = file;
		} else {
			delete file;
		}
	}
	if (!f)
		return false;

	byte magic[8];
	if (f->read(magic, 8) != 8 || memcmp(magic, kMagic, 8) != 0) {
		warning("SemanticParser: bad magic in %s", path.toString().c_str());
		delete f;
		return false;
	}

	_dim = f->readUint16LE();
	const uint16 groupCount = f->readUint16LE();
	const uint32 entryCount = f->readUint32LE();
	const uint32 vocabCount = f->readUint32LE();
	uint32 rawScale = f->readUint32LE();
	memcpy(&_scale, &rawScale, sizeof(float));

	if (!_dim || _dim > 1024 || !groupCount || !entryCount || !vocabCount) {
		warning("SemanticParser: implausible header in %s", path.toString().c_str());
		delete f;
		return false;
	}

	_groups.clear();
	_groupIndex.clear();
	_groups.reserve(groupCount);
	for (uint16 i = 0; i < groupCount; ++i) {
		Group g;
		g.gid = f->readUint16LE();
		g.verb = f->readByte() != 0;
		const byte len = f->readByte();
		char buf[256];
		f->read(buf, len);
		g.name = Common::String(buf, len);
		_groupIndex[g.gid] = _groups.size();
		_groups.push_back(g);
	}

	_entries.clear();
	_entries.reserve(entryCount);
	for (uint32 i = 0; i < entryCount; ++i) {
		Entry e;
		e.gid = f->readUint16LE();
		e.weight = f->readByte() / 255.0f;
		const byte len = f->readByte();
		char buf[256];
		f->read(buf, len);
		e.text = Common::String(buf, len);
		_entries.push_back(e);
	}

	_tokens.clear();
	_tokens.reserve(vocabCount);
	_vectors.resize((uint32)vocabCount * _dim);
	for (uint32 i = 0; i < vocabCount; ++i) {
		Token t;
		const byte len = f->readByte();
		char buf[256];
		f->read(buf, len);
		t.text = Common::String(buf, len);
		uint32 raw = f->readUint32LE();
		memcpy(&t.logp, &raw, sizeof(float));
		t.vecOffset = i;
		f->read(&_vectors[(uint32)i * _dim], _dim);
		_tokens.push_back(t);
	}

	if (f->err()) {
		warning("SemanticParser: truncated %s", path.toString().c_str());
		delete f;
		return false;
	}
	delete f;

	// Precompute a unit vector per dictionary entry so matching is a dot
	// product rather than a tokenize-and-average per comparison.
	_entryVectors.resize((uint32)_entries.size() * _dim);
	for (uint i = 0; i < _entries.size(); ++i) {
		float *dst = &_entryVectors[(uint32)i * _dim];
		if (!embed(_entries[i].text, dst))
			memset(dst, 0, _dim * sizeof(float));
	}

	_loaded = true;
	debug(1, "SemanticParser: %u groups, %u entries, %u tokens, dim %u",
	      (uint)_groups.size(), (uint)_entries.size(), (uint)_tokens.size(), _dim);
	return true;
}

int SemanticParser::findToken(const Common::String &s) const {
	int lo = 0, hi = (int)_tokens.size() - 1;
	while (lo <= hi) {
		const int mid = lo + (hi - lo) / 2;
		const int cmp = strcmp(_tokens[mid].text.c_str(), s.c_str());
		if (cmp == 0)
			return mid;
		if (cmp < 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

void SemanticParser::viterbi(const Common::String &text, Common::Array<int> &outIds) const {
	outIds.clear();

	// Metaspace: prefix the string and replace spaces, always.
	Common::String s = kMeta;
	for (uint i = 0; i < text.size(); ++i) {
		if (text[i] == ' ')
			s += kMeta;
		else
			s += text[i];
	}

	const uint n = s.size();
	Common::Array<float> best(n + 1);
	Common::Array<int> prevPos(n + 1), prevTok(n + 1);
	for (uint i = 0; i <= n; ++i) {
		best[i] = -1e30f;
		prevPos[i] = -1;
		prevTok[i] = -1;
	}
	best[0] = 0.0f;

	for (uint i = 0; i < n; ++i) {
		if (best[i] <= -1e29f)
			continue;
		const uint limit = MIN(n, i + kMaxTokenBytes);
		for (uint j = i + 1; j <= limit; ++j) {
			// Only consider cuts on UTF-8 character boundaries.
			if (j < n && ((byte)s[j] & 0xC0) == 0x80)
				continue;
			const int idx = findToken(Common::String(s.c_str() + i, j - i));
			if (idx < 0)
				continue;
			const float sc = best[i] + _tokens[idx].logp;
			if (sc > best[j]) {
				best[j] = sc;
				prevPos[j] = (int)i;
				prevTok[j] = idx;
			}
		}

		// Fallback so unknown text still advances: consume one character.
		uint j = i + 1;
		while (j < n && ((byte)s[j] & 0xC0) == 0x80)
			++j;
		if (best[j] <= -1e29f) {
			const int idx = findToken(Common::String(s.c_str() + i, j - i));
			const float sc = best[i] + (idx >= 0 ? _tokens[idx].logp : kUnknownLogP);
			if (sc > best[j]) {
				best[j] = sc;
				prevPos[j] = (int)i;
				prevTok[j] = idx;
			}
		}
	}

	Common::Array<int> rev;
	int k = (int)n;
	while (k > 0 && prevPos[k] >= 0) {
		if (prevTok[k] >= 0)
			rev.push_back(prevTok[k]);
		k = prevPos[k];
	}
	for (int i = (int)rev.size() - 1; i >= 0; --i)
		outIds.push_back(rev[i]);
}

bool SemanticParser::embed(const Common::String &text, float *outVec) const {
	Common::Array<int> ids;
	viterbi(text, ids);
	if (ids.empty())
		return false;

	for (uint16 d = 0; d < _dim; ++d)
		outVec[d] = 0.0f;

	for (uint i = 0; i < ids.size(); ++i) {
		const int8 *row = &_vectors[(uint32)_tokens[ids[i]].vecOffset * _dim];
		for (uint16 d = 0; d < _dim; ++d)
			outVec[d] += (float)row[d];
	}

	const float inv = _scale / (float)ids.size();
	float norm = 0.0f;
	for (uint16 d = 0; d < _dim; ++d) {
		outVec[d] *= inv;
		norm += outVec[d] * outVec[d];
	}
	if (norm <= 0.0f)
		return false;

	norm = sqrtf(norm);
	for (uint16 d = 0; d < _dim; ++d)
		outVec[d] /= norm;
	return true;
}

void SemanticParser::stemCandidates(const Common::String &word,
                                    Common::Array<Common::String> &out) {
	out.clear();
	out.push_back(word);
	for (uint i = 0; i < ARRAYSIZE(kEndings); ++i) {
		const uint elen = strlen(kEndings[i]);
		if (word.size() > elen &&
		    memcmp(word.c_str() + word.size() - elen, kEndings[i], elen) == 0) {
			out.push_back(Common::String(word.c_str(), word.size() - elen));
		}
	}
}

float SemanticParser::lexicalScore(const Common::Array<Common::String> &forms,
                                   const Common::String &entry) const {
	if (entry.empty())
		return 0.0f;
	for (uint i = 0; i < forms.size(); ++i) {
		if (forms[i] == entry)
			return 1.0f;
	}
	for (uint i = 0; i < forms.size(); ++i) {
		const Common::String &f = forms[i];
		if (f.hasPrefix(entry) || entry.hasPrefix(f))
			return 0.5f;
	}
	return 0.0f;
}

void SemanticParser::rankToken(const Common::String &token, bool wantVerb,
                               const Common::Array<uint16> &allowed,
                               Common::Array<Match> &out, uint maxOut) const {
	out.clear();
	if (!_loaded)
		return;

	Common::Array<Common::String> forms;
	stemCandidates(token, forms);

	// Embed every surface form once; the score of an entry is the best over
	// all of them.
	Common::Array<float> qbuf(forms.size() * _dim);
	Common::Array<bool> qok(forms.size());
	for (uint i = 0; i < forms.size(); ++i)
		qok[i] = embed(forms[i], &qbuf[(uint32)i * _dim]);

	// Per-group best score.
	Common::HashMap<uint16, float> best;
	for (uint e = 0; e < _entries.size(); ++e) {
		const Entry &ent = _entries[e];

		if (!allowed.empty()) {
			bool found = false;
			for (uint a = 0; a < allowed.size(); ++a) {
				if (allowed[a] == ent.gid) {
					found = true;
					break;
				}
			}
			if (!found)
				continue;
		}

		const uint gi = _groupIndex.getValOrDefault(ent.gid, 0);
		if (gi < _groups.size() && _groups[gi].verb != wantVerb)
			continue;

		float sim = 0.0f;
		const float *ev = &_entryVectors[(uint32)e * _dim];
		for (uint i = 0; i < forms.size(); ++i) {
			if (!qok[i])
				continue;
			const float *q = &qbuf[(uint32)i * _dim];
			float dot = 0.0f;
			for (uint16 d = 0; d < _dim; ++d)
				dot += q[d] * ev[d];
			if (dot > sim)
				sim = dot;
		}
		if (sim < 0.0f)
			sim = 0.0f;

		const float lex = lexicalScore(forms, ent.text);
		const float score = (sim * 0.45f + lex) * (0.25f + 0.75f * ent.weight);

		if (!best.contains(ent.gid) || score > best[ent.gid])
			best[ent.gid] = score;
	}

	// Selection sort over the (small) candidate set.
	for (uint pick = 0; pick < maxOut; ++pick) {
		uint16 bestGid = 0;
		float bestScore = -1e30f;
		bool any = false;
		for (Common::HashMap<uint16, float>::const_iterator it = best.begin();
		     it != best.end(); ++it) {
			bool taken = false;
			for (uint o = 0; o < out.size(); ++o) {
				if (out[o].gid == it->_key) {
					taken = true;
					break;
				}
			}
			if (taken)
				continue;
			if (!any || it->_value > bestScore) {
				any = true;
				bestScore = it->_value;
				bestGid = it->_key;
			}
		}
		if (!any)
			break;
		Match m;
		m.gid = bestGid;
		m.score = bestScore;
		m.verb = isVerbGroup(bestGid);
		out.push_back(m);
	}
}

Common::String SemanticParser::groupName(uint16 gid) const {
	if (!_groupIndex.contains(gid))
		return Common::String();
	return _groups[_groupIndex[gid]].name;
}

bool SemanticParser::isVerbGroup(uint16 gid) const {
	if (!_groupIndex.contains(gid))
		return false;
	return _groups[_groupIndex[gid]].verb;
}

} // End of namespace Agi
