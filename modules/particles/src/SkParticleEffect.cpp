/*
* Copyright 2019 Google LLC
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include "modules/particles/include/SkParticleEffect.h"

#include "include/core/SkPaint.h"
#include "include/private/SkOnce.h"
#include "include/private/SkTPin.h"
#include "modules/particles/include/SkParticleBinding.h"
#include "modules/particles/include/SkParticleDrawable.h"
#include "modules/particles/include/SkReflected.h"
#include "modules/skresources/include/SkResources.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkPaintPriv.h"
#include "src/core/SkVM.h"
#include "src/sksl/SkSLCompiler.h"
#include "src/sksl/SkSLUtil.h"
#include "src/sksl/codegen/SkSLVMCodeGenerator.h"
#include "src/sksl/ir/SkSLProgram.h"

// Cached state for a single program (either all Effect code, or all Particle code)
struct SkParticleProgram {
    SkParticleProgram(skvm::Program effectSpawn,
                      skvm::Program effectUpdate,
                      skvm::Program spawn,
                      skvm::Program update,
                      std::vector<std::unique_ptr<SkSL::ExternalFunction>> externalFunctions,
                      skvm::Uniforms externalFunctionUniforms,
                      std::unique_ptr<SkArenaAlloc> alloc,
                      std::unique_ptr<SkSL::UniformInfo> uniformInfo)
            : fEffectSpawn(std::move(effectSpawn))
            , fEffectUpdate(std::move(effectUpdate))
            , fSpawn(std::move(spawn))
            , fUpdate(std::move(update))
            , fExternalFunctions(std::move(externalFunctions))
            , fExternalFunctionUniforms(std::move(externalFunctionUniforms))
            , fAlloc(std::move(alloc))
            , fUniformInfo(std::move(uniformInfo)) {}

    // Programs for each entry point
    skvm::Program fEffectSpawn;
    skvm::Program fEffectUpdate;
    skvm::Program fSpawn;
    skvm::Program fUpdate;

    // External functions created by each SkParticleBinding
    std::vector<std::unique_ptr<SkSL::ExternalFunction>> fExternalFunctions;

    // Storage for uniforms generated by external functions
    skvm::Uniforms fExternalFunctionUniforms;
    std::unique_ptr<SkArenaAlloc> fAlloc;

    // Information about uniforms declared in the SkSL
    std::unique_ptr<SkSL::UniformInfo> fUniformInfo;
};

static const char* kCommonHeader =
R"(
struct Effect {
  float  age;
  float  lifetime;
  int    loop;
  float  rate;
  int    burst;

  float2 pos;
  float2 dir;
  float  scale;
  float2 vel;
  float  spin;
  float4 color;
  float  frame;
  float  seed;
};

struct Particle {
  float  age;
  float  lifetime;
  float2 pos;
  float2 dir;
  float  scale;
  float2 vel;
  float  spin;
  float4 color;
  float  frame;
  float  seed;
};

uniform float dt;
uniform Effect effect;

// We use a not-very-random pure-float PRNG. It does have nice properties for our situation:
// It's fast-ish. Importantly, it only uses types and operations that exist in public SkSL's
// minimum spec (no bitwise operations on integers).
float rand(inout float seed) {
  seed = sin(31*seed) + sin(19*seed + 1);
  return fract(abs(10*seed));
}
)";

static const char* kDefaultCode =
R"(void effectSpawn(inout Effect effect) {
}

void effectUpdate(inout Effect effect) {
}

void spawn(inout Particle p) {
}

void update(inout Particle p) {
}
)";

SkParticleEffectParams::SkParticleEffectParams()
        : fMaxCount(128)
        , fDrawable(nullptr)
        , fCode(kDefaultCode) {}

void SkParticleEffectParams::visitFields(SkFieldVisitor* v) {
    v->visit("MaxCount", fMaxCount);
    v->visit("Drawable", fDrawable);
    v->visit("Code",     fCode);
    v->visit("Bindings", fBindings);
}

void SkParticleEffectParams::prepare(const skresources::ResourceProvider* resourceProvider) {
    for (auto& binding : fBindings) {
        if (binding) {
            binding->prepare(resourceProvider);
        }
    }
    if (fDrawable) {
        fDrawable->prepare(resourceProvider);
    }

    auto buildProgram = [this](const std::string& code) -> std::unique_ptr<SkParticleProgram> {
        std::unique_ptr<SkSL::ShaderCaps> caps = SkSL::ShaderCapsFactory::Standalone();
        SkSL::Compiler compiler(caps.get());

        // We use two separate blocks of uniforms (ie two args of stride 0). The first is for skvm
        // uniforms generated by any external functions. These are managed with a Uniforms instance,
        // and after it's populated, the values never need to be touched again.
        // The second uniform arg is for things declared as 'uniform' in the SkSL (including the
        // built-in declarations of 'dt' and 'effect').
        skvm::Uniforms efUniforms(skvm::UPtr{{0}}, 0);
        auto alloc = std::make_unique<SkArenaAlloc>(0);

        std::vector<std::unique_ptr<SkSL::ExternalFunction>> externalFns;
        externalFns.reserve(fBindings.size());
        for (const auto& binding : fBindings) {
            if (binding) {
                externalFns.push_back(binding->toFunction(compiler, &efUniforms, alloc.get()));
            }
        }

        SkSL::ProgramSettings settings;
        settings.fExternalFunctions = &externalFns;

        auto program = compiler.convertProgram(SkSL::ProgramKind::kGeneric, code, settings);
        if (!program) {
            SkDebugf("%s\n", compiler.errorText().c_str());
            return nullptr;
        }

        std::unique_ptr<SkSL::UniformInfo> uniformInfo = SkSL::Program_GetUniformInfo(*program);

        // For each entry point, convert to an skvm::Program. We need a fresh Builder and uniform
        // IDs (though we can reuse the Uniforms object, thanks to how it works).
        auto buildFunction = [&](const char* name){
            auto fn = SkSL::Program_GetFunction(*program, name);
            if (!fn) {
                return skvm::Program{};
            }

            skvm::Builder b;
            skvm::UPtr efUniformPtr   = b.uniform(),  // aka efUniforms.base
                       skslUniformPtr = b.uniform();
            (void)efUniformPtr;

            std::vector<skvm::Val> uniformIDs;
            for (int i = 0; i < uniformInfo->fUniformSlotCount; ++i) {
                uniformIDs.push_back(b.uniform32(skslUniformPtr, i * sizeof(int)).id);
            }
            if (!SkSL::ProgramToSkVM(*program, *fn, &b, /*debugTrace=*/nullptr,
                                     SkSpan(uniformIDs))) {
                return skvm::Program{};
            }
            return b.done();
        };

        skvm::Program effectSpawn  = buildFunction("effectSpawn"),
                      effectUpdate = buildFunction("effectUpdate"),
                      spawn        = buildFunction("spawn"),
                      update       = buildFunction("update");

        return std::make_unique<SkParticleProgram>(std::move(effectSpawn),
                                                   std::move(effectUpdate),
                                                   std::move(spawn),
                                                   std::move(update),
                                                   std::move(externalFns),
                                                   std::move(efUniforms),
                                                   std::move(alloc),
                                                   std::move(uniformInfo));
    };

    std::string particleCode(kCommonHeader);
    particleCode.append(fCode.c_str());

    if (auto prog = buildProgram(particleCode)) {
        fProgram = std::move(prog);
    }
}

