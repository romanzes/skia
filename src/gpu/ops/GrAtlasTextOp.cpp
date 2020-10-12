/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/ops/GrAtlasTextOp.h"

#include "include/core/SkPoint3.h"
#include "include/private/GrRecordingContext.h"
#include "src/core/SkMathPriv.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkStrikeCache.h"
#include "src/gpu/GrCaps.h"
#include "src/gpu/GrMemoryPool.h"
#include "src/gpu/GrOpFlushState.h"
#include "src/gpu/GrRecordingContextPriv.h"
#include "src/gpu/GrRenderTargetContext.h"
#include "src/gpu/GrResourceProvider.h"
#include "src/gpu/effects/GrBitmapTextGeoProc.h"
#include "src/gpu/effects/GrDistanceFieldGeoProc.h"
#include "src/gpu/ops/GrSimpleMeshDrawOpHelper.h"
#include "src/gpu/text/GrAtlasManager.h"
#include "src/gpu/text/GrDistanceFieldAdjustTable.h"

#if GR_TEST_UTILS
#include "src/gpu/GrDrawOpTest.h"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

GrAtlasTextOp::GrAtlasTextOp(MaskType maskType,
                             GrPaint&& paint,
                             GrTextBlob::SubRun* subrun,
                             const SkMatrix& drawMatrix,
                             SkPoint drawOrigin,
                             const SkIRect& clipRect,
                             const SkPMColor4f& filteredColor,
                             SkColor luminanceColor,
                             bool useGammaCorrectDistanceTable,
                             uint32_t DFGPFlags)
        : INHERITED(ClassID())
        , fMaskType{maskType}
        , fNeedsGlyphTransform{subrun->needsTransform()}
        , fLuminanceColor{luminanceColor}
        , fUseGammaCorrectDistanceTable{useGammaCorrectDistanceTable}
        , fDFGPFlags{DFGPFlags}
        , fGeoDataAllocSize{kMinGeometryAllocated}
        , fProcessors{std::move(paint)}
        , fNumGlyphs{subrun->glyphCount()} {
    GrAtlasTextOp::Geometry& geometry = fGeoData[0];

    // Unref handled in ~GrAtlasTextOp().
    geometry.fBlob = SkRef(subrun->fBlob);
    geometry.fSubRunPtr = subrun;
    geometry.fDrawMatrix = drawMatrix;
    geometry.fDrawOrigin = drawOrigin;
    geometry.fClipRect = clipRect;
    geometry.fColor = subrun->maskFormat() == kARGB_GrMaskFormat ? SK_PMColor4fWHITE
                                                                 : filteredColor;
    fGeoCount = 1;

    SkRect bounds = subrun->deviceRect(drawMatrix, drawOrigin);
    // We don't have tight bounds on the glyph paths in device space. For the purposes of bounds
    // we treat this as a set of non-AA rects rendered with a texture.
    this->setBounds(bounds, HasAABloat::kNo, IsHairline::kNo);
}

void GrAtlasTextOp::Geometry::fillVertexData(void *dst, int offset, int count) const {
    fSubRunPtr->fillVertexData(dst, offset, count, fColor.toBytes_RGBA(),
                               fDrawMatrix, fDrawOrigin, fClipRect);
}

std::unique_ptr<GrAtlasTextOp> GrAtlasTextOp::MakeBitmap(GrRecordingContext* context,
                                                         GrPaint&& paint,
                                                         GrTextBlob::SubRun* subrun,
                                                         const SkMatrix& drawMatrix,
                                                         SkPoint drawOrigin,
                                                         const SkIRect& clipRect,
                                                         const SkPMColor4f& filteredColor) {
    GrOpMemoryPool* pool = context->priv().opMemoryPool();

    MaskType maskType = [&]() {
        switch (subrun->maskFormat()) {
            case kA8_GrMaskFormat: return kGrayscaleCoverageMask_MaskType;
            case kA565_GrMaskFormat: return kLCDCoverageMask_MaskType;
            case kARGB_GrMaskFormat: return kColorBitmapMask_MaskType;
            // Needed to placate some compilers.
            default: return kGrayscaleCoverageMask_MaskType;
        }
    }();

    return pool->allocate<GrAtlasTextOp>(maskType,
                                         std::move(paint),
                                         subrun,
                                         drawMatrix,
                                         drawOrigin,
                                         clipRect,
                                         filteredColor,
                                         0,
                                         false,
                                         0);
}

