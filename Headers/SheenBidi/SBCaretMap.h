/*
 * Copyright (C) 2026 Muhammad Tayyab Akram
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _SB_PUBLIC_CARET_MAP_H
#define _SB_PUBLIC_CARET_MAP_H

#include <SheenBidi/SBBase.h>
#include <SheenBidi/SBLine.h>

SB_EXTERN_C_BEGIN

typedef const struct _SBCaretMap *SBCaretMapRef;

/**
 * A structure that describes a single, visually contiguous highlight rectangle of a selection.
 *
 * A logical selection range may map to more than one visual fragment whenever it spans runs of
 * opposite directions (for example, an English word embedded inside Arabic text). Each fragment is
 * expressed in the same code unit space as the source string passed to the algorithm.
 */
typedef struct _SBSelectionFragment {
    SBUInteger offset; /**< The index to the first code unit of the fragment in source string. */
    SBUInteger length; /**< The number of code units covering the length of the fragment. */
} SBSelectionFragment;

/**
 * Creates a caret map for the specified line.
 *
 * A caret map records, for every code unit of the line, the position at which it is rendered after
 * the bidirectional reordering of rules L1-L2 has been applied. It is the bridge between the visual
 * geometry computed by a shaping/rendering engine and the logical offsets of the source string in
 * its native encoding (UTF-8, UTF-16 or UTF-32).
 *
 * The map keeps a reference to the line, so the line may be released as soon as the map has been
 * created. The source string itself must remain alive for as long as the map is in use.
 *
 * Multi-code-unit code points (UTF-8 continuation bytes, UTF-16 trailing surrogates) are kept glued
 * to their leading code unit. They therefore share a single visual cell and can never host a caret
 * boundary on their own, which prevents the caret from being inserted into the middle of a code
 * point or a mirrored/shaped cluster.
 *
 * @param line
 *      The line whose visual-to-logical mapping is built.
 * @return
 *      A reference to a caret map object if the call was successful, NULL otherwise.
 */
SB_PUBLIC SBCaretMapRef SBCaretMapCreate(SBLineRef line);

/**
 * Returns the number of visual cells in the map.
 *
 * A visual cell corresponds to one decoded code point (one or more code units) and occupies one
 * slot in the visual ordering of the line. Bidirectional control characters that the algorithm
 * removed from the visual stream are not counted.
 *
 * @param caretMap
 *      The caret map whose cell count is returned.
 * @return
 *      The number of visual cells in the map. It is zero for an empty line.
 */
SB_PUBLIC SBUInteger SBCaretMapGetCellCount(SBCaretMapRef caretMap);

/**
 * Determines the source offset of the code unit that is rendered at a visual cell.
 *
 * @param caretMap
 *      The caret map to query.
 * @param visualIndex
 *      The visual cell index, ranging from 0 to SBCaretMapGetCellCount() - 1.
 * @return
 *      The absolute offset of the first code unit of the code point rendered at the given visual
 *      cell, or SBInvalidIndex if the index is out of bounds or the map is empty.
 */
SB_PUBLIC SBUInteger SBCaretMapGetSourceOffset(SBCaretMapRef caretMap, SBUInteger visualIndex);

/**
 * Determines the number of source code units occupied by the code point rendered at a visual cell.
 *
 * @param caretMap
 *      The caret map to query.
 * @param visualIndex
 *      The visual cell index, ranging from 0 to SBCaretMapGetCellCount() - 1.
 * @return
 *      The code unit length of the rendered code point (for example 4 for an astral character in
 *      UTF-8, 2 in UTF-16 and 1 in UTF-32), or zero if the index is out of bounds or the map is
 *      empty.
 */
SB_PUBLIC SBUInteger SBCaretMapGetSourceLength(SBCaretMapRef caretMap, SBUInteger visualIndex);

