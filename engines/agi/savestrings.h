/* ScummVM - Graphic Adventure Engine (GPLv3+, see translation.h for the header) */

#ifndef AGI_SAVESTRINGS_H
#define AGI_SAVESTRINGS_H

#include "common/str.h"
#include "common/stream.h"

namespace Agi {

/**
 * The script string table of a version-12 save (UTF-8 text mode): for each
 * of @p count slots of @p stride bytes, a u8 length and that many bytes;
 * then for each slot the English original of a translated answer
 * (AgiGame::stringAnswer), the same way. A length must be < stride.
 */
void writeUtf8Strings(Common::WriteStream &out, const char *table, uint stride, uint count, const Common::String *answers);

/** Read what writeUtf8Strings() wrote; false on a bad length or a short stream. */
bool readUtf8Strings(Common::ReadStream &in, char *table, uint stride, uint count, Common::String *answers);

/**
 * A version-11 save read in a UTF-8 session: each of the @p count slots holds
 * code page 850 text; turn it into UTF-8, clipped to @p maxCells cells.
 * Pure ASCII is left as it is.
 */
void convertLegacyStrings(char *table, uint stride, uint count, int16 maxCells);

} // End of namespace Agi

#endif