std::unique_ptr<GrAtlasTextOp> GrAtlasTextOp::MakeDistanceField(
                                            GrRecordingContext* context,
                                            GrPaint&& paint,
                                            GrTextBlob::SubRun* subrun,
                                            const SkMatrix& drawMatrix,
                                            SkPoint drawOrigin,
                                            const SkIRect& clipRect,
                                            const SkPMColor4f& filteredColor,
                                            bool useGammaCorrectDistanceTable,
                                            SkColor luminanceColor,
                                            const SkSurfaceProps& props) {
    GrOpMemoryPool* pool = context->priv().opMemoryPool();
    bool isBGR = SkPixelGeometryIsBGR(props.pixelGeometry());
    bool isLCD = subrun->hasUseLCDText() && SkPixelGeometryIsH(props.pixelGeometry());
    MaskType maskType = !subrun->isAntiAliased() ? kAliasedDistanceField_MaskType
                                                 : isLCD ? (isBGR ? kLCDBGRDistanceField_MaskType
                                                                  : kLCDDistanceField_MaskType)
                                                         : kGrayscaleDistanceField_MaskType;

    uint32_t DFGPFlags = drawMatrix.isSimilarity() ? kSimilarity_DistanceFieldEffectFlag : 0;
    DFGPFlags |= drawMatrix.isScaleTranslate() ? kScaleOnly_DistanceFieldEffectFlag : 0;
    DFGPFlags |= drawMatrix.hasPerspective() ? kPerspective_DistanceFieldEffectFlag : 0;
    DFGPFlags |= useGammaCorrectDistanceTable ? kGammaCorrect_DistanceFieldEffectFlag : 0;
    DFGPFlags |= kAliasedDistanceField_MaskType == maskType ? kAliased_DistanceFieldEffectFlag : 0;

    if (isLCD) {
        DFGPFlags |= kUseLCD_DistanceFieldEffectFlag;
        DFGPFlags |= kLCDBGRDistanceField_MaskType == maskType ? kBGR_DistanceFieldEffectFlag : 0;
    }

    return pool->allocate<GrAtlasTextOp>(maskType,
                                         std::move(paint),
                                         subrun,
                                         drawMatrix,
                                         drawOrigin,
                                         clipRect,
                                         filteredColor,
                                         luminanceColor,
                                         useGammaCorrectDistanceTable,
                                         DFGPFlags);
}

void GrAtlasTextOp::visitProxies(const VisitProxyFunc& func) const {
    fProcessors.visitProxies(func);
}

#ifdef SK_DEBUG
SkString GrAtlasTextOp::dumpInfo() const {
    SkString str;

    for (int i = 0; i < fGeoCount; ++i) {
        str.appendf("%d: Color: 0x%08x Trans: %.2f,%.2f\n",
                    i,
                    fGeoData[i].fColor.toBytes_RGBA(),
                    fGeoData[i].fDrawOrigin.x(),
                    fGeoData[i].fDrawOrigin.y());
    }

    str += fProcessors.dumpProcessors();
    str += INHERITED::dumpInfo();
    return str;
}
#endif

GrDrawOp::FixedFunctionFlags GrAtlasTextOp::fixedFunctionFlags() const {
    return FixedFunctionFlags::kNone;
}

