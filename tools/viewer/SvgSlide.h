/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SvgSlide_DEFINED
#define SvgSlide_DEFINED

#include "tools/viewer/Slide.h"

class SkSVGDOM;

class SvgSlide final : public Slide {
public:
    SvgSlide(const SkString& name, const SkString& path);
    SvgSlide(const SkString& name, std::unique_ptr<SkStream>);

    void load(SkScalar winWidth, SkScalar winHeight) override;
    void unload() override;
    void resize(SkScalar, SkScalar) override;

    SkISize getDimensions() const override;

    void draw(SkCanvas*) override;
private:
    std::unique_ptr<SkStream> fStream;
    SkSize fWinSize = SkSize::MakeEmpty();
    sk_sp<SkSVGDOM> fDom;

    typedef Slide INHERITED;
};

#endif // SvgSlide_DEFINED
