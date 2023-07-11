/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef skgpu_graphite_DrawPass_DEFINED
#define skgpu_graphite_DrawPass_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "src/core/SkEnumBitMask.h"
#include "src/core/SkTBlockList.h"
#include "src/gpu/graphite/AttachmentTypes.h"
#include "src/gpu/graphite/DrawCommands.h"
#include "src/gpu/graphite/DrawTypes.h"
#include "src/gpu/graphite/GraphicsPipelineDesc.h"
#include "src/gpu/graphite/ResourceTypes.h"

#include <memory>
#include <vector>

class SkTextureDataBlock;

namespace skgpu::graphite {

class BoundsManager;
class CommandBuffer;
class DrawList;
class GraphicsPipeline;
class Recorder;
struct RenderPassDesc;
class ResourceProvider;
class Sampler;
struct SamplerDesc;
class TextureProxy;
class Texture;
enum class UniformSlot;

/**
 * DrawPass is analogous to a subpass, storing the drawing operations in the order they are stored
 * in the eventual command buffer, as well as the surface proxy the operations are intended for.
 * DrawPasses are grouped into a RenderPassTask for execution within a single render pass if the
 * subpasses are compatible with each other.
 *
 * Unlike DrawList, DrawPasses are immutable and represent as closely as possible what will be
 * stored in the command buffer while being flexible as to how the pass is incorporated. Depending
 * on the backend, it may even be able to write accumulated vertex and uniform data directly to
 * mapped GPU memory, although that is the extent of the CPU->GPU work they perform before they are
 * executed by a RenderPassTask.
 */
class DrawPass {
public:
    ~DrawPass();

    // TODO: Replace SDC with the SDC's surface proxy view
    static std::unique_ptr<DrawPass> Make(Recorder*,
                                          std::unique_ptr<DrawList>,
                                          sk_sp<TextureProxy>,
                                          std::pair<LoadOp, StoreOp>,
                                          std::array<float, 4> clearColor);

    // Defined relative to the top-left corner of the surface the DrawPass renders to, and is
    // contained within its dimensions.
    const SkIRect&      bounds() const { return fBounds;       }
    TextureProxy* target() const { return fTarget.get(); }
    std::pair<LoadOp, StoreOp> ops() const { return fOps; }
    std::array<float, 4> clearColor() const { return fClearColor; }

    bool requiresDstTexture() const { return false;            }
    bool requiresMSAA()       const { return fRequiresMSAA;    }

    SkEnumBitMask<DepthStencilFlags> depthStencilFlags() const { return fDepthStencilFlags; }

    size_t vertexBufferSize()  const { return 0; }
    size_t uniformBufferSize() const { return 0; }

    // Instantiate and prepare any resources used by the DrawPass that require the Recorder's
    // ResourceProvider. This includes things likes GraphicsPipelines, sampled Textures, Samplers,
    // etc.
    bool prepareResources(ResourceProvider*, const RenderPassDesc&);

    DrawPassCommands::List::Iter commands() const {
        return fCommandList.commands();
    }

    const GraphicsPipeline* getPipeline(size_t index) const {
        return fFullPipelines[index].get();
    }
    const Texture* getTexture(size_t index) const;
    const Sampler* getSampler(size_t index) const;

    void addResourceRefs(CommandBuffer*) const;

private:
    class SortKey;
    class Drawer;

    DrawPass(sk_sp<TextureProxy> target,
             std::pair<LoadOp, StoreOp> ops,
             std::array<float, 4> clearColor,
             int renderStepCount);

    DrawPassCommands::List fCommandList;

    // The pipelines are referenced by index in BindGraphicsPipeline, but that will index into a
    // an array of actual GraphicsPipelines. fPipelineDescs only needs to accumulate encountered
    // GraphicsPipelineDescs and provide stable pointers, hence SkTBlockList.
    SkTBlockList<GraphicsPipelineDesc, 32> fPipelineDescs;

    std::vector<SamplerDesc> fSamplerDescs;

    sk_sp<TextureProxy> fTarget;
    SkIRect fBounds;

    std::pair<LoadOp, StoreOp> fOps;
    std::array<float, 4> fClearColor;

    SkEnumBitMask<DepthStencilFlags> fDepthStencilFlags = DepthStencilFlags::kNone;
    bool fRequiresMSAA = false;

    // These resources all get instantiated during prepareResources.
    // Use a vector instead of SkTBlockList for the full pipelines so that random access is fast.
    std::vector<sk_sp<GraphicsPipeline>> fFullPipelines;
    std::vector<sk_sp<TextureProxy>> fSampledTextures;
    std::vector<sk_sp<Sampler>> fSamplers;
};

} // namespace skgpu::graphite

#endif // skgpu_graphite_DrawPass_DEFINED
