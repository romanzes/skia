/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColorFilter.h"
#include "include/effects/SkImageFilters.h"
#include "modules/svg/include/SkSVGFe.h"
#include "modules/svg/include/SkSVGFilter.h"
#include "modules/svg/include/SkSVGFilterContext.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGValue.h"

bool SkSVGFilter::parseAndSetAttribute(const char* name, const char* value) {
    return INHERITED::parseAndSetAttribute(name, value) ||
           this->setX(SkSVGAttributeParser::parse<SkSVGLength>("x", name, value)) ||
           this->setY(SkSVGAttributeParser::parse<SkSVGLength>("y", name, value)) ||
           this->setWidth(SkSVGAttributeParser::parse<SkSVGLength>("width", name, value)) ||
           this->setHeight(SkSVGAttributeParser::parse<SkSVGLength>("height", name, value)) ||
           this->setFilterUnits(SkSVGAttributeParser::parse<SkSVGObjectBoundingBoxUnits>(
                   "filterUnits", name, value)) ||
           this->setPrimitiveUnits(SkSVGAttributeParser::parse<SkSVGObjectBoundingBoxUnits>(
                   "primitiveUnits", name, value));
}

void SkSVGFilter::applyProperties(SkSVGRenderContext* ctx) const {
    SkDebugf("SkSVGFilter::applyProperties\n");
    this->onPrepareToRender(ctx);
}

sk_sp<SkImageFilter> SkSVGFilter::buildFilterDAG(const SkSVGRenderContext& ctx) const {
    sk_sp<SkImageFilter> filter;
    SkSVGFilterContext fctx(ctx.resolveOBBRect(fX, fY, fWidth, fHeight, fFilterUnits),
                            fPrimitiveUnits);
    SkSVGColorspace cs = SkSVGColorspace::kSRGB;
    for (const auto& child : fChildren) {
        if (!SkSVGFe::IsFilterEffect(child)) {
            continue;
        }

        const auto& feNode = static_cast<const SkSVGFe&>(*child);
        SkSVGColorspace cs1 = feNode.resolveColorspace(ctx, fctx);
        if (cs1 == SkSVGColorspace::kAuto) {
            SkDebugf("SkSVGFilter::buildFilterDAG: before: kAuto\n");
        } else if (cs1 == SkSVGColorspace::kSRGB) {
            SkDebugf("SkSVGFilter::buildFilterDAG: before: kSRGB\n");
        } else if (cs1 == SkSVGColorspace::kLinearRGB) {
            SkDebugf("SkSVGFilter::buildFilterDAG: before: kLinearRGB\n");
        }

        const auto& feResultType = feNode.getResult();

        // Propagate any inherited properties that may impact filter effect behavior (e.g.
        // color-interpolation-filters). We call this explicitly here because the SkSVGFe
        // nodes do not participate in the normal onRender path, which is when property
        // propagation currently occurs.
        SkSVGRenderContext localCtx(ctx);
        feNode.applyProperties(&localCtx);

        const SkRect filterSubregion = feNode.resolveFilterSubregion(localCtx, fctx);
        cs = feNode.resolveColorspace(localCtx, fctx);
        if (cs == SkSVGColorspace::kAuto) {
            SkDebugf("SkSVGFilter::buildFilterDAG: after: kAuto\n");
        } else if (cs == SkSVGColorspace::kSRGB) {
            SkDebugf("SkSVGFilter::buildFilterDAG: after: kSRGB\n");
        } else if (cs == SkSVGColorspace::kLinearRGB) {
            SkDebugf("SkSVGFilter::buildFilterDAG: after: kLinearRGB\n");
        }
        filter = feNode.makeImageFilter(localCtx, fctx);

        if (!feResultType.isEmpty()) {
            fctx.registerResult(feResultType, filter, filterSubregion, cs);
        }

        // Unspecified 'in' and 'in2' inputs implicitly resolve to the previous filter's result.
        fctx.setPreviousResult(filter, filterSubregion, cs);
    }

    // Convert to final destination colorspace
    if (cs != SkSVGColorspace::kSRGB) {
        filter = SkImageFilters::ColorFilter(SkColorFilters::LinearToSRGBGamma(), filter);
    }

    return filter;
}
