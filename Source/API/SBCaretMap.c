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

#include <stddef.h>

#include <API/SBAssert.h>
#include <API/SBBase.h>
#include <API/SBCodepointSequence.h>
#include <API/SBLine.h>
#include <Core/Memory.h>
#include <Core/Object.h>

#include "SBCaretMap.h"

typedef SBCaretMap *SBMutableCaretMapRef;

#define CARETMAP    0
#define CELLS       1
#define LOOKUP      2
#define FRAGMENTS   3
#define CHUNK_COUNT 4

static void FinalizeCaretMap(ObjectRef object)
{
    SBMutableCaretMapRef caretMap = object;
    SBLineRelease(caretMap->_line);
}

/*
 * Finds the visible cell reached from the line-relative code unit index while moving forward.
 * Continuation code units resolve to their own cell and invisible control characters are skipped.
 */
static SBUInteger ResolveCellForward(SBCaretMapRef caretMap, SBUInteger relativeIndex)
{
    const SBUInteger *lookup = caretMap->lookup;
    SBUInteger length = caretMap->_line->length;
    SBUInteger index = relativeIndex;

    while (index < length && lookup[index] == SBInvalidIndex) {
        index += 1;
    }

    if (index < length) {
        return lookup[index];
    }

    /* No visible cell on the right: fall back to the last visible cell on the left. */
    index = relativeIndex;

    while (index > 0 && lookup[index - 1] == SBInvalidIndex) {
        index -= 1;
    }

    if (index == 0) {
        return SBInvalidIndex;
    }

    return lookup[index - 1];
}

/*
 * Finds the visible cell reached from the line-relative code unit boundary while moving backward.
 * The boundary lies after code unit (relativeIndex - 1). Invisible control characters are skipped.
 */
static SBUInteger ResolveCellBackward(SBCaretMapRef caretMap, SBUInteger relativeIndex)
{
    const SBUInteger *lookup = caretMap->lookup;
    SBUInteger index = relativeIndex;

    while (index > 0 && lookup[index - 1] == SBInvalidIndex) {
        index -= 1;
    }

    if (index == 0) {
        return SBInvalidIndex;
    }

    return lookup[index - 1];
}

/*
 * The visual boundary at which a forward (logically leading) caret movement enters the cell.
 */
static SBUInteger LeadingBoundary(const CaretCell *cell)
{
    /* LTR cells read left to right, so the leading edge is the left one. */
    if (!(cell->level & 1)) {
        return cell->visualIndex;
    }

    /* RTL cells read right to left, so the leading edge is the right one. */
    return cell->visualIndex + 1;
}

/*
 * The visual boundary at which a backward (logically trailing) caret movement leaves the cell.
 */
static SBUInteger TrailingBoundary(const CaretCell *cell)
{
    if (!(cell->level & 1)) {
        return cell->visualIndex + 1;
    }

    return cell->visualIndex;
}