GrProcessorSet::Analysis GrAtlasTextOp::finalize(
        const GrCaps& caps, const GrAppliedClip* clip, bool hasMixedSampledCoverage,
        GrClampType clampType) {
    GrProcessorAnalysisCoverage coverage;
    GrProcessorAnalysisColor color;
    if (kColorBitmapMask_MaskType == fMaskType) {
        color.setToUnknown();
    } else {
        color.setToConstant(this->color());
    }
    switch (fMaskType) {
        case kGrayscaleCoverageMask_MaskType:
        case kAliasedDistanceField_MaskType:
        case kGrayscaleDistanceField_MaskType:
            coverage = GrProcessorAnalysisCoverage::kSingleChannel;
            break;
        case kLCDCoverageMask_MaskType:
        case kLCDDistanceField_MaskType:
        case kLCDBGRDistanceField_MaskType:
            coverage = GrProcessorAnalysisCoverage::kLCD;
            break;
        case kColorBitmapMask_MaskType:
            coverage = GrProcessorAnalysisCoverage::kNone;
            break;
    }
    auto analysis = fProcessors.finalize(
            color, coverage, clip, &GrUserStencilSettings::kUnused, hasMixedSampledCoverage, caps,
            clampType, &fGeoData[0].fColor);
    fUsesLocalCoords = analysis.usesLocalCoords();
    return analysis;
}

