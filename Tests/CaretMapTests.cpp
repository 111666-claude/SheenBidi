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

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include <SheenBidi/SheenBidi.h>

#include "CaretMapTests.h"

using namespace std;
using namespace SheenBidi;

/* Arabic letters (RTL strong). */
static const SBUInt32 AIN   = 0x0639;
static const SBUInt32 BEH   = 0x0628;
static const SBUInt32 JEEM  = 0x062C;
/* Latin letters (LTR strong). */
static const SBUInt32 CAP_A = 0x0041;
static const SBUInt32 CAP_B = 0x0042;
static const SBUInt32 CAP_C = 0x0043;
/* Digits, punctuation and marks. */
static const SBUInt32 ONE   = 0x0031;
static const SBUInt32 TWO   = 0x0032;
static const SBUInt32 SPACE = 0x0020;
static const SBUInt32 HAMZA = 0x0654; /* Arabic hamza above, nonspacing mark (Mn). */
static const SBUInt32 EMOJI = 0x1F600; /* Astral character, needs a surrogate pair in UTF-16. */
static const SBUInt32 RLO   = 0x202E; /* Right-to-left override, a BN formatting control. */
static const SBUInt32 PDF   = 0x202C; /* Pop directional formatting, a BN formatting control. */

struct Fixture {
    vector<SBUInt32> codepoints;
    SBAlgorithmRef algorithm;
    SBParagraphRef paragraph;
    SBLineRef line;
    SBCaretMapRef caretMap;

    explicit Fixture(const vector<SBUInt32> &points)
        : codepoints(points), algorithm(NULL), paragraph(NULL), line(NULL), caretMap(NULL)
    {
        SBCodepointSequence sequence;
        sequence.stringEncoding = SBStringEncodingUTF32;
        sequence.stringBuffer = codepoints.data();
        sequence.stringLength = codepoints.size();

        algorithm = SBAlgorithmCreate(&sequence);
        assert(algorithm != NULL);

        paragraph = SBAlgorithmCreateParagraph(algorithm, 0, codepoints.size(), SBLevelDefaultLTR);
        assert(paragraph != NULL);

        line = SBParagraphCreateLine(paragraph, 0, codepoints.size());
        assert(line != NULL);

        caretMap = SBCaretMapCreate(line);
        assert(caretMap != NULL);
    }

    ~Fixture()
    {
        SBCaretMapRelease(caretMap);
        SBLineRelease(line);
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
    }
};

/* Returns the sequence of source offsets as rendered visually, one entry per visual cell. */
static vector<SBUInteger> VisualOffsets(SBCaretMapRef caretMap)
{
    vector<SBUInteger> offsets;
    SBUInteger count = SBCaretMapGetCellCount(caretMap);
    SBUInteger index;

    for (index = 0; index < count; index++) {
        offsets.push_back(SBCaretMapGetSourceOffset(caretMap, index));
    }

    return offsets;
}