SBCaretMapRef SBCaretMapCreate(SBLineRef line)
{
    SBMutableCaretMapRef caretMap = NULL;

    if (line) {
        const SBCodepointSequence *sequence = &line->codepointSequence;
        SBUInteger lineLength = line->length;
        SBUInteger chunkSizes[CHUNK_COUNT] = { 0 };
        void *pointers[CHUNK_COUNT] = { NULL };
        SBUInteger cellCount = 0;
        SBUInteger runIndex;

        /* First pass: count the visible code points. Each code point owns one cell. */
        for (runIndex = 0; runIndex < line->runCount; runIndex++) {
            const SBRun *run = &line->fixedRuns[runIndex];
            SBUInteger runLimit = run->offset + run->length;

            if (!(run->level & 1)) {
                SBUInteger stringIndex = run->offset;

                while (stringIndex < runLimit) {
                    SBUInteger initialIndex = stringIndex;
                    SBCodepointSequenceGetCodepointAt(sequence, &stringIndex);

                    /* Forward decoding always consumes at least one code unit. */
                    if (stringIndex > initialIndex) {
                        cellCount += 1;
                    } else {
                        stringIndex = initialIndex + 1;
                    }
                }
            } else {
                SBUInteger stringIndex = runLimit;

                while (stringIndex > run->offset) {
                    SBUInteger endIndex = stringIndex;
                    SBCodepointSequenceGetCodepointBefore(sequence, &stringIndex);

                    /* A degenerate backward decode must still make progress to avoid an endless or
                     * duplicated cell on malformed input. */
                    if (stringIndex >= endIndex) {
                        stringIndex = endIndex - 1;
                    }

                    cellCount += 1;
                }
            }
        }

        chunkSizes[CARETMAP] = sizeof(SBCaretMap);
        chunkSizes[CELLS] = sizeof(CaretCell) * (cellCount > 0 ? cellCount : 1);
        chunkSizes[LOOKUP] = sizeof(SBUInteger) * (lineLength > 0 ? lineLength : 1);
        /* At most one fragment per cell is ever required. */
        chunkSizes[FRAGMENTS] = sizeof(SBSelectionFragment) * (cellCount > 0 ? cellCount : 1);

        caretMap = ObjectCreate(chunkSizes, CHUNK_COUNT, pointers, &FinalizeCaretMap);

        if (caretMap) {
            CaretCell *cells = pointers[CELLS];
            SBUInteger *lookup = pointers[LOOKUP];
            SBUInteger visualIndex = 0;
            SBUInteger unitIndex;

            caretMap->_line = SBLineRetain(line);
            caretMap->cells = cells;
            caretMap->cellCount = cellCount;
            caretMap->lookup = lookup;
            caretMap->fragments = pointers[FRAGMENTS];
            caretMap->fragmentCapacity = cellCount;

            /* Every code unit starts as invisible until a visible code point claims it. */
            for (unitIndex = 0; unitIndex < lineLength; unitIndex++) {
                lookup[unitIndex] = SBInvalidIndex;
            }

            /*
             * Second pass: the run array of a line is already in visual order. Within an LTR run
             * the code points keep their logical order, within an RTL run they are reversed.
             */
            for (runIndex = 0; runIndex < line->runCount; runIndex++) {
                const SBRun *run = &line->fixedRuns[runIndex];
                SBUInteger runLimit = run->offset + run->length;

                if (!(run->level & 1)) {
                    SBUInteger stringIndex = run->offset;

                    while (stringIndex < runLimit) {
                        SBUInteger initialIndex = stringIndex;
                        SBUInteger nextIndex;
                        CaretCell *cell;
                        SBUInteger relative;

                        SBCodepointSequenceGetCodepointAt(sequence, &stringIndex);

                        /* Guarantee progress even when a malformed sequence fails to decode. */
                        if (stringIndex <= initialIndex) {
                            stringIndex = initialIndex + 1;
                        }

                        nextIndex = stringIndex;
                        cell = &cells[visualIndex];

                        cell->sourceOffset = initialIndex;
                        cell->sourceLength = nextIndex - initialIndex;
                        cell->level = run->level;
                        cell->visualIndex = visualIndex;

                        for (relative = initialIndex - line->offset;
                             relative < nextIndex - line->offset; relative++) {
                            lookup[relative] = visualIndex;
                        }

                        visualIndex += 1;
                    }
                } else {
                    SBUInteger stringIndex = runLimit;

                    while (stringIndex > run->offset) {
                        SBUInteger endIndex = stringIndex;
                        SBUInteger beginIndex;
                        CaretCell *cell;
                        SBUInteger relative;

                        SBCodepointSequenceGetCodepointBefore(sequence, &stringIndex);

                        /* Guarantee progress even when a malformed sequence fails to decode. */
                        if (stringIndex >= endIndex) {
                            stringIndex = endIndex - 1;
                        }

                        beginIndex = stringIndex;
                        cell = &cells[visualIndex];

                        cell->sourceOffset = beginIndex;
                        cell->sourceLength = endIndex - beginIndex;
                        cell->level = run->level;
                        cell->visualIndex = visualIndex;

                        for (relative = beginIndex - line->offset;
                             relative < endIndex - line->offset; relative++) {
                            lookup[relative] = visualIndex;
                        }

                        visualIndex += 1;
                    }
                }
            }

            SBAssert(visualIndex == cellCount);
        }
    }

    return caretMap;
}

SBUInteger SBCaretMapGetCellCount(SBCaretMapRef caretMap)
{
    if (!caretMap) {
        return 0;
    }

    return caretMap->cellCount;
}

SBUInteger SBCaretMapGetSourceOffset(SBCaretMapRef caretMap, SBUInteger visualIndex)
{
    if (caretMap && visualIndex < caretMap->cellCount) {
        return caretMap->cells[visualIndex].sourceOffset;
    }

    return SBInvalidIndex;
}

