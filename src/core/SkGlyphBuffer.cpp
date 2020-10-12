/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkGlyphBuffer.h"
#include "src/core/SkGlyphRunPainter.h"
#include "src/core/SkStrikeForGPU.h"

void SkSourceGlyphBuffer::reset() {
    fRejectedGlyphIDs.reset();
    fRejectedPositions.reset();
}

void SkDrawableGlyphBuffer::ensureSize(size_t size) {
    if (size > fMaxSize) {
        fMultiBuffer.reset(size);
        fPositions.reset(size);
        fMaxSize = size;
    }

    fInputSize = 0;
    fDrawableSize = 0;
}

void SkDrawableGlyphBuffer::startSource(const SkZip<const SkGlyphID, const SkPoint>& source) {
    fInputSize = source.size();
    fDrawableSize = 0;

    auto positions = source.get<1>();
    memcpy(fPositions, positions.data(), positions.size() * sizeof(SkPoint));

    // Convert from SkGlyphIDs to SkPackedGlyphIDs.
    SkGlyphVariant* packedIDCursor = fMultiBuffer;
    for (auto t : source) {
        *packedIDCursor++ = SkPackedGlyphID{std::get<0>(t)};
    }
    SkDEBUGCODE(fPhase = kInput);
}

void SkDrawableGlyphBuffer::startBitmapDevice(
        const SkZip<const SkGlyphID, const SkPoint>& source,
        SkPoint origin, const SkMatrix& viewMatrix,
        const SkGlyphPositionRoundingSpec& roundingSpec) {
    fInputSize = source.size();
    fDrawableSize = 0;

    // Map the positions including subpixel position.
    auto positions = source.get<1>();
    SkMatrix matrix = viewMatrix;
    matrix.preTranslate(origin.x(), origin.y());
    SkPoint halfSampleFreq = roundingSpec.halfAxisSampleFreq;
    matrix.postTranslate(halfSampleFreq.x(), halfSampleFreq.y());
    matrix.mapPoints(fPositions, positions.data(), positions.size());

    // Mask for controlling axis alignment.
    SkIPoint mask = roundingSpec.ignorePositionFieldMask;

    // Convert glyph ids and positions to packed glyph ids.
    SkZip<const SkGlyphID, const SkPoint> withMappedPos =
            SkMakeZip(source.get<0>(), fPositions.get());
    SkGlyphVariant* packedIDCursor = fMultiBuffer;
    for (auto [glyphID, pos] : withMappedPos) {
        *packedIDCursor++ = SkPackedGlyphID{glyphID, pos, mask};
    }
    SkDEBUGCODE(fPhase = kInput);
}

void SkDrawableGlyphBuffer::startGPUDevice(
        const SkZip<const SkGlyphID, const SkPoint>& source,
        SkPoint origin, const SkMatrix& viewMatrix,
        const SkGlyphPositionRoundingSpec& roundingSpec) {
    fInputSize = source.size();
    fDrawableSize = 0;

    // Map the positions including subpixel position.
    auto positions = source.get<1>();
    SkMatrix matrix = viewMatrix;
    matrix.preTranslate(origin.x(), origin.y());

    // Q = [M][T](0,0).
    SkPoint Q = matrix.mapXY(0, 0);
    SkPoint halfSampleFreq = roundingSpec.halfAxisSampleFreq;
    matrix.postTranslate(halfSampleFreq.x(), halfSampleFreq.y());
    matrix.mapPoints(fPositions, positions.data(), positions.size());

    // Mask for controlling axis alignment.
    SkIPoint mask = roundingSpec.ignorePositionFieldMask;

    // Convert glyph ids and positions to packed glyph ids.
    SkZip<const SkGlyphID, const SkPoint> withMappedPos =
            SkMakeZip(source.get<0>(), fPositions.get());
    SkGlyphVariant* packedIDCursor = fMultiBuffer;
    for (auto [glyphID, pos] : withMappedPos) {
        *packedIDCursor++ = SkPackedGlyphID{glyphID, pos, mask};
    }

    for (SkPoint& pos : SkSpan<SkPoint>(fPositions, source.size())) {
        SkPoint P = SkPoint::Make(SkScalarFloorToScalar(pos.x()), SkScalarFloorToScalar(pos.y()));
        pos = P - Q;
    }

    SkDEBUGCODE(fPhase = kInput);
}


void SkDrawableGlyphBuffer::reset() {
    SkDEBUGCODE(fPhase = kReset);
    if (fMaxSize > 200) {
        fMultiBuffer.reset();
        fPositions.reset();
        fMaxSize = 0;
    }
    fInputSize = 0;
    fDrawableSize = 0;
}