void GrAtlasTextOp::onPrepareDraws(Target* target) {
    auto resourceProvider = target->resourceProvider();

    // if we have RGB, then we won't have any SkShaders so no need to use a localmatrix.
    // TODO actually only invert if we don't have RGBA
    SkMatrix localMatrix;
    if (this->usesLocalCoords() && !fGeoData[0].fDrawMatrix.invert(&localMatrix)) {
        return;
    }

    GrAtlasManager* atlasManager = target->atlasManager();

    GrMaskFormat maskFormat = this->maskFormat();

    unsigned int numActiveViews;
    const GrSurfaceProxyView* views = atlasManager->getViews(maskFormat, &numActiveViews);
    if (!views) {
        SkDebugf("Could not allocate backing texture for atlas\n");
        return;
    }
    SkASSERT(views[0].proxy());

    static constexpr int kMaxTextures = GrBitmapTextGeoProc::kMaxTextures;
    static_assert(GrDistanceFieldA8TextGeoProc::kMaxTextures == kMaxTextures);
    static_assert(GrDistanceFieldLCDTextGeoProc::kMaxTextures == kMaxTextures);

    auto primProcProxies = target->allocPrimProcProxyPtrs(kMaxTextures);
    for (unsigned i = 0; i < numActiveViews; ++i) {
        primProcProxies[i] = views[i].proxy();
        // This op does not know its atlas proxies when it is added to a GrOpsTasks, so the proxies
        // don't get added during the visitProxies call. Thus we add them here.
        target->sampledProxyArray()->push_back(views[i].proxy());
    }

    FlushInfo flushInfo;
    flushInfo.fPrimProcProxies = primProcProxies;

    bool vmPerspective = fGeoData[0].fDrawMatrix.hasPerspective();
    if (this->usesDistanceFields()) {
        flushInfo.fGeometryProcessor = this->setupDfProcessor(target->allocator(),
                                                              *target->caps().shaderCaps(),
                                                              views, numActiveViews);
    } else {
        auto filter = fNeedsGlyphTransform ? GrSamplerState::Filter::kBilerp
                                           : GrSamplerState::Filter::kNearest;
        flushInfo.fGeometryProcessor = GrBitmapTextGeoProc::Make(
                target->allocator(), *target->caps().shaderCaps(), this->color(), false, views,
                numActiveViews, filter, maskFormat, localMatrix, vmPerspective);
    }

    int vertexStride = (int)flushInfo.fGeometryProcessor->vertexStride();

    // Ensure we don't request an insanely large contiguous vertex allocation.
    static const int kMaxVertexBytes = GrBufferAllocPool::kDefaultBufferSize;
    const int quadSize = vertexStride * kVerticesPerGlyph;
    const int maxQuadsPerBuffer = kMaxVertexBytes / quadSize;

    // Where the quad buffer begins and ends relative to totalGlyphsRegened.
    int quadBufferBegin = 0;
    int quadBufferEnd = std::min(this->numGlyphs(), maxQuadsPerBuffer);

    flushInfo.fIndexBuffer = resourceProvider->refNonAAQuadIndexBuffer();
    void* vertices = target->makeVertexSpace(
            vertexStride,
            kVerticesPerGlyph * (quadBufferEnd - quadBufferBegin),
            &flushInfo.fVertexBuffer,
            &flushInfo.fVertexOffset);
    if (!vertices || !flushInfo.fVertexBuffer) {
        SkDebugf("Could not allocate vertices\n");
        return;
    }

    // totalGlyphsRegened is all the glyphs for the op [0, this->numGlyphs()). The subRun glyph and
    // quad buffer indices are calculated from this.
    int totalGlyphsRegened = 0;
    for (int i = 0; i < fGeoCount; i++) {
        const Geometry& args = fGeoData[i];
        auto subRun = args.fSubRunPtr;
        SkASSERT((int)subRun->vertexStride() == vertexStride);

        subRun->prepareGrGlyphs(target->strikeCache());

        // TODO4F: Preserve float colors
        GrTextBlob::VertexRegenerator regenerator(resourceProvider, subRun,
                                                  target->deferredUploadTarget(), atlasManager);

        // Where the subRun begins and ends relative to totalGlyphsRegened.
        int subRunBegin = totalGlyphsRegened;
        int subRunEnd = subRunBegin + subRun->glyphCount();

        // Draw all the glyphs in the subRun.
        while (totalGlyphsRegened < subRunEnd) {
            // drawBegin and drawEnd are indices for the subRun on the
            // interval [0, subRun->fGlyphs.size()).
            int drawBegin = totalGlyphsRegened - subRunBegin;
            // drawEnd is either the end of the subRun or the end of the current quad buffer.
            int drawEnd = std::min(subRunEnd, quadBufferEnd) - subRunBegin;
            auto[ok, glyphsRegenerated] = regenerator.regenerate(drawBegin, drawEnd);

            // There was a problem allocating the glyph in the atlas. Bail.
            if (!ok) {
                return;
            }

            // Update all the vertices for glyphsRegenerate glyphs.
            if (glyphsRegenerated > 0) {
                int quadBufferIndex = totalGlyphsRegened - quadBufferBegin;
                auto regeneratedQuadBuffer =
                        SkTAddOffset<char>(vertices, subRun->quadOffset(quadBufferIndex));
                int subRunIndex = totalGlyphsRegened - subRunBegin;
                args.fillVertexData(regeneratedQuadBuffer, subRunIndex, glyphsRegenerated);
            }

            totalGlyphsRegened += glyphsRegenerated;
            flushInfo.fGlyphsToFlush += glyphsRegenerated;

            // regenerate() has stopped part way through a SubRun. This means that either the atlas
            // or the quad buffer is full or both. There is a case were the flow through
            // the loop is strange. If we run out of quad buffer space at the same time the
            // SubRun ends, then this is not triggered which is the right result for the last
            // SubRun. But, if this is not the last SubRun, then advance to the next SubRun which
            // will process no glyphs, and return to this point where the quad buffer will be
            // expanded.
            if (totalGlyphsRegened != subRunEnd) {
                // Flush if not all glyphs drawn because either the quad buffer is full or the
                // atlas is out of space.
                this->createDrawForGeneratedGlyphs(target, &flushInfo);
                if (totalGlyphsRegened == quadBufferEnd) {
                    // Quad buffer is full. Get more buffer.
                    quadBufferBegin = totalGlyphsRegened;
                    int quadBufferSize =
                            std::min(maxQuadsPerBuffer, this->numGlyphs() - totalGlyphsRegened);
                    quadBufferEnd = quadBufferBegin + quadBufferSize;

                    vertices = target->makeVertexSpace(
                            vertexStride,
                            kVerticesPerGlyph * quadBufferSize,
                            &flushInfo.fVertexBuffer,
                            &flushInfo.fVertexOffset);
                    if (!vertices || !flushInfo.fVertexBuffer) {
                        SkDebugf("Could not allocate vertices\n");
                        return;
                    }
                }
            }
        }
    }  // for all geometries
    this->createDrawForGeneratedGlyphs(target, &flushInfo);
}