SBUInteger SBCaretMapGetSourceLength(SBCaretMapRef caretMap, SBUInteger visualIndex)
{
    if (caretMap && visualIndex < caretMap->cellCount) {
        return caretMap->cells[visualIndex].sourceLength;
    }

    return 0;
}

SBUInteger SBCaretMapGetLeadingOffset(SBCaretMapRef caretMap, SBUInteger visualPosition)
{
    if (!caretMap || caretMap->cellCount == 0) {
        return SBInvalidIndex;
    }

    if (visualPosition == 0) {
        const CaretCell *cell = &caretMap->cells[0];

        /* Prefer the cell whose leading edge sits on the leftmost visual boundary. */
        if (LeadingBoundary(cell) == 0) {
            return cell->sourceOffset;
        }

        return cell->sourceOffset + cell->sourceLength;
    }

    if (visualPosition >= caretMap->cellCount) {
        const CaretCell *cell = &caretMap->cells[caretMap->cellCount - 1];

        /* Prefer the cell whose leading edge sits on the rightmost visual boundary. */
        if (LeadingBoundary(cell) == caretMap->cellCount) {
            return cell->sourceOffset;
        }

        return cell->sourceOffset + cell->sourceLength;
    }

    /*
     * Prefer the cell on the right when it is entered from this boundary while moving forward;
     * otherwise fall back to the cell on the left.
     */
    {
        const CaretCell *right = &caretMap->cells[visualPosition];

        if (LeadingBoundary(right) == visualPosition) {
            return right->sourceOffset;
        }

        {
            const CaretCell *left = &caretMap->cells[visualPosition - 1];

            return left->sourceOffset + left->sourceLength;
        }
    }
}

SBUInteger SBCaretMapGetTrailingOffset(SBCaretMapRef caretMap, SBUInteger visualPosition)
{
    if (!caretMap || caretMap->cellCount == 0) {
        return SBInvalidIndex;
    }

    if (visualPosition == 0) {
        const CaretCell *cell = &caretMap->cells[0];

        /* Prefer the cell whose trailing edge sits on the leftmost visual boundary. */
        if (TrailingBoundary(cell) == 0) {
            return cell->sourceOffset + cell->sourceLength;
        }

        return cell->sourceOffset;
    }

    if (visualPosition >= caretMap->cellCount) {
        const CaretCell *cell = &caretMap->cells[caretMap->cellCount - 1];

        if (TrailingBoundary(cell) == caretMap->cellCount) {
            return cell->sourceOffset + cell->sourceLength;
        }

        return cell->sourceOffset;
    }

    /*
     * Prefer the cell on the left when it is left through this boundary while moving backward;
     * otherwise fall back to the cell on the right.
     */
    {
        const CaretCell *left = &caretMap->cells[visualPosition - 1];

        if (TrailingBoundary(left) == visualPosition) {
            return left->sourceOffset + left->sourceLength;
        }

        {
            const CaretCell *right = &caretMap->cells[visualPosition];

            return right->sourceOffset;
        }
    }
}

SBUInteger SBCaretMapGetVisualIndex(SBCaretMapRef caretMap, SBUInteger sourceOffset)
{
    if (!caretMap || caretMap->cellCount == 0) {
        return SBInvalidIndex;
    }

    {
        SBLineRef line = caretMap->_line;
        SBUInteger relativeIndex = sourceOffset - line->offset;

        if (sourceOffset < line->offset || relativeIndex >= line->length) {
            return SBInvalidIndex;
        }

        return ResolveCellForward(caretMap, relativeIndex);
    }
}

static SBUInteger ClampRelativeOffset(SBCaretMapRef caretMap, SBUInteger sourceOffset)
{
    SBLineRef line = caretMap->_line;

    if (sourceOffset <= line->offset) {
        return 0;
    }

    if (sourceOffset >= line->offset + line->length) {
        return line->length;
    }

    return sourceOffset - line->offset;
}

SBUInteger SBCaretMapGetLeadingVisualPosition(SBCaretMapRef caretMap, SBUInteger sourceOffset)
{
    if (!caretMap || caretMap->cellCount == 0) {
        return 0;
    }

    {
        SBUInteger relativeIndex = ClampRelativeOffset(caretMap, sourceOffset);

        /* The end boundary belongs to the last logical code point, reached backward. */
        if (relativeIndex == caretMap->_line->length) {
            SBUInteger cellIndex = ResolveCellBackward(caretMap, relativeIndex);

            if (cellIndex == SBInvalidIndex) {
                return 0;
            }

            return TrailingBoundary(&caretMap->cells[cellIndex]);
        }

        /* Every other boundary belongs to the cell entered while moving forward. */
        {
            SBUInteger cellIndex = ResolveCellForward(caretMap, relativeIndex);

            if (cellIndex == SBInvalidIndex) {
                return 0;
            }

            return LeadingBoundary(&caretMap->cells[cellIndex]);
        }
    }
}