SkParticleEffect::SkParticleEffect(sk_sp<SkParticleEffectParams> params)
        : fParams(std::move(params))
        , fLooping(false)
        , fCount(0)
        , fLastTime(-1.0)
        , fSpawnRemainder(0.0f) {
    fState.fAge = -1.0f;
    this->updateStorage();
}

void SkParticleEffect::updateStorage() {
    // Handle user edits to fMaxCount
    if (fParams->fMaxCount != fCapacity) {
        this->setCapacity(fParams->fMaxCount);
    }

    // Ensure our storage block for uniforms is large enough
    if (this->uniformInfo()) {
        int newCount = this->uniformInfo()->fUniformSlotCount;
        if (newCount > fUniforms.count()) {
            fUniforms.push_back_n(newCount - fUniforms.count(), 0.0f);
        } else {
            fUniforms.resize(newCount);
        }
    }
}

bool SkParticleEffect::setUniform(const char* name, const float* val, int count) {
    const SkSL::UniformInfo* info = this->uniformInfo();
    if (!info) {
        return false;
    }

    auto it = std::find_if(info->fUniforms.begin(), info->fUniforms.end(),
                           [name](const auto& u) { return u.fName == name; });
    if (it == info->fUniforms.end()) {
        return false;
    }
    if (it->fRows * it->fColumns != count) {
        return false;
    }

    std::copy(val, val + count, this->uniformData() + it->fSlot);
    return true;
}