void GrAtlasTextOp::onExecute(GrOpFlushState* flushState, const SkRect& chainBounds) {
    auto pipeline = GrSimpleMeshDrawOpHelper::CreatePipeline(flushState,
                                                             std::move(fProcessors),
                                                             GrPipeline::InputFlags::kNone);

    flushState->executeDrawsAndUploadsForMeshDrawOp(this, chainBounds, pipeline);
}

void GrAtlasTextOp::createDrawForGeneratedGlyphs(
        GrMeshDrawOp::Target* target, FlushInfo* flushInfo) const {
    if (!flushInfo->fGlyphsToFlush) {
        return;
    }

    auto atlasManager = target->atlasManager();

    GrGeometryProcessor* gp = flushInfo->fGeometryProcessor;
    GrMaskFormat maskFormat = this->maskFormat();

    unsigned int numActiveViews;
    const GrSurfaceProxyView* views = atlasManager->getViews(maskFormat, &numActiveViews);
    SkASSERT(views);
    // Something has gone terribly wrong, bail
    if (!views || 0 == numActiveViews) {
        return;
    }
    if (gp->numTextureSamplers() != (int) numActiveViews) {
        // During preparation the number of atlas pages has increased.
        // Update the proxies used in the GP to match.
        for (unsigned i = gp->numTextureSamplers(); i < numActiveViews; ++i) {
            flushInfo->fPrimProcProxies[i] = views[i].proxy();
            // This op does not know its atlas proxies when it is added to a GrOpsTasks, so the
            // proxies don't get added during the visitProxies call. Thus we add them here.
            target->sampledProxyArray()->push_back(views[i].proxy());
            // These will get unreffed when the previously recorded draws destruct.
            for (int d = 0; d < flushInfo->fNumDraws; ++d) {
                flushInfo->fPrimProcProxies[i]->ref();
            }
        }
        if (this->usesDistanceFields()) {
            if (this->isLCD()) {
                reinterpret_cast<GrDistanceFieldLCDTextGeoProc*>(gp)->addNewViews(
                        views, numActiveViews, GrSamplerState::Filter::kBilerp);
            } else {
                reinterpret_cast<GrDistanceFieldA8TextGeoProc*>(gp)->addNewViews(
                        views, numActiveViews, GrSamplerState::Filter::kBilerp);
            }
        } else {
            auto filter = fNeedsGlyphTransform ? GrSamplerState::Filter::kBilerp
                                               : GrSamplerState::Filter::kNearest;
            reinterpret_cast<GrBitmapTextGeoProc*>(gp)->addNewViews(views, numActiveViews, filter);
        }
    }
    int maxGlyphsPerDraw = static_cast<int>(flushInfo->fIndexBuffer->size() / sizeof(uint16_t) / 6);
    GrSimpleMesh* mesh = target->allocMesh();
    mesh->setIndexedPatterned(flushInfo->fIndexBuffer, kIndicesPerGlyph, flushInfo->fGlyphsToFlush,
                              maxGlyphsPerDraw, flushInfo->fVertexBuffer, kVerticesPerGlyph,
                              flushInfo->fVertexOffset);
    target->recordDraw(flushInfo->fGeometryProcessor, mesh, 1, flushInfo->fPrimProcProxies,
                       GrPrimitiveType::kTriangles);
    flushInfo->fVertexOffset += kVerticesPerGlyph * flushInfo->fGlyphsToFlush;
    flushInfo->fGlyphsToFlush = 0;
    ++flushInfo->fNumDraws;
}

