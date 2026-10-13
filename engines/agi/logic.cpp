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

#include "agi/agi.h"
#include "agi/textcells.h"

namespace Agi {

/**
 * Decode logic resource
 * This function decodes messages from the specified raw logic resource
 * into a message list.
 * @param logicNr  The number of the logic resource to decode.
 */
int AgiEngine::decodeLogic(int16 logicNr) {
	AgiLogic &logic = _game.logics[logicNr];
	AgiDir &dirLogic = _game.dirLogic[logicNr];

	// bytecode section:
	// u16  bytecode size
	// u8[] bytecode
	uint16 bytecodeSize = READ_LE_UINT16(logic.data);

	// message section:
	// u8       message count
	// u16      messages size (2 + offsets + strings)
	// u16[]    string offsets (relative to message section + 1)
	// string[] strings (null terminated, possibly encrypted)
	int messageSectionPos = 2 + bytecodeSize;
	uint8 messageCount = logic.data[messageSectionPos];
	uint16 messagesSize = READ_LE_UINT16(logic.data + messageSectionPos + 1);
	int stringOffsetsPos = messageSectionPos + 3;
	int stringsPos = stringOffsetsPos + (2 * messageCount);
	int stringsSize = messagesSize - 2 - (2 * messageCount);

	// decrypt the message strings if the logic was not compressed
	// and the logic has messages.
	if ((~dirLogic.flags & RES_COMPRESSED) && messageCount > 0) {
		decrypt(logic.data + stringsPos, stringsSize);
	}

	// reset logic pointers
	logic.sIP = 2;
	logic.cIP = 2;
	logic.size = messageSectionPos; // exclude messages from logic size

	// allocate list of pointers to message texts. last entry is null.
	logic.numTexts = messageCount;
	logic.texts = (const char **)calloc(1 + logic.numTexts, sizeof(char *));
	if (logic.texts == nullptr) {
		free(logic.data);
		logic.data = nullptr;
		logic.numTexts = 0;
		return errNotEnoughMemory;
	}

	// populate list of pointers to message texts
	for (int i = 0; i < messageCount; i++) {
		int stringOffset = READ_LE_UINT16(logic.data + stringOffsetsPos + (i * 2));
		if (stringOffset != 0) {
			// offset is relative to the message section + 1
			stringOffset += messageSectionPos + 1;
			logic.texts[i] = (const char *)(logic.data + stringOffset);
		} else {
			// TODO: does this happen? when is a string offset zero?
			logic.texts[i] = "";
		}
	}

	if (textIsUtf8())
		translateLogicTexts(logicNr);

	// set loaded flag
	dirLogic.flags |= RES_LOADED;
	return errOK;
}

/**
 * UTF-8 text mode: point every message at its translation, or at a UTF-8
 * copy of the game's own text. The game's bytes stay reachable through
 * rawTexts (compare.strings accepts an answer's English original, design
 * 5.3). Overlay strings live as long as the engine; converted ones go in
 * one block per logic, freed with it.
 */
void AgiEngine::translateLogicTexts(int16 logicNr) {
	AgiLogic &logic = _game.logics[logicNr];
	logic.rawTexts = (const char **)calloc(1 + logic.numTexts, sizeof(char *));
	if (!logic.rawTexts)
		return;
	Common::Array<char> pool;
	Common::Array<int> poolOffset;
	poolOffset.resize(logic.numTexts);
	for (int i = 0; i < logic.numTexts; i++) {
		logic.rawTexts[i] = logic.texts[i];
		poolOffset[i] = -1;
		const Common::String *t = _overlay.message(logicNr, i + 1);
		if (t) {
			logic.texts[i] = t->c_str();
			continue;
		}
		if (!TextCells::hasHighBytes(logic.texts[i]))
			continue;
		const Common::String u = TextOverlay::legacyToUtf8(logic.texts[i], Common::kDos850);
		poolOffset[i] = pool.size();
		for (uint k = 0; k < u.size(); k++)
			pool.push_back(u[k]);
		pool.push_back('\0');
	}
	if (pool.empty())
		return;
	logic.utf8Pool = (char *)malloc(pool.size());
	if (!logic.utf8Pool)
		return;
	memcpy(logic.utf8Pool, pool.data(), pool.size());
	for (int i = 0; i < logic.numTexts; i++) {
		if (poolOffset[i] >= 0)
			logic.texts[i] = logic.utf8Pool + poolOffset[i];
	}
}

/**
 * Unload logic resource
 * This function unloads the specified logic resource, freeing any
 * memory chunks allocated for this resource.
 * @param logicNr  The number of the logic resource to unload
 */
void AgiEngine::unloadLogic(int16 logicNr) {
	AgiLogic &logic = _game.logics[logicNr];
	AgiDir &dirLogic = _game.dirLogic[logicNr];

	if (dirLogic.flags & RES_LOADED) {
		free(logic.data);
		logic.data = nullptr;
		free(logic.texts);
		logic.texts = nullptr;
		free(logic.rawTexts);
		logic.rawTexts = nullptr;
		free(logic.utf8Pool);
		logic.utf8Pool = nullptr;
		logic.numTexts = 0;
		dirLogic.flags &= ~RES_LOADED;
	}

	logic.sIP = 2;
	logic.cIP = 2;
}

} // End of namespace Agi