void SkParticleEffect::start(double now, bool looping, SkPoint position, SkVector heading,
                             float scale, SkVector velocity, float spin, SkColor4f color,
                             float frame, float seed) {
    fCount = 0;
    fLastTime = now;
    fSpawnRemainder = 0.0f;
    fLooping = looping;

    fState.fAge = 0.0f;

    // A default lifetime makes sense - many effects are simple loops that don't really care.
    // Every effect should define its own rate of emission, or only use bursts, so leave that as
    // zero initially.
    fState.fLifetime = 1.0f;
    fState.fLoopCount = 0;
    fState.fRate = 0.0f;
    fState.fBurst = 0;

    fState.fPosition = position;
    fState.fHeading  = heading;
    fState.fScale    = scale;
    fState.fVelocity = velocity;
    fState.fSpin     = spin;
    fState.fColor    = color;
    fState.fFrame    = frame;
    fState.fRandom   = seed;

    // Defer running effectSpawn until the first update (to reuse the code when looping)
}

// Just the update step from our "rand" function
static float advance_seed(float x) {
    return sinf(31*x) + sinf(19*x + 1);
}

void SkParticleEffect::runEffectScript(EntryPoint entryPoint) {
    if (!fParams->fProgram) {
        return;
    }

    const skvm::Program& prog = entryPoint == EntryPoint::kSpawn ? fParams->fProgram->fEffectSpawn
                                                                 : fParams->fProgram->fEffectUpdate;
    if (prog.empty()) {
        return;
    }

    constexpr size_t kNumEffectArgs = sizeof(EffectState) / sizeof(int);
    void* args[kNumEffectArgs
               + 1    // external function uniforms
               + 1];  // SkSL uniforms

    args[0] = fParams->fProgram->fExternalFunctionUniforms.buf.data();
    args[1] = fUniforms.data();
    for (size_t i = 0; i < kNumEffectArgs; ++i) {
        args[i + 2] = SkTAddOffset<void>(&fState, i * sizeof(int));
    }

    memcpy(&fUniforms[1], &fState.fAge, sizeof(EffectState));
    prog.eval(1, args);
}

void SkParticleEffect::runParticleScript(EntryPoint entryPoint, int start, int count) {
    if (!fParams->fProgram) {
        return;
    }

    const skvm::Program& prog = entryPoint == EntryPoint::kSpawn ? fParams->fProgram->fSpawn
                                                                 : fParams->fProgram->fUpdate;
    if (prog.empty()) {
        return;
    }

    void* args[SkParticles::kNumChannels
               + 1    // external function uniforms
               + 1];  // SkSL uniforms
    args[0] = fParams->fProgram->fExternalFunctionUniforms.buf.data();
    args[1] = fUniforms.data();
    for (int i = 0; i < SkParticles::kNumChannels; ++i) {
        args[i + 2] = fParticles.fData[i].get() + start;
    }

    memcpy(&fUniforms[1], &fState.fAge, sizeof(EffectState));
    prog.eval(count, args);
}