GrOp::CombineResult GrAtlasTextOp::onCombineIfPossible(GrOp* t, GrRecordingContext::Arenas*,
                                                       const GrCaps& caps) {
    GrAtlasTextOp* that = t->cast<GrAtlasTextOp>();
    if (fProcessors != that->fProcessors) {
        return CombineResult::kCannotCombine;
    }

    if (fMaskType != that->fMaskType) {
        return CombineResult::kCannotCombine;
    }

    const SkMatrix& thisFirstMatrix = fGeoData[0].fDrawMatrix;
    const SkMatrix& thatFirstMatrix = that->fGeoData[0].fDrawMatrix;

    if (this->usesLocalCoords() && !SkMatrixPriv::CheapEqual(thisFirstMatrix, thatFirstMatrix)) {
        return CombineResult::kCannotCombine;
    }

    if (fNeedsGlyphTransform != that->fNeedsGlyphTransform) {
        return CombineResult::kCannotCombine;
    }

    if (fNeedsGlyphTransform &&
        (thisFirstMatrix.hasPerspective() != thatFirstMatrix.hasPerspective())) {
        return CombineResult::kCannotCombine;
    }

    if (this->usesDistanceFields()) {
        if (fDFGPFlags != that->fDFGPFlags) {
            return CombineResult::kCannotCombine;
        }

        if (fLuminanceColor != that->fLuminanceColor) {
            return CombineResult::kCannotCombine;
        }
    } else {
        if (kColorBitmapMask_MaskType == fMaskType && this->color() != that->color()) {
            return CombineResult::kCannotCombine;
        }
    }

    fNumGlyphs += that->numGlyphs();

    // Reallocate space for geo data if necessary and then import that geo's data.
    int newGeoCount = that->fGeoCount + fGeoCount;

    // We reallocate at a rate of 1.5x to try to get better total memory usage
    if (newGeoCount > fGeoDataAllocSize) {
        int newAllocSize = fGeoDataAllocSize + fGeoDataAllocSize / 2;
        while (newAllocSize < newGeoCount) {
            newAllocSize += newAllocSize / 2;
        }
        fGeoData.realloc(newAllocSize);
        fGeoDataAllocSize = newAllocSize;
    }

    // We steal the ref on the blobs from the other AtlasTextOp and set its count to 0 so that
    // it doesn't try to unref them.
    memcpy(&fGeoData[fGeoCount], that->fGeoData.get(), that->fGeoCount * sizeof(Geometry));
#ifdef SK_DEBUG
    for (int i = 0; i < that->fGeoCount; ++i) {
        that->fGeoData.get()[i].fBlob = (GrTextBlob*)0x1;
    }
#endif
    that->fGeoCount = 0;
    fGeoCount = newGeoCount;

    return CombineResult::kMerged;
}

static const int kDistanceAdjustLumShift = 5;

// TODO trying to figure out why lcd is so whack
GrGeometryProcessor* GrAtlasTextOp::setupDfProcessor(SkArenaAlloc* arena,
                                                     const GrShaderCaps& caps,
                                                     const GrSurfaceProxyView* views,
                                                     unsigned int numActiveViews) const {
    bool isLCD = this->isLCD();

    SkMatrix localMatrix = SkMatrix::I();
    if (this->usesLocalCoords()) {
        // If this fails we'll just use I().
        bool result = fGeoData[0].fDrawMatrix.invert(&localMatrix);
        (void)result;
    }

    auto dfAdjustTable = GrDistanceFieldAdjustTable::Get();

    // see if we need to create a new effect
    if (isLCD) {
        float redCorrection = dfAdjustTable->getAdjustment(
                SkColorGetR(fLuminanceColor) >> kDistanceAdjustLumShift,
                fUseGammaCorrectDistanceTable);
        float greenCorrection = dfAdjustTable->getAdjustment(
                SkColorGetG(fLuminanceColor) >> kDistanceAdjustLumShift,
                fUseGammaCorrectDistanceTable);
        float blueCorrection = dfAdjustTable->getAdjustment(
                SkColorGetB(fLuminanceColor) >> kDistanceAdjustLumShift,
                fUseGammaCorrectDistanceTable);
        GrDistanceFieldLCDTextGeoProc::DistanceAdjust widthAdjust =
                GrDistanceFieldLCDTextGeoProc::DistanceAdjust::Make(
                        redCorrection, greenCorrection, blueCorrection);
        return GrDistanceFieldLCDTextGeoProc::Make(arena, caps, views, numActiveViews,
                                                   GrSamplerState::Filter::kBilerp, widthAdjust,
                                                   fDFGPFlags, localMatrix);
    } else {
#ifdef SK_GAMMA_APPLY_TO_A8
        float correction = 0;
        if (kAliasedDistanceField_MaskType != fMaskType) {
            U8CPU lum = SkColorSpaceLuminance::computeLuminance(SK_GAMMA_EXPONENT,
                                                                fLuminanceColor);
            correction = dfAdjustTable->getAdjustment(lum >> kDistanceAdjustLumShift,
                                                      fUseGammaCorrectDistanceTable);
        }
        return GrDistanceFieldA8TextGeoProc::Make(arena, caps, views, numActiveViews,
                                                  GrSamplerState::Filter::kBilerp, correction,
                                                  fDFGPFlags, localMatrix);
#else
        return GrDistanceFieldA8TextGeoProc::Make(arena, caps, views, numActiveViews,
                                                  GrSamplerState::Filter::kBilerp,
                                                  fDFGPFlags, localMatrix);
#endif
    }
}