static vector<SBUInt8> EncodeUTF8(const vector<SBUInt32> &codepoints)
{
    vector<SBUInt8> units;

    for (SBUInt32 cp : codepoints) {
        if (cp < 0x80) {
            units.push_back(cp);
        } else if (cp < 0x800) {
            units.push_back(0xC0 | (cp >> 6));
            units.push_back(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            units.push_back(0xE0 | (cp >> 12));
            units.push_back(0x80 | ((cp >> 6) & 0x3F));
            units.push_back(0x80 | (cp & 0x3F));
        } else {
            units.push_back(0xF0 | (cp >> 18));
            units.push_back(0x80 | ((cp >> 12) & 0x3F));
            units.push_back(0x80 | ((cp >> 6) & 0x3F));
            units.push_back(0x80 | (cp & 0x3F));
        }
    }

    return units;
}

static vector<SBUInt16> EncodeUTF16(const vector<SBUInt32> &codepoints)
{
    vector<SBUInt16> units;

    for (SBUInt32 cp : codepoints) {
        if (cp < 0x10000) {
            units.push_back((SBUInt16)cp);
        } else {
            SBUInt32 v = cp - 0x10000;
            units.push_back((SBUInt16)(0xD800 + (v >> 10)));
            units.push_back((SBUInt16)(0xDC00 + (v & 0x3FF)));
        }
    }

    return units;
}

void CaretMapTests::testVisualOrder()
{
    /*
     * Mixed paragraph: "ابج ABC 12"
     *
     * The Arabic run is reversed visually while the embedded Latin word and the number keep their
     * internal order. The exact order is derived from the runs produced by the library itself, so
     * this test primarily locks down the cell bookkeeping (offsets, lengths, round trip).
     */
    vector<SBUInt32> points = { AIN, BEH, JEEM, SPACE, CAP_A, CAP_B, CAP_C, SPACE, ONE, TWO };
    Fixture fixture(points);
    SBCaretMapRef caretMap = fixture.caretMap;
    SBUInteger count = SBCaretMapGetCellCount(caretMap);
    SBUInteger index;

    assert(count == points.size());

    /* Every cell maps back to a distinct code point, in source order coverage. */
    vector<SBUInteger> seen(points.size(), 0);

    for (index = 0; index < count; index++) {
        SBUInteger offset = SBCaretMapGetSourceOffset(caretMap, index);
        SBUInteger length = SBCaretMapGetSourceLength(caretMap, index);

        assert(offset < points.size());
        assert(length == 1);
        assert(seen[offset] == 0);
        seen[offset] = 1;

        /* Visual -> source -> visual must be the identity for every leading code unit. */
        assert(SBCaretMapGetVisualIndex(caretMap, offset) == index);
    }

    for (SBUInteger value : seen) {
        assert(value == 1);
    }

    /* The Arabic run itself is reversed: JEEM is rendered before BEH before AIN. */
    vector<SBUInteger> offsets = VisualOffsets(caretMap);
    SBUInteger posJeem = (SBUInteger)-1;
    SBUInteger posBeh = (SBUInteger)-1;
    SBUInteger posAin = (SBUInteger)-1;

    for (index = 0; index < offsets.size(); index++) {
        if (points[offsets[index]] == JEEM) {
            posJeem = index;
        } else if (points[offsets[index]] == BEH) {
            posBeh = index;
        } else if (points[offsets[index]] == AIN) {
            posAin = index;
        }
    }

    assert(posJeem < posBeh && posBeh < posAin);

    /* The Latin word keeps ABC in reading order. */
    SBUInteger posA = (SBUInteger)-1;
    SBUInteger posB = (SBUInteger)-1;
    SBUInteger posC = (SBUInteger)-1;

    for (index = 0; index < offsets.size(); index++) {
        if (points[offsets[index]] == CAP_A) {
            posA = index;
        } else if (points[offsets[index]] == CAP_B) {
            posB = index;
        } else if (points[offsets[index]] == CAP_C) {
            posC = index;
        }
    }

    assert(posA < posB && posB < posC);
}

void CaretMapTests::testCaretPositions()
{
    /*
     * Plain LTR line: every boundary is unambiguous.
     */
    {
        vector<SBUInt32> points = { CAP_A, CAP_B, CAP_C };
        Fixture fixture(points);
        SBCaretMapRef caretMap = fixture.caretMap;
        SBUInteger position;

        for (position = 0; position <= 3; position++) {
            assert(SBCaretMapGetLeadingOffset(caretMap, position) == position);
            assert(SBCaretMapGetTrailingOffset(caretMap, position) == position);
            assert(SBCaretMapGetLeadingVisualPosition(caretMap, position) == position);
            assert(SBCaretMapGetTrailingVisualPosition(caretMap, position) == position);
        }
    }

    /*
     * Plain RTL line: the whole line is one RTL run.
     */
    {
        vector<SBUInt32> points = { AIN, BEH, JEEM };
        Fixture fixture(points);
        SBCaretMapRef caretMap = fixture.caretMap;

        /* Visual order is JEEM, BEH, AIN. */
        vector<SBUInteger> offsets = VisualOffsets(caretMap);
        assert(offsets == (vector<SBUInteger>{ 2, 1, 0 }));

        /* Logical offset 0 (before AIN) is the rightmost visual boundary. */
        assert(SBCaretMapGetLeadingVisualPosition(caretMap, 0) == 3);
        assert(SBCaretMapGetTrailingVisualPosition(caretMap, 0) == 3);
        /* Logical offset 3 (after JEEM) is the leftmost visual boundary. */
        assert(SBCaretMapGetLeadingVisualPosition(caretMap, 3) == 0);
        assert(SBCaretMapGetTrailingVisualPosition(caretMap, 3) == 0);
        /* Interior logical offsets mirror: offset 1 -> boundary 2, offset 2 -> boundary 1. */
        assert(SBCaretMapGetLeadingVisualPosition(caretMap, 1) == 2);
        assert(SBCaretMapGetTrailingVisualPosition(caretMap, 1) == 2);
        assert(SBCaretMapGetLeadingVisualPosition(caretMap, 2) == 1);
        assert(SBCaretMapGetTrailingVisualPosition(caretMap, 2) == 1);

        /*
         * Round trip invariant: the leading and trailing insertion offsets offered by all visual
         * boundaries together are exactly the set of valid logical offsets {0..3}.
         */
        {
            vector<SBUInteger> covered(4, 0);
            SBUInteger position;

            for (position = 0; position <= 3; position++) {
                covered[SBCaretMapGetLeadingOffset(caretMap, position)] = 1;
                covered[SBCaretMapGetTrailingOffset(caretMap, position)] = 1;
            }
            assert(covered == (vector<SBUInteger>{ 1, 1, 1, 1 }));
        }
    }

    /*
     * RTL/LTR junction: "اب ABC" resolves to an RTL base paragraph with an embedded LTR Latin run.
     * The shared boundary between the two runs carries two distinct logical insertion points which
     * leading and trailing must keep separable.
     */
    {
        vector<SBUInt32> points = { AIN, BEH, SPACE, CAP_A, CAP_B, CAP_C };
        Fixture fixture(points);
        SBCaretMapRef caretMap = fixture.caretMap;
        vector<SBUInteger> offsets = VisualOffsets(caretMap);

        /* The Latin run is rendered first (left), the Arabic run after it (right). */
        assert(offsets == (vector<SBUInteger>{ 3, 4, 5, 2, 1, 0 }));

        /*
         * Both logical offsets of the junction boundary (offset 3 leaving the Latin word, offset 0
         * entering the Arabic word) collapse onto the same visual boundary. The biases separate
         * them: leading resolves to the forward insertion point while trailing resolves backward.
         */
        assert(SBCaretMapGetLeadingVisualPosition(caretMap, 0) == (SBUInteger)(offsets.size()));
        assert(SBCaretMapGetTrailingVisualPosition(caretMap, 0) == (SBUInteger)(offsets.size()));
        /* Entering the Latin run from its leading edge lands at the leftmost boundary; moving
         * backward into it lands on the shared junction boundary instead. */
        assert(SBCaretMapGetLeadingVisualPosition(caretMap, 3) == 0);
        assert(SBCaretMapGetTrailingVisualPosition(caretMap, 3) == 3);

        /*
         * The internal junction boundary (between the Latin cell C and the Arabic space cell) is
         * touched by two opposite-direction cells. The two biases therefore resolve it to the two
         * distinct logical insertion points: entering the Latin word from its trailing edge
         * (offset 3) and entering the Arabic space forward (offset 1).
         */
        {
            SBUInteger junctionBoundary = 4; /* Between visual cell 3 (C) and cell 4 (space). */
            SBUInteger leading = SBCaretMapGetLeadingOffset(caretMap, junctionBoundary);
            SBUInteger trailing = SBCaretMapGetTrailingOffset(caretMap, junctionBoundary);

            assert(leading != trailing);
            assert(leading == 3);
            assert(trailing == 1);
        }

        /* Coverage invariant: every logical insertion offset is reachable from a visual boundary
         * under one of the two biases. */
        {
            vector<SBUInteger> covered(points.size() + 1, 0);
            SBUInteger boundary;

            for (boundary = 0; boundary <= offsets.size(); boundary++) {
                covered[SBCaretMapGetLeadingOffset(caretMap, boundary)] = 1;
                covered[SBCaretMapGetTrailingOffset(caretMap, boundary)] = 1;
            }

            for (SBUInteger value : covered) {
                assert(value == 1);
            }
        }

        /* Every internal boundary resolves to two in-range source offsets. */
        for (SBUInteger boundary = 1; boundary < offsets.size(); boundary++) {
            assert(SBCaretMapGetLeadingOffset(caretMap, boundary) <= points.size());
            assert(SBCaretMapGetTrailingOffset(caretMap, boundary) <= points.size());
        }

        /* Out-of-range positions are clamped to the line edges. */
        assert(SBCaretMapGetLeadingOffset(caretMap, 1000) == 0);
        assert(SBCaretMapGetTrailingOffset(caretMap, 1000) == 0);
        assert(SBCaretMapGetSourceOffset(caretMap, 1000) == (SBUInteger)(-1));
        assert(SBCaretMapGetSourceLength(caretMap, 1000) == 0);
    }
}

void CaretMapTests::testSelectionFragments()
{
    /*
     * "اب ABC": selecting the Arabic source range inside an RTL run yields one fragment expressed
     * in source offsets, while selecting across the junction yields multiple visual fragments.
     */
    vector<SBUInt32> points = { AIN, BEH, SPACE, CAP_A, CAP_B, CAP_C };
    Fixture fixture(points);
    SBCaretMapRef caretMap = fixture.caretMap;
    const SBSelectionFragment *fragments = NULL;
    SBUInteger count;

    /* Select the two Arabic code points: a single fragment covering source units [0, 2). */
    count = SBCaretMapGetSelectionFragments(caretMap, 0, 2, &fragments);
    assert(count == 1);
    assert(fragments[0].offset == 0);
    assert(fragments[0].length == 2);

    /* Select the Latin word: a single fragment covering source units [3, 6). */
    count = SBCaretMapGetSelectionFragments(caretMap, 3, 3, &fragments);
    assert(count == 1);
    assert(fragments[0].offset == 3);
    assert(fragments[0].length == 3);

    /* Select everything: the two opposite-direction runs form two visual fragments whose source
     * units still partition the whole line. */
    count = SBCaretMapGetSelectionFragments(caretMap, 0, points.size(), &fragments);
    assert(count == 2);
    assert(fragments[0].offset == 3 && fragments[0].length == 3); /* Latin run, drawn first. */
    assert(fragments[1].offset == 0 && fragments[1].length == 3); /* Arabic run, drawn second. */
    {
        SBUInteger covered = 0;
        SBUInteger index;
        for (index = 0; index < count; index++) {
            covered += fragments[index].length;
        }
        assert(covered == points.size());
    }

    /* A selection crossing the junction is split into one fragment per run. */
    count = SBCaretMapGetSelectionFragments(caretMap, 1, 4, &fragments);
    assert(count == 2);
    assert(fragments[0].offset == 3 && fragments[0].length == 2);
    assert(fragments[1].offset == 1 && fragments[1].length == 2);

    /* Selecting the Arabic run plus the shared space stays a single RTL fragment. */
    count = SBCaretMapGetSelectionFragments(caretMap, 0, 3, &fragments);
    assert(count == 1);
    assert(fragments[0].offset == 0 && fragments[0].length == 3);

    /* Empty selection. */
    assert(SBCaretMapGetSelectionFragments(caretMap, 2, 0, &fragments) == 0);

    /* Fully out-of-range selection. */
    assert(SBCaretMapGetSelectionFragments(caretMap, 100, 5, &fragments) == 0);

    /* NULL output pointer is tolerated. */
    assert(SBCaretMapGetSelectionFragments(caretMap, 0, 2, NULL) == 0);
}

void CaretMapTests::testEncodings()
{
    /*
     * The same logical paragraph in all three encodings, featuring an astral character (one UTF-32
     * unit, two UTF-16 units, four UTF-8 units) next to Arabic so that reordering still happens.
     */
    vector<SBUInt32> points = { AIN, BEH, EMOJI, CAP_A };
    vector<SBUInt8> utf8 = EncodeUTF8(points);
    vector<SBUInt16> utf16 = EncodeUTF16(points);

    SBCodepointSequence sequence32;
    sequence32.stringEncoding = SBStringEncodingUTF32;
    sequence32.stringBuffer = points.data();
    sequence32.stringLength = points.size();

    SBCodepointSequence sequence16;
    sequence16.stringEncoding = SBStringEncodingUTF16;
    sequence16.stringBuffer = utf16.data();
    sequence16.stringLength = utf16.size();

    SBCodepointSequence sequence8;
    sequence8.stringEncoding = SBStringEncodingUTF8;
    sequence8.stringBuffer = utf8.data();
    sequence8.stringLength = utf8.size();

    const SBCodepointSequence *sequences[3] = { &sequence8, &sequence16, &sequence32 };
    const SBUInteger emojiLength[3] = { 4, 2, 1 };
    const SBUInteger totalLength[3] = { utf8.size(), utf16.size(), points.size() };
    SBUInteger emojiUnitOffset[3] = {
        EncodeUTF8({ AIN, BEH }).size(),   /* UTF-8 offset of the emoji. */
        2,                                  /* Two Arabic units precede it in UTF-16. */
        2                                   /* Two Arabic code points in UTF-32. */
    };
    SBUInteger trial;

    for (trial = 0; trial < 3; trial++) {
        SBAlgorithmRef algorithm = SBAlgorithmCreate(sequences[trial]);
        SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, totalLength[trial],
            SBLevelDefaultLTR);
        SBLineRef line = SBParagraphCreateLine(paragraph, 0, totalLength[trial]);
        SBCaretMapRef caretMap = SBCaretMapCreate(line);

        /* Four code points, hence four cells regardless of encoding. */
        assert(SBCaretMapGetCellCount(caretMap) == 4);

        /* Locate the emoji cell and verify its code unit span. */
        SBUInteger visualIndex = SBCaretMapGetVisualIndex(caretMap, emojiUnitOffset[trial]);
        assert(visualIndex != (SBUInteger)(-1));
        assert(SBCaretMapGetSourceOffset(caretMap, visualIndex) == emojiUnitOffset[trial]);
        assert(SBCaretMapGetSourceLength(caretMap, visualIndex) == emojiLength[trial]);

        /*
         * Continuation units must resolve to the same cell, proving that a click can never place
         * the caret inside the code point.
         */
        SBUInteger continuation;
        for (continuation = 1; continuation < emojiLength[trial]; continuation++) {
            assert(SBCaretMapGetVisualIndex(caretMap, emojiUnitOffset[trial] + continuation)
                   == visualIndex);
        }

        SBCaretMapRelease(caretMap);
        SBLineRelease(line);
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
    }

    /*
     * Combining mark: base + hamza above are two separate code points/cells but selection of both
     * stays inside one fragment and the mark never gets detached from its base.
     */
    {
        vector<SBUInt32> combined = { AIN, HAMZA, BEH };
        Fixture fixture(combined);
        SBCaretMapRef caretMap = fixture.caretMap;
        const SBSelectionFragment *fragments = NULL;

        assert(SBCaretMapGetCellCount(caretMap) == 3);
        assert(SBCaretMapGetSelectionFragments(caretMap, 0, 2, &fragments) == 1);
        assert(fragments[0].offset == 0 && fragments[0].length == 2);
    }
}