void SkParticleEffect::advanceTime(double now) {
    // TODO: Sub-frame spawning. Tricky with script driven position. Supply variable effect.age?
    // Could be done if effect.age were an external value that offset by particle lane, perhaps.
    float deltaTime = static_cast<float>(now - fLastTime);
    if (deltaTime <= 0.0f) {
        return;
    }
    fLastTime = now;

    // Possibly re-allocate cached storage, if our params have changed
    this->updateStorage();

    // Copy known values into the uniform blocks
    if (fParams->fProgram) {
        fUniforms[0] = deltaTime;
    }

    // Is this the first update after calling start()?
    // Run 'effectSpawn' to set initial emitter properties.
    if (fState.fAge == 0.0f && fState.fLoopCount == 0) {
        this->runEffectScript(EntryPoint::kSpawn);
    }

    fState.fAge += deltaTime / fState.fLifetime;
    if (fState.fAge > 1) {
        if (fLooping) {
            // If we looped, then run effectSpawn again (with the updated loop count)
            fState.fLoopCount += sk_float_floor2int(fState.fAge);
            fState.fAge = fmodf(fState.fAge, 1.0f);
            this->runEffectScript(EntryPoint::kSpawn);
        } else {
            // Effect is dead if we've reached the end (and are not looping)
            return;
        }
    }

    // Advance age for existing particles, and remove any that have reached their end of life
    for (int i = 0; i < fCount; ++i) {
        fParticles.fData[SkParticles::kAge][i] +=
                fParticles.fData[SkParticles::kLifetime][i] * deltaTime;
        if (fParticles.fData[SkParticles::kAge][i] > 1.0f) {
            // NOTE: This is fast, but doesn't preserve drawing order. Could be a problem...
            for (int j = 0; j < SkParticles::kNumChannels; ++j) {
                fParticles.fData[j][i] = fParticles.fData[j][fCount - 1];
            }
            fStableRandoms[i] = fStableRandoms[fCount - 1];
            --i;
            --fCount;
        }
    }

    // Run 'effectUpdate' to adjust emitter properties
    this->runEffectScript(EntryPoint::kUpdate);

    // Do integration of effect position and orientation
    {
        fState.fPosition += fState.fVelocity * deltaTime;
        float s = sk_float_sin(fState.fSpin * deltaTime),
              c = sk_float_cos(fState.fSpin * deltaTime);
        // Using setNormalize to prevent scale drift
        fState.fHeading.setNormalize(fState.fHeading.fX * c - fState.fHeading.fY * s,
                                     fState.fHeading.fX * s + fState.fHeading.fY * c);
    }

    // Spawn new particles
    float desired = fState.fRate * deltaTime + fSpawnRemainder + fState.fBurst;
    fState.fBurst = 0;
    int numToSpawn = sk_float_round2int(desired);
    fSpawnRemainder = desired - numToSpawn;
    numToSpawn = SkTPin(numToSpawn, 0, fParams->fMaxCount - fCount);
    if (numToSpawn) {
        const int spawnBase = fCount;

        for (int i = 0; i < numToSpawn; ++i) {
            // Mutate our random seed so each particle definitely gets a different generator
            fState.fRandom = advance_seed(fState.fRandom);
            fParticles.fData[SkParticles::kAge            ][fCount] = 0.0f;
            fParticles.fData[SkParticles::kLifetime       ][fCount] = 0.0f;
            fParticles.fData[SkParticles::kPositionX      ][fCount] = fState.fPosition.fX;
            fParticles.fData[SkParticles::kPositionY      ][fCount] = fState.fPosition.fY;
            fParticles.fData[SkParticles::kHeadingX       ][fCount] = fState.fHeading.fX;
            fParticles.fData[SkParticles::kHeadingY       ][fCount] = fState.fHeading.fY;
            fParticles.fData[SkParticles::kScale          ][fCount] = fState.fScale;
            fParticles.fData[SkParticles::kVelocityX      ][fCount] = fState.fVelocity.fX;
            fParticles.fData[SkParticles::kVelocityY      ][fCount] = fState.fVelocity.fY;
            fParticles.fData[SkParticles::kVelocityAngular][fCount] = fState.fSpin;
            fParticles.fData[SkParticles::kColorR         ][fCount] = fState.fColor.fR;
            fParticles.fData[SkParticles::kColorG         ][fCount] = fState.fColor.fG;
            fParticles.fData[SkParticles::kColorB         ][fCount] = fState.fColor.fB;
            fParticles.fData[SkParticles::kColorA         ][fCount] = fState.fColor.fA;
            fParticles.fData[SkParticles::kSpriteFrame    ][fCount] = fState.fFrame;
            fParticles.fData[SkParticles::kRandom         ][fCount] = fState.fRandom;
            fCount++;
        }

        // Run the spawn script
        this->runParticleScript(EntryPoint::kSpawn, spawnBase, numToSpawn);

        // Now stash copies of the random seeds and compute inverse particle lifetimes
        // (so that subsequent updates are faster)
        for (int i = spawnBase; i < fCount; ++i) {
            fParticles.fData[SkParticles::kLifetime][i] =
                    sk_ieee_float_divide(1.0f, fParticles.fData[SkParticles::kLifetime][i]);
            fStableRandoms[i] = fParticles.fData[SkParticles::kRandom][i];
        }
    }

    // Restore all stable random seeds so update scripts get consistent behavior each frame
    for (int i = 0; i < fCount; ++i) {
        fParticles.fData[SkParticles::kRandom][i] = fStableRandoms[i];
    }

    // Run the update script
    this->runParticleScript(EntryPoint::kUpdate, 0, fCount);

    // Do fixed-function update work (integration of position and orientation)
    for (int i = 0; i < fCount; ++i) {
        fParticles.fData[SkParticles::kPositionX][i] +=
                fParticles.fData[SkParticles::kVelocityX][i] * deltaTime;
        fParticles.fData[SkParticles::kPositionY][i] +=
                fParticles.fData[SkParticles::kVelocityY][i] * deltaTime;

        float spin = fParticles.fData[SkParticles::kVelocityAngular][i];
        float s = sk_float_sin(spin * deltaTime),
              c = sk_float_cos(spin * deltaTime);
        float oldHeadingX = fParticles.fData[SkParticles::kHeadingX][i],
              oldHeadingY = fParticles.fData[SkParticles::kHeadingY][i];
        fParticles.fData[SkParticles::kHeadingX][i] = oldHeadingX * c - oldHeadingY * s;
        fParticles.fData[SkParticles::kHeadingY][i] = oldHeadingX * s + oldHeadingY * c;
    }
}

void SkParticleEffect::update(double now) {
    if (this->isAlive()) {
        this->advanceTime(now);
    }
}

void SkParticleEffect::draw(SkCanvas* canvas) {
    if (this->isAlive() && fParams->fDrawable) {
        fParams->fDrawable->draw(canvas, fParticles, fCount);
    }
}

void SkParticleEffect::setCapacity(int capacity) {
    for (int i = 0; i < SkParticles::kNumChannels; ++i) {
        fParticles.fData[i].realloc(capacity);
    }
    fStableRandoms.realloc(capacity);

    fCapacity = capacity;
    fCount = std::min(fCount, fCapacity);
}

const SkSL::UniformInfo* SkParticleEffect::uniformInfo() const {
    return fParams->fProgram ? fParams->fProgram->fUniformInfo.get() : nullptr;
}

void SkParticleEffect::RegisterParticleTypes() {
    static SkOnce once;
    once([]{
        REGISTER_REFLECTED(SkReflected);
        SkParticleBinding::RegisterBindingTypes();
        SkParticleDrawable::RegisterDrawableTypes();
    });
}