SBUInteger SBCaretMapGetTrailingVisualPosition(SBCaretMapRef caretMap, SBUInteger sourceOffset)
{
    if (!caretMap || caretMap->cellCount == 0) {
        return 0;
    }

    {
        SBUInteger relativeIndex = ClampRelativeOffset(caretMap, sourceOffset);

        /* The start boundary belongs to the first logical code point, reached forward. */
        if (relativeIndex == 0) {
            SBUInteger cellIndex = ResolveCellForward(caretMap, 0);

            if (cellIndex == SBInvalidIndex) {
                return 0;
            }

            return LeadingBoundary(&caretMap->cells[cellIndex]);
        }

        /* Every other boundary belongs to the cell left behind while moving backward. */
        {
            SBUInteger cellIndex = ResolveCellBackward(caretMap, relativeIndex);

            if (cellIndex == SBInvalidIndex) {
                return 0;
            }

            return TrailingBoundary(&caretMap->cells[cellIndex]);
        }
    }
}

SBUInteger SBCaretMapGetSelectionFragments(SBCaretMapRef caretMap,
    SBUInteger sourceOffset, SBUInteger sourceLength,
    const SBSelectionFragment **fragments)
{
    SBUInteger fragmentCount = 0;

    if (fragments) {
        *fragments = NULL;
    }

    if (caretMap && fragments && caretMap->cellCount > 0 && sourceLength > 0) {
        SBLineRef line = caretMap->_line;
        SBUInteger lineBegin = line->offset;
        SBUInteger lineEnd = line->offset + line->length;
        SBUInteger selectionEnd = sourceOffset + sourceLength;
        SBUInteger visualIndex;
        SBBoolean previousSelected = SBFalse;

        /* A wrapped selection end means the range conceptually runs up to the line end. */
        if (selectionEnd < sourceOffset) {
            selectionEnd = lineEnd;
        }

        /* Clamp the selection to the line range. */
        if (sourceOffset < lineBegin) {
            sourceOffset = lineBegin;
        }
        if (selectionEnd > lineEnd) {
            selectionEnd = lineEnd;
        }

        /*
         * Cells are stored in visual order. Visually adjacent cells sharing one embedding level
         * form a single highlight, even though their source offsets run backwards inside an RTL
         * run. Each fragment therefore records the smallest source offset and the largest source
         * end of the cells it groups.
         */
        for (visualIndex = 0; visualIndex < caretMap->cellCount; visualIndex++) {
            const CaretCell *cell = &caretMap->cells[visualIndex];
            SBUInteger cellBegin = cell->sourceOffset;
            SBUInteger cellEnd = cellBegin + cell->sourceLength;
            SBBoolean isSelected = (cellBegin < selectionEnd && cellEnd > sourceOffset);

            if (!isSelected) {
                previousSelected = SBFalse;
                continue;
            }

            if (previousSelected && caretMap->cells[visualIndex - 1].level == cell->level) {
                SBSelectionFragment *fragment = &caretMap->fragments[fragmentCount - 1];
                SBUInteger fragmentEnd = fragment->offset + fragment->length;

                if (cellBegin < fragment->offset) {
                    fragment->offset = cellBegin;
                }
                if (cellEnd > fragmentEnd) {
                    fragmentEnd = cellEnd;
                }

                fragment->length = fragmentEnd - fragment->offset;
            } else if (fragmentCount < caretMap->fragmentCapacity) {
                SBSelectionFragment *fragment = &caretMap->fragments[fragmentCount];
                fragment->offset = cellBegin;
                fragment->length = cellEnd - cellBegin;
                fragmentCount += 1;
            }

            previousSelected = SBTrue;
        }

        if (fragmentCount > 0) {
            *fragments = caretMap->fragments;
        }
    }

    return fragmentCount;
}

SBCaretMapRef SBCaretMapRetain(SBCaretMapRef caretMap)
{
    return ObjectRetain((ObjectRef)caretMap);
}

void SBCaretMapRelease(SBCaretMapRef caretMap)
{
    ObjectRelease((ObjectRef)caretMap);
}