void CaretMapTests::testEdgeCases()
{
    /*
     * Explicit bidirectional formatting controls carry BN type. They remain addressable cells in
     * the source space, but an override reverses the visual ordering of the cells it encloses.
     */
    {
        vector<SBUInt32> points = { CAP_A, RLO, CAP_B, PDF, CAP_C };
        Fixture fixture(points);
        SBCaretMapRef caretMap = fixture.caretMap;

        /* Every source code unit, including the two controls, owns a cell. */
        assert(SBCaretMapGetCellCount(caretMap) == 5);

        /* The override flips B so that it is rendered before the PDF that closes it. */
        vector<SBUInteger> offsets = VisualOffsets(caretMap);
        assert(offsets == (vector<SBUInteger>{ 0, 1, 3, 2, 4 }));

        /* Each source offset still resolves to exactly one cell: no offset is ever lost. */
        SBUInteger offset;
        for (offset = 0; offset < points.size(); offset++) {
            assert(SBCaretMapGetVisualIndex(caretMap, offset) != (SBUInteger)(-1));
        }

        /* Selecting a formatting control alone produces an ordinary one-unit fragment. */
        const SBSelectionFragment *fragments = NULL;
        assert(SBCaretMapGetSelectionFragments(caretMap, 1, 1, &fragments) == 1);
        assert(fragments[0].offset == 1 && fragments[0].length == 1);

        /* Every logical offset maps to a boundary inside the visual range. */
        for (offset = 0; offset <= points.size(); offset++) {
            assert(SBCaretMapGetLeadingVisualPosition(caretMap, offset) <= 5);
            assert(SBCaretMapGetTrailingVisualPosition(caretMap, offset) <= 5);
        }
    }

    /*
     * Paragraph separator: boundary detection reports the separator length so the caller can split
     * the paragraph precisely. The separator remains an addressable cell of the paragraph it
     * terminates, while the following code point starts a fresh paragraph and never appears in this
     * line's map.
     */
    {
        vector<SBUInt32> points = { CAP_A, 0x000A, CAP_B };
        SBCodepointSequence sequence;
        sequence.stringEncoding = SBStringEncodingUTF32;
        sequence.stringBuffer = points.data();
        sequence.stringLength = points.size();

        SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
        SBUInteger actualLength = 0;
        SBUInteger separatorLength = 0;

        SBAlgorithmGetParagraphBoundary(algorithm, 0, points.size(),
            &actualLength, &separatorLength);
        assert(actualLength == 2);
        assert(separatorLength == 1);

        SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, actualLength,
            SBLevelDefaultLTR);
        SBLineRef line = SBParagraphCreateLine(paragraph, 0, actualLength);
        SBCaretMapRef caretMap = SBCaretMapCreate(line);

        /* The line covers A and the separator; B belongs to the next paragraph and is absent. */
        assert(SBCaretMapGetCellCount(caretMap) == 2);
        assert(SBCaretMapGetSourceOffset(caretMap, 0) == 0);
        assert(SBCaretMapGetSourceOffset(caretMap, 1) == 1);
        assert(SBCaretMapGetVisualIndex(caretMap, 2) == (SBUInteger)(-1));

        /* A second paragraph beginning at B maps independently. */
        {
            SBParagraphRef nextParagraph = SBAlgorithmCreateParagraph(algorithm, 2, 1,
                SBLevelDefaultLTR);
            SBLineRef nextLine = SBParagraphCreateLine(nextParagraph, 2, 1);
            SBCaretMapRef nextMap = SBCaretMapCreate(nextLine);

            assert(SBCaretMapGetCellCount(nextMap) == 1);
            assert(SBCaretMapGetSourceOffset(nextMap, 0) == 2);

            SBCaretMapRelease(nextMap);
            SBLineRelease(nextLine);
            SBParagraphRelease(nextParagraph);
        }

        SBCaretMapRelease(caretMap);
        SBLineRelease(line);
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
    }

    /*
     * Lone UTF-16 surrogate: decoded as a faulty replacement code point but kept as a real cell.
     */
    {
        vector<SBUInt16> units = { 0x0041, 0xD800, 0x0042 };
        SBCodepointSequence sequence;
        sequence.stringEncoding = SBStringEncodingUTF16;
        sequence.stringBuffer = units.data();
        sequence.stringLength = units.size();

        SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
        SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, units.size(),
            SBLevelDefaultLTR);
        SBLineRef line = SBParagraphCreateLine(paragraph, 0, units.size());
        SBCaretMapRef caretMap = SBCaretMapCreate(line);

        assert(SBCaretMapGetCellCount(caretMap) == 3);
        assert(SBCaretMapGetSourceOffset(caretMap, 1) == 1);
        assert(SBCaretMapGetSourceLength(caretMap, 1) == 1);
        assert(SBCaretMapGetVisualIndex(caretMap, 1) == 1);

        SBCaretMapRelease(caretMap);
        SBLineRelease(line);
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
    }

    /*
     * Malformed UTF-8: each faulty byte becomes its own replacement cell, never crashing and never
     * merging with a neighbour.
     */
    {
        vector<SBUInt8> units = { 0x41, 0xC0, 0x80, 0x42 }; /* A, overlong NUL (2 faulty), B. */
        SBCodepointSequence sequence;
        sequence.stringEncoding = SBStringEncodingUTF8;
        sequence.stringBuffer = units.data();
        sequence.stringLength = units.size();

        SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
        SBParagraphRef paragraph = SBAlgorithmCreateParagraph(algorithm, 0, units.size(),
            SBLevelDefaultLTR);
        SBLineRef line = SBParagraphCreateLine(paragraph, 0, units.size());
        SBCaretMapRef caretMap = SBCaretMapCreate(line);

        assert(SBCaretMapGetCellCount(caretMap) == 4);
        assert(SBCaretMapGetSourceOffset(caretMap, 0) == 0);
        assert(SBCaretMapGetSourceOffset(caretMap, 1) == 1);
        assert(SBCaretMapGetSourceOffset(caretMap, 2) == 2);
        assert(SBCaretMapGetSourceOffset(caretMap, 3) == 3);

        SBCaretMapRelease(caretMap);
        SBLineRelease(line);
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(algorithm);
    }

    /*
     * Smallest supported line (single cell) and an all-whitespace paragraph: the library never
     * creates a zero-length paragraph, but every whitespace code unit still maps to a real cell.
     */
    {
        vector<SBUInt32> points = { CAP_A };
        Fixture fixture(points);
        SBCaretMapRef caretMap = fixture.caretMap;

        assert(SBCaretMapGetCellCount(caretMap) == 1);
        assert(SBCaretMapGetLeadingOffset(caretMap, 0) == 0);
        assert(SBCaretMapGetTrailingOffset(caretMap, 0) == 0);
        assert(SBCaretMapGetLeadingOffset(caretMap, 1) == 1);
        assert(SBCaretMapGetTrailingOffset(caretMap, 1) == 1);

        /* Out-of-range source offsets are rejected. */
        assert(SBCaretMapGetVisualIndex(caretMap, 5) == (SBUInteger)(-1));
    }

    {
        vector<SBUInt32> points = { SPACE, 0x0009, SPACE };
        Fixture fixture(points);
        SBCaretMapRef caretMap = fixture.caretMap;

        assert(SBCaretMapGetCellCount(caretMap) == 3);
        /* Source-to-visual is the identity for an all-LTR whitespace line. */
        assert(SBCaretMapGetVisualIndex(caretMap, 0) == 0);
        assert(SBCaretMapGetVisualIndex(caretMap, 2) == 2);
        assert(SBCaretMapGetLeadingVisualPosition(caretMap, 3) == 3);
    }

    /* NULL map must fail gracefully instead of crashing. */
    {
        assert(SBCaretMapCreate(NULL) == NULL);
        assert(SBCaretMapGetCellCount(NULL) == 0);
        assert(SBCaretMapGetSourceOffset(NULL, 0) == (SBUInteger)(-1));
        assert(SBCaretMapGetSourceLength(NULL, 0) == 0);
        assert(SBCaretMapGetLeadingOffset(NULL, 0) == (SBUInteger)(-1));
        assert(SBCaretMapGetTrailingOffset(NULL, 0) == (SBUInteger)(-1));
        assert(SBCaretMapGetVisualIndex(NULL, 0) == (SBUInteger)(-1));
        assert(SBCaretMapGetLeadingVisualPosition(NULL, 0) == 0);
        assert(SBCaretMapGetTrailingVisualPosition(NULL, 0) == 0);
        assert(SBCaretMapGetSelectionFragments(NULL, 0, 1, NULL) == 0);
    }
}

void CaretMapTests::run()
{
    testVisualOrder();
    testCaretPositions();
    testSelectionFragments();
    testEncodings();
    testEdgeCases();
}

#ifdef STANDALONE_TESTING

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    CaretMapTests caretMapTests;
    caretMapTests.run();

    return 0;
}

#endif
