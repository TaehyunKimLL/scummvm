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

/* sizeof(svf_face) as the C library sees it: SvfFace checks it against its own view (graphics/hires_text/svfface.h). */
#include "graphics/svf/svf.h"

#if !SVF_JMAP || !SVF_JSET_BIG || SVF_JMAP_RECOG
#error "graphics/svf: configure must add -DSVF_JMAP=1 -DSVF_JSET_BIG=1 -DSVF_JMAP_RECOG=0 to DEFINES"
#endif

const size_t scummvm_svf_face_size = sizeof(svf_face);
