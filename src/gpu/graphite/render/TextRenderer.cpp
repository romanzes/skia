/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/Renderer.h"

#include "src/gpu/AtlasTypes.h"
#include "src/gpu/graphite/render/TextDirectRenderStep.h"
#include "src/gpu/graphite/render/TextSDFRenderStep.h"

namespace skgpu::graphite {

const Renderer& Renderer::TextDirect() {
    static const TextDirectRenderStep kDirect;

    static const Renderer kTextDirectRenderer{"TextDirectRenderer",
                                              &kDirect};
    return kTextDirectRenderer;
}

const Renderer& Renderer::TextSDF(bool useLCDText) {
    static const TextSDFRenderStep kA8{false};
    static const TextSDFRenderStep kLCD{true};

    static const Renderer kTextSDFA8Renderer{"TextSDFA8Renderer",
                                             &kA8};
    static const Renderer kTextSDFLCDRenderer{"TextSDFLCDRenderer",
                                              &kLCD};

    if (useLCDText) {
        return kTextSDFLCDRenderer;
    } else {
        return kTextSDFA8Renderer;
    }
    SkUNREACHABLE;
}

} // namespace skgpu
