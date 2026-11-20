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

#ifndef _SB_INTERNAL_CARET_MAP_H
#define _SB_INTERNAL_CARET_MAP_H

#include <SheenBidi/SBCaretMap.h>
#include <SheenBidi/SBLine.h>

#include <API/SBBase.h>
#include <Core/Object.h>

/**
 * A single visual cell. Cells are stored in visual order, i.e. the order in which the code points
 * appear on screen from left to right.
 */
typedef struct _CaretCell {
    SBUInteger sourceOffset; /**< Absolute offset of the leading code unit of the code point. */
    SBUInteger sourceLength; /**< Number of source code units covered by the code point. */
    SBUInteger visualIndex;  /**< Position of the cell in the visual ordering. */
    SBLevel level;           /**< Resolved embedding level of the code point. */
} CaretCell;

typedef struct _SBCaretMap {
    ObjectBase _base;
    SBLineRef _line;
    CaretCell *cells;        /**< Visual cells, indexed by visual index. */
    SBUInteger cellCount;    /**< Number of visual cells. */
    SBUInteger *lookup;      /**< Maps a line-relative code unit index to its visual cell. */
    SBSelectionFragment *fragments; /**< Scratch storage returned by SBCaretMapGetSelectionFragments. */
    SBUInteger fragmentCapacity;
} SBCaretMap;

#endif