/**
 * Maps a caret position measured on the visual line to a source insertion offset.
 *
 * The visual position is expressed as the boundary lying before the visual cell with the given
 * index. Valid positions therefore range from 0 (the left edge of the line) to
 * SBCaretMapGetCellCount() (the right edge).
 *
 * At the junction between an LTR and an RTL run, the shared visual boundary represents two
 * distinct logical insertion points. The bias argument disambiguates them:
 *
 * - SBCaretMapGetLeadingOffset() returns the offset at which typing would insert the next code unit
 *   when moving forward logically through the source string.
 * - SBCaretMapGetTrailingOffset() returns the offset at which typing would insert the next code
 *   unit when moving backward logically through the source string.
 *
 * Inside a uniformly directed run both functions return the same value.
 *
 * Lone surrogates, malformed byte sequences and noncharacters are decoded into replacement code
 * points like the rest of the library and treated as ordinary cells; they never split a code point.
 *
 * @param caretMap
 *      The caret map to query.
 * @param visualPosition
 *      The visual caret boundary, ranging from 0 to SBCaretMapGetCellCount().
 * @return
 *      The absolute source code unit offset of the insertion point, clamped to the line range when
 *      the position is out of bounds. SBInvalidIndex is returned for an empty map.
 */
SB_PUBLIC SBUInteger SBCaretMapGetLeadingOffset(SBCaretMapRef caretMap, SBUInteger visualPosition);
SB_PUBLIC SBUInteger SBCaretMapGetTrailingOffset(SBCaretMapRef caretMap, SBUInteger visualPosition);

/**
 * Determines the visual cell index at which a source code unit is rendered.
 *
 * When the offset points at a continuation code unit (a UTF-8 continuation byte or a UTF-16
 * trailing surrogate), the cell of its leading code unit is returned.
 *
 * @param caretMap
 *      The caret map to query.
 * @param sourceOffset
 *      The absolute source code unit offset to locate. It must fall within the line range.
 * @return
 *      The visual cell index of the code point containing the given offset, or SBInvalidIndex if
 *      the offset falls outside the line.
 */
SB_PUBLIC SBUInteger SBCaretMapGetVisualIndex(SBCaretMapRef caretMap, SBUInteger sourceOffset);

/**
 * Maps a source insertion offset to its visual caret boundary.
 *
 * As with SBCaretMapGetLeadingOffset(), the visual boundary shared by two opposite-direction runs
 * carries two logical meanings. The leading variant returns the boundary reached when entering the
 * location while moving forward through the source string, the trailing variant when moving
 * backward.
 *
 * @param caretMap
 *      The caret map to query.
 * @param sourceOffset
 *      The absolute source code unit offset, ranging from the line offset to the line offset plus
 *      its length. Out-of-range values are clamped to the nearest line boundary.
 * @return
 *      The visual boundary index, ranging from 0 to SBCaretMapGetCellCount().
 */
SB_PUBLIC SBUInteger SBCaretMapGetLeadingVisualPosition(SBCaretMapRef caretMap, SBUInteger sourceOffset);
SB_PUBLIC SBUInteger SBCaretMapGetTrailingVisualPosition(SBCaretMapRef caretMap, SBUInteger sourceOffset);

/**
 * Splits a logical source range into visually contiguous selection fragments.
 *
 * The fragments are returned in visual order and partition exactly the code points whose source
 * code units lie within [sourceOffset, sourceOffset + sourceLength). Code units belonging to the
 * same rendered code point are always kept together, even when the range starts or ends inside a
 * multi-code-unit code point. Bidirectional control characters, which have no visual
 * representation, are skipped without producing fragments.
 *
 * @param caretMap
 *      The caret map to query.
 * @param sourceOffset
 *      The absolute source code unit offset marking the start of the selection.
 * @param sourceLength
 *      The number of source code units covered by the selection.
 * @param fragments
 *      On output, an array of selection fragments. The referenced storage is owned by the caret map
 *      and remains valid until the map is released or queried again through this function.
 * @return
 *      The number of fragments written to the output array. Zero is returned when the selection is
 *      empty, lies entirely outside the line or only covers invisible control characters.
 */
SB_PUBLIC SBUInteger SBCaretMapGetSelectionFragments(SBCaretMapRef caretMap,
    SBUInteger sourceOffset, SBUInteger sourceLength,
    const SBSelectionFragment **fragments);

/**
 * Increments the reference count of a caret map object.
 *
 * @param caretMap
 *      The caret map object whose reference count will be incremented.
 * @return
 *      The same caret map object passed in as the parameter.
 */
SB_PUBLIC SBCaretMapRef SBCaretMapRetain(SBCaretMapRef caretMap);

/**
 * Decrements the reference count of a caret map object. The object will be deallocated when its
 * reference count reaches zero.
 *
 * @param caretMap
 *      The caret map object whose reference count will be decremented.
 */
SB_PUBLIC void SBCaretMapRelease(SBCaretMapRef caretMap);

SB_EXTERN_C_END

#endif