#if GR_TEST_UTILS
std::unique_ptr<GrDrawOp> GrAtlasTextOp::CreateOpTestingOnly(GrRenderTargetContext* rtc,
                                                             const SkPaint& skPaint,
                                                             const SkFont& font,
                                                             const SkMatrixProvider& mtxProvider,
                                                             const char* text,
                                                             int x,
                                                             int y) {
    static SkSurfaceProps surfaceProps(SkSurfaceProps::kLegacyFontHost_InitType);

    size_t textLen = (int)strlen(text);

    const SkMatrix& drawMatrix(mtxProvider.localToDevice());

    auto drawOrigin = SkPoint::Make(x, y);
    SkGlyphRunBuilder builder;
    builder.drawTextUTF8(skPaint, font, text, textLen, drawOrigin);

    auto glyphRunList = builder.useGlyphRunList();

    const GrRecordingContextPriv& contextPriv = rtc->fContext->priv();
    GrSDFTOptions SDFOptions = rtc->fContext->priv().SDFTOptions();

    if (glyphRunList.empty()) {
        return nullptr;
    }
    sk_sp<GrTextBlob> blob = GrTextBlob::Make(glyphRunList, drawMatrix);
    SkGlyphRunListPainter* painter = &rtc->fGlyphPainter;
    painter->processGlyphRunList(
            glyphRunList, drawMatrix, surfaceProps,
            contextPriv.caps()->shaderCaps()->supportsDistanceFieldText(),
            SDFOptions, blob.get());

    return blob->firstSubRun()->makeOp(mtxProvider,
                                       drawOrigin,
                                       SkIRect::MakeEmpty(),
                                       skPaint,
                                       surfaceProps,
                                       rtc->textTarget());
}

GR_DRAW_OP_TEST_DEFINE(GrAtlasTextOp) {
    // Setup dummy SkPaint / GrPaint / GrRenderTargetContext
    auto rtc = GrRenderTargetContext::Make(
            context, GrColorType::kRGBA_8888, nullptr, SkBackingFit::kApprox, {1024, 1024});

    SkSimpleMatrixProvider matrixProvider(GrTest::TestMatrixInvertible(random));

    SkPaint skPaint;
    skPaint.setColor(random->nextU());

    SkFont font;
    if (random->nextBool()) {
        font.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    } else {
        font.setEdging(random->nextBool() ? SkFont::Edging::kAntiAlias : SkFont::Edging::kAlias);
    }
    font.setSubpixel(random->nextBool());

    const char* text = "The quick brown fox jumps over the lazy dog.";

    // create some random x/y offsets, including negative offsets
    static const int kMaxTrans = 1024;
    int xPos = (random->nextU() % 2) * 2 - 1;
    int yPos = (random->nextU() % 2) * 2 - 1;
    int xInt = (random->nextU() % kMaxTrans) * xPos;
    int yInt = (random->nextU() % kMaxTrans) * yPos;

    return GrAtlasTextOp::CreateOpTestingOnly(
            rtc.get(), skPaint, font, matrixProvider, text, xInt, yInt);
}

#endif


