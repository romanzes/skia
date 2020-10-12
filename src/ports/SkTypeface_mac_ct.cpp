/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)

#ifdef SK_BUILD_FOR_MAC
#import <ApplicationServices/ApplicationServices.h>
#endif

#ifdef SK_BUILD_FOR_IOS
#include <CoreText/CoreText.h>
#include <CoreText/CTFontManager.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkFontArguments.h"
#include "include/core/SkFontParameters.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkTypeface_mac.h"
#include "include/private/SkFixed.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkMutex.h"
#include "include/private/SkOnce.h"
#include "include/private/SkTDArray.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "src/core/SkAdvancedTypefaceMetrics.h"
#include "src/core/SkEndian.h"
#include "src/core/SkFontDescriptor.h"
#include "src/core/SkMask.h"
#include "src/core/SkScalerContext.h"
#include "src/core/SkTypefaceCache.h"
#include "src/core/SkUtils.h"
#include "src/ports/SkScalerContext_mac_ct.h"
#include "src/ports/SkTypeface_mac_ct.h"
#include "src/sfnt/SkOTTableTypes.h"
#include "src/sfnt/SkOTTable_OS_2.h"
#include "src/sfnt/SkOTTable_OS_2_V4.h"
#include "src/sfnt/SkOTUtils.h"
#include "src/sfnt/SkSFNTHeader.h"
#include "src/utils/SkUTF.h"
#include "src/utils/mac/SkCGBase.h"
#include "src/utils/mac/SkCGGeometry.h"
#include "src/utils/mac/SkCTFontSmoothBehavior.h"
#include "src/utils/mac/SkUniqueCFRef.h"

#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#include <memory>

// In macOS 10.12 and later any variation on the CGFont which has default axis value will be
// dropped when creating the CTFont. Unfortunately, in macOS 10.15 the priority of setting
// the optical size (and opsz variation) is
// 1. the value of kCTFontOpticalSizeAttribute in the CTFontDescriptor (undocumented)
// 2. the opsz axis default value if kCTFontOpticalSizeAttribute is 'none' (undocumented)
// 3. the opsz variation on the nascent CTFont from the CGFont (was dropped if default)
// 4. the opsz variation in kCTFontVariationAttribute in CTFontDescriptor (crashes 10.10)
// 5. the size requested (can fudge in SkTypeface but not SkScalerContext)
// The first one which is found will be used to set the opsz variation (after clamping).
static void add_opsz_attr(CFMutableDictionaryRef attr, double opsz) {
    SkUniqueCFRef<CFNumberRef> opszValueNumber(
        CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &opsz));
    // Avoid using kCTFontOpticalSizeAttribute directly
    CFStringRef SkCTFontOpticalSizeAttribute = CFSTR("NSCTFontOpticalSizeAttribute");
    CFDictionarySetValue(attr, SkCTFontOpticalSizeAttribute, opszValueNumber.get());
}

// This turns off application of the 'trak' table to advances, but also all other tracking.
static void add_notrak_attr(CFMutableDictionaryRef attr) {
    int zero = 0;
    SkUniqueCFRef<CFNumberRef> unscaledTrackingNumber(
        CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &zero));
    CFStringRef SkCTFontUnscaledTrackingAttribute = CFSTR("NSCTFontUnscaledTrackingAttribute");
    CFDictionarySetValue(attr, SkCTFontUnscaledTrackingAttribute, unscaledTrackingNumber.get());
}

SkUniqueCFRef<CTFontRef> SkCTFontCreateExactCopy(CTFontRef baseFont, CGFloat textSize,
                                                 OpszVariation opsz)
{
    SkUniqueCFRef<CFMutableDictionaryRef> attr(
    CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks));

    if (opsz.isSet) {
        add_opsz_attr(attr.get(), opsz.value);
#if !defined(SK_IGNORE_MAC_OPSZ_FORCE)
    } else {
        // On (at least) 10.10 though 10.14 the default system font was SFNSText/SFNSDisplay.
        // The CTFont is backed by both; optical size < 20 means SFNSText else SFNSDisplay.
        // On at least 10.11 the glyph ids in these fonts became non-interchangable.
        // To keep glyph ids stable over size changes, preserve the optical size.
        // In 10.15 this was replaced with use of variable fonts with an opsz axis.
        // A CTFont backed by multiple fonts picked by opsz where the multiple backing fonts are
        // variable fonts with opsz axis and non-interchangeable glyph ids would break the
        // opsz.isSet branch above, but hopefully that never happens.
        // See https://crbug.com/524646 .
        CFStringRef SkCTFontOpticalSizeAttribute = CFSTR("NSCTFontOpticalSizeAttribute");
        SkUniqueCFRef<CFTypeRef> opsz(CTFontCopyAttribute(baseFont, SkCTFontOpticalSizeAttribute));
        double opsz_val;
        if (!opsz ||
            CFGetTypeID(opsz.get()) != CFNumberGetTypeID() ||
            !CFNumberGetValue(static_cast<CFNumberRef>(opsz.get()),kCFNumberDoubleType,&opsz_val) ||
            opsz_val <= 0)
        {
            opsz_val = CTFontGetSize(baseFont);
        }
        add_opsz_attr(attr.get(), opsz_val);
#endif
    }
    add_notrak_attr(attr.get());

    SkUniqueCFRef<CTFontDescriptorRef> desc(CTFontDescriptorCreateWithAttributes(attr.get()));

#if !defined(SK_IGNORE_MAC_OPSZ_FORCE)
    return SkUniqueCFRef<CTFontRef>(
            CTFontCreateCopyWithAttributes(baseFont, textSize, nullptr, desc.get()));
#else
    SkUniqueCFRef<CGFontRef> baseCGFont(CTFontCopyGraphicsFont(baseFont, nullptr));
    return SkUniqueCFRef<CTFontRef>(
            CTFontCreateWithGraphicsFont(baseCGFont.get(), textSize, nullptr, desc.get()));

#endif
}

CTFontRef SkTypeface_GetCTFontRef(const SkTypeface* face) {
    return face ? (CTFontRef)face->internal_private_getCTFontRef() : nullptr;
}

static bool find_by_CTFontRef(SkTypeface* cached, void* context) {
    CTFontRef self = (CTFontRef)context;
    CTFontRef other = (CTFontRef)cached->internal_private_getCTFontRef();

    return CFEqual(self, other);
}

/** Creates a typeface, searching the cache if isLocalStream is false. */
sk_sp<SkTypeface> SkTypeface_Mac::Make(SkUniqueCFRef<CTFontRef> font,
                                       OpszVariation opszVariation,
                                       std::unique_ptr<SkStreamAsset> providedData) {
    static SkMutex gTFCacheMutex;
    static SkTypefaceCache gTFCache;

    SkASSERT(font);
    const bool isFromStream(providedData);

    if (!isFromStream) {
        SkAutoMutexExclusive ama(gTFCacheMutex);
        sk_sp<SkTypeface> face = gTFCache.findByProcAndRef(find_by_CTFontRef, (void*)font.get());
        if (face) {
            return face;
        }
    }

    SkUniqueCFRef<CTFontDescriptorRef> desc(CTFontCopyFontDescriptor(font.get()));
    SkFontStyle style = SkCTFontDescriptorGetSkFontStyle(desc.get(), isFromStream);
    CTFontSymbolicTraits traits = CTFontGetSymbolicTraits(font.get());
    bool isFixedPitch = SkToBool(traits & kCTFontMonoSpaceTrait);

    sk_sp<SkTypeface> face(new SkTypeface_Mac(std::move(font), style,
                                              isFixedPitch, opszVariation,
                                              std::move(providedData)));
    if (!isFromStream) {
        SkAutoMutexExclusive ama(gTFCacheMutex);
        gTFCache.add(face);
    }
    return face;
}

/*  This function is visible on the outside. It first searches the cache, and if
 *  not found, returns a new entry (after adding it to the cache).
 */
sk_sp<SkTypeface> SkMakeTypefaceFromCTFont(CTFontRef font) {
    CFRetain(font);
    return SkTypeface_Mac::Make(SkUniqueCFRef<CTFontRef>(font),
                                OpszVariation(),
                                nullptr);
}

static bool find_dict_CGFloat(CFDictionaryRef dict, CFStringRef name, CGFloat* value) {
    CFNumberRef num;
    return CFDictionaryGetValueIfPresent(dict, name, (const void**)&num)
        && CFNumberIsFloatType(num)
        && CFNumberGetValue(num, kCFNumberCGFloatType, value);
}

template <typename S, typename D, typename C> struct LinearInterpolater {
    struct Mapping {
        S src_val;
        D dst_val;
    };
    constexpr LinearInterpolater(Mapping const mapping[], int mappingCount)
        : fMapping(mapping), fMappingCount(mappingCount) {}

    static D map(S value, S src_min, S src_max, D dst_min, D dst_max) {
        SkASSERT(src_min < src_max);
        SkASSERT(dst_min <= dst_max);
        return C()(dst_min + (((value - src_min) * (dst_max - dst_min)) / (src_max - src_min)));
    }

    D map(S val) const {
        // -Inf to [0]
        if (val < fMapping[0].src_val) {
            return fMapping[0].dst_val;
        }

        // Linear from [i] to [i+1]
        for (int i = 0; i < fMappingCount - 1; ++i) {
            if (val < fMapping[i+1].src_val) {
                return map(val, fMapping[i].src_val, fMapping[i+1].src_val,
                                fMapping[i].dst_val, fMapping[i+1].dst_val);
            }
        }

        // From [n] to +Inf
        // if (fcweight < Inf)
        return fMapping[fMappingCount - 1].dst_val;
    }

    Mapping const * fMapping;
    int fMappingCount;
};

struct RoundCGFloatToInt {
    int operator()(CGFloat s) { return s + 0.5; }
};
struct CGFloatIdentity {
    CGFloat operator()(CGFloat s) { return s; }
};

/** Returns the [-1, 1] CTFontDescriptor weights for the
 *  <0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000> CSS weights.
 *
 *  It is assumed that the values will be interpolated linearly between these points.
 *  NSFontWeightXXX were added in 10.11, appear in 10.10, but do not appear in 10.9.
 *  The actual values appear to be stable, but they may change in the future without notice.
 */
static CGFloat(&get_NSFontWeight_mapping())[11] {

    // Declarations in <AppKit/AppKit.h> on macOS, <UIKit/UIKit.h> on iOS
#ifdef SK_BUILD_FOR_MAC
#  define SK_KIT_FONT_WEIGHT_PREFIX "NS"
#endif
#ifdef SK_BUILD_FOR_IOS
#  define SK_KIT_FONT_WEIGHT_PREFIX "UI"
#endif
    static constexpr struct {
        CGFloat defaultValue;
        const char* name;
    } nsFontWeightLoaderInfos[] = {
        { -0.80f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightUltraLight" },
        { -0.60f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightThin" },
        { -0.40f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightLight" },
        {  0.00f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightRegular" },
        {  0.23f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightMedium" },
        {  0.30f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightSemibold" },
        {  0.40f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightBold" },
        {  0.56f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightHeavy" },
        {  0.62f, SK_KIT_FONT_WEIGHT_PREFIX "FontWeightBlack" },
    };

    static_assert(SK_ARRAY_COUNT(nsFontWeightLoaderInfos) == 9, "");
    static CGFloat nsFontWeights[11];
    static SkOnce once;
    once([&] {
        size_t i = 0;
        nsFontWeights[i++] = -1.00;
        for (const auto& nsFontWeightLoaderInfo : nsFontWeightLoaderInfos) {
            void* nsFontWeightValuePtr = dlsym(RTLD_DEFAULT, nsFontWeightLoaderInfo.name);
            if (nsFontWeightValuePtr) {
                nsFontWeights[i++] = *(static_cast<CGFloat*>(nsFontWeightValuePtr));
            } else {
                nsFontWeights[i++] = nsFontWeightLoaderInfo.defaultValue;
            }
        }
        nsFontWeights[i++] = 1.00;
    });
    return nsFontWeights;
}

/** Convert the [0, 1000] CSS weight to [-1, 1] CTFontDescriptor weight (for system fonts).
 *
 *  The -1 to 1 weights reported by CTFontDescriptors have different mappings depending on if the
 *  CTFont is native or created from a CGDataProvider.
 */
CGFloat SkCTFontCTWeightForCSSWeight(int fontstyleWeight) {
    using Interpolator = LinearInterpolater<int, CGFloat, CGFloatIdentity>;

    // Note that Mac supports the old OS2 version A so 0 through 10 are as if multiplied by 100.
    // However, on this end we can't tell, so this is ignored.

    static Interpolator::Mapping nativeWeightMappings[11];
    static SkOnce once;
    once([&] {
        CGFloat(&nsFontWeights)[11] = get_NSFontWeight_mapping();
        for (int i = 0; i < 11; ++i) {
            nativeWeightMappings[i].src_val = i * 100;
            nativeWeightMappings[i].dst_val = nsFontWeights[i];
        }
    });
    static constexpr Interpolator nativeInterpolator(
            nativeWeightMappings, SK_ARRAY_COUNT(nativeWeightMappings));

    return nativeInterpolator.map(fontstyleWeight);
}


/** Convert the [-1, 1] CTFontDescriptor weight to [0, 1000] CSS weight.
 *
 *  The -1 to 1 weights reported by CTFontDescriptors have different mappings depending on if the
 *  CTFont is native or created from a CGDataProvider.
 */
static int ct_weight_to_fontstyle(CGFloat cgWeight, bool fromDataProvider) {
    using Interpolator = LinearInterpolater<CGFloat, int, RoundCGFloatToInt>;

    // Note that Mac supports the old OS2 version A so 0 through 10 are as if multiplied by 100.
    // However, on this end we can't tell, so this is ignored.

    /** This mapping for CGDataProvider created fonts is determined by creating font data with every
     *  weight, creating a CTFont, and asking the CTFont for its weight. See the TypefaceStyle test
     *  in tests/TypefaceTest.cpp for the code used to determine these values.
     */
    static constexpr Interpolator::Mapping dataProviderWeightMappings[] = {
        { -1.00,    0 },
        { -0.70,  100 },
        { -0.50,  200 },
        { -0.23,  300 },
        {  0.00,  400 },
        {  0.20,  500 },
        {  0.30,  600 },
        {  0.40,  700 },
        {  0.60,  800 },
        {  0.80,  900 },
        {  1.00, 1000 },
    };
    static constexpr Interpolator dataProviderInterpolator(
            dataProviderWeightMappings, SK_ARRAY_COUNT(dataProviderWeightMappings));

    static Interpolator::Mapping nativeWeightMappings[11];
    static SkOnce once;
    once([&] {
        CGFloat(&nsFontWeights)[11] = get_NSFontWeight_mapping();
        for (int i = 0; i < 11; ++i) {
            nativeWeightMappings[i].src_val = nsFontWeights[i];
            nativeWeightMappings[i].dst_val = i * 100;
        }
    });
    static constexpr Interpolator nativeInterpolator(
            nativeWeightMappings, SK_ARRAY_COUNT(nativeWeightMappings));

    return fromDataProvider ? dataProviderInterpolator.map(cgWeight)
                            : nativeInterpolator.map(cgWeight);
}

/** Convert the [0, 10] CSS weight to [-1, 1] CTFontDescriptor width. */
CGFloat SkCTFontCTWidthForCSSWidth(int fontstyleWidth) {
    using Interpolator = LinearInterpolater<int, CGFloat, CGFloatIdentity>;

    // Values determined by creating font data with every width, creating a CTFont,
    // and asking the CTFont for its width. See TypefaceStyle test for basics.
    static constexpr Interpolator::Mapping widthMappings[] = {
        {  0, -0.5 },
        { 10,  0.5 },
    };
    static constexpr Interpolator interpolator(widthMappings, SK_ARRAY_COUNT(widthMappings));
    return interpolator.map(fontstyleWidth);
}

/** Convert the [-1, 1] CTFontDescriptor width to [0, 10] CSS weight. */
static int ct_width_to_fontstyle(CGFloat cgWidth) {
    using Interpolator = LinearInterpolater<CGFloat, int, RoundCGFloatToInt>;

    // Values determined by creating font data with every width, creating a CTFont,
    // and asking the CTFont for its width. See TypefaceStyle test for basics.
    static constexpr Interpolator::Mapping widthMappings[] = {
        { -0.5,  0 },
        {  0.5, 10 },
    };
    static constexpr Interpolator interpolator(widthMappings, SK_ARRAY_COUNT(widthMappings));
    return interpolator.map(cgWidth);
}

SkFontStyle SkCTFontDescriptorGetSkFontStyle(CTFontDescriptorRef desc, bool fromDataProvider) {
    SkUniqueCFRef<CFTypeRef> traits(CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute));
    if (!traits || CFDictionaryGetTypeID() != CFGetTypeID(traits.get())) {
        return SkFontStyle();
    }
    SkUniqueCFRef<CFDictionaryRef> fontTraitsDict(static_cast<CFDictionaryRef>(traits.release()));

    CGFloat weight, width, slant;
    if (!find_dict_CGFloat(fontTraitsDict.get(), kCTFontWeightTrait, &weight)) {
        weight = 0;
    }
    if (!find_dict_CGFloat(fontTraitsDict.get(), kCTFontWidthTrait, &width)) {
        width = 0;
    }
    if (!find_dict_CGFloat(fontTraitsDict.get(), kCTFontSlantTrait, &slant)) {
        slant = 0;
    }

    return SkFontStyle(ct_weight_to_fontstyle(weight, fromDataProvider),
                       ct_width_to_fontstyle(width),
                       slant ? SkFontStyle::kItalic_Slant
                             : SkFontStyle::kUpright_Slant);
}


// Web fonts added to the CTFont registry do not return their character set.
// Iterate through the font in this case. The existing caller caches the result,
// so the performance impact isn't too bad.
static void populate_glyph_to_unicode_slow(CTFontRef ctFont, CFIndex glyphCount,
                                           SkUnichar* out) {
    sk_bzero(out, glyphCount * sizeof(SkUnichar));
    UniChar unichar = 0;
    while (glyphCount > 0) {
        CGGlyph glyph;
        if (CTFontGetGlyphsForCharacters(ctFont, &unichar, &glyph, 1)) {
            if (out[glyph] == 0) {
                out[glyph] = unichar;
                --glyphCount;
            }
        }
        if (++unichar == 0) {
            break;
        }
    }
}

static constexpr uint16_t kPlaneSize = 1 << 13;

static void get_plane_glyph_map(const uint8_t* bits,
                                CTFontRef ctFont,
                                CFIndex glyphCount,
                                SkUnichar* glyphToUnicode,
                                uint8_t planeIndex) {
    SkUnichar planeOrigin = (SkUnichar)planeIndex << 16; // top half of codepoint.
    for (uint16_t i = 0; i < kPlaneSize; i++) {
        uint8_t mask = bits[i];
        if (!mask) {
            continue;
        }
        for (uint8_t j = 0; j < 8; j++) {
            if (0 == (mask & ((uint8_t)1 << j))) {
                continue;
            }
            uint16_t planeOffset = (i << 3) | j;
            SkUnichar codepoint = planeOrigin | (SkUnichar)planeOffset;
            uint16_t utf16[2] = {planeOffset, 0};
            size_t count = 1;
            if (planeOrigin != 0) {
                count = SkUTF::ToUTF16(codepoint, utf16);
            }
            CGGlyph glyphs[2] = {0, 0};
            if (CTFontGetGlyphsForCharacters(ctFont, utf16, glyphs, count)) {
                SkASSERT(glyphs[1] == 0);
                SkASSERT(glyphs[0] < glyphCount);
                // CTFontCopyCharacterSet and CTFontGetGlyphsForCharacters seem to add 'support'
                // for characters 0x9, 0xA, and 0xD mapping them to the glyph for character 0x20?
                // Prefer mappings to codepoints at or above 0x20.
                if (glyphToUnicode[glyphs[0]] < 0x20) {
                    glyphToUnicode[glyphs[0]] = codepoint;
                }
            }
        }
    }
}
// Construct Glyph to Unicode table.
static void populate_glyph_to_unicode(CTFontRef ctFont, CFIndex glyphCount,
                                      SkUnichar* glyphToUnicode) {
    sk_bzero(glyphToUnicode, sizeof(SkUnichar) * glyphCount);
    SkUniqueCFRef<CFCharacterSetRef> charSet(CTFontCopyCharacterSet(ctFont));
    if (!charSet) {
        populate_glyph_to_unicode_slow(ctFont, glyphCount, glyphToUnicode);
        return;
    }

    SkUniqueCFRef<CFDataRef> bitmap(
            CFCharacterSetCreateBitmapRepresentation(nullptr, charSet.get()));
    if (!bitmap) {
        return;
    }
    CFIndex dataLength = CFDataGetLength(bitmap.get());
    if (!dataLength) {
        return;
    }
    SkASSERT(dataLength >= kPlaneSize);
    const UInt8* bits = CFDataGetBytePtr(bitmap.get());

    get_plane_glyph_map(bits, ctFont, glyphCount, glyphToUnicode, 0);
    /*
    A CFData object that specifies the bitmap representation of the Unicode
    character points the for the new character set. The bitmap representation could
    contain all the Unicode character range starting from BMP to Plane 16. The
    first 8KiB (8192 bytes) of the data represent the BMP range. The BMP range 8KiB
    can be followed by zero to sixteen 8KiB bitmaps, each prepended with the plane
    index byte. For example, the bitmap representing the BMP and Plane 2 has the
    size of 16385 bytes (8KiB for BMP, 1 byte index, and a 8KiB bitmap for Plane
    2). The plane index byte, in this case, contains the integer value two.
    */

    if (dataLength <= kPlaneSize) {
        return;
    }
    int extraPlaneCount = (dataLength - kPlaneSize) / (1 + kPlaneSize);
    SkASSERT(dataLength == kPlaneSize + extraPlaneCount * (1 + kPlaneSize));
    while (extraPlaneCount-- > 0) {
        bits += kPlaneSize;
        uint8_t planeIndex = *bits++;
        SkASSERT(planeIndex >= 1);
        SkASSERT(planeIndex <= 16);
        get_plane_glyph_map(bits, ctFont, glyphCount, glyphToUnicode, planeIndex);
    }
}

/** Assumes src and dst are not nullptr. */
void SkStringFromCFString(CFStringRef src, SkString* dst) {
    // Reserve enough room for the worst-case string,
    // plus 1 byte for the trailing null.
    CFIndex length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(src),
                                                       kCFStringEncodingUTF8) + 1;
    dst->resize(length);
    CFStringGetCString(src, dst->writable_str(), length, kCFStringEncodingUTF8);
    // Resize to the actual UTF-8 length used, stripping the null character.
    dst->resize(strlen(dst->c_str()));
}

void SkTypeface_Mac::getGlyphToUnicodeMap(SkUnichar* dstArray) const {
    SkUniqueCFRef<CTFontRef> ctFont =
            SkCTFontCreateExactCopy(fFontRef.get(), CTFontGetUnitsPerEm(fFontRef.get()),
                                    fOpszVariation);
    CFIndex glyphCount = CTFontGetGlyphCount(ctFont.get());
    populate_glyph_to_unicode(ctFont.get(), glyphCount, dstArray);
}

std::unique_ptr<SkAdvancedTypefaceMetrics> SkTypeface_Mac::onGetAdvancedMetrics() const {

    SkUniqueCFRef<CTFontRef> ctFont =
            SkCTFontCreateExactCopy(fFontRef.get(), CTFontGetUnitsPerEm(fFontRef.get()),
                                    fOpszVariation);

    std::unique_ptr<SkAdvancedTypefaceMetrics> info(new SkAdvancedTypefaceMetrics);

    {
        SkUniqueCFRef<CFStringRef> fontName(CTFontCopyPostScriptName(ctFont.get()));
        if (fontName.get()) {
            SkStringFromCFString(fontName.get(), &info->fPostScriptName);
            info->fFontName = info->fPostScriptName;
        }
    }

    // In 10.10 and earlier, CTFontCopyVariationAxes and CTFontCopyVariation do not work when
    // applied to fonts which started life with CGFontCreateWithDataProvider (they simply always
    // return nullptr). As a result, we are limited to CGFontCopyVariationAxes and
    // CGFontCopyVariations here until support for 10.10 and earlier is removed.
    SkUniqueCFRef<CGFontRef> cgFont(CTFontCopyGraphicsFont(ctFont.get(), nullptr));
    if (cgFont) {
        SkUniqueCFRef<CFArrayRef> cgAxes(CGFontCopyVariationAxes(cgFont.get()));
        if (cgAxes && CFArrayGetCount(cgAxes.get()) > 0) {
            info->fFlags |= SkAdvancedTypefaceMetrics::kMultiMaster_FontFlag;
        }
    }

    SkOTTableOS2_V4::Type fsType;
    if (sizeof(fsType) == this->getTableData(SkTEndian_SwapBE32(SkOTTableOS2::TAG),
                                             offsetof(SkOTTableOS2_V4, fsType),
                                             sizeof(fsType),
                                             &fsType)) {
        SkOTUtils::SetAdvancedTypefaceFlags(fsType, info.get());
    }

    // If it's not a truetype font, mark it as 'other'. Assume that TrueType
    // fonts always have both glyf and loca tables. At the least, this is what
    // sfntly needs to subset the font. CTFontCopyAttribute() does not always
    // succeed in determining this directly.
    if (!this->getTableSize('glyf') || !this->getTableSize('loca')) {
        return info;
    }

    info->fType = SkAdvancedTypefaceMetrics::kTrueType_Font;
    CTFontSymbolicTraits symbolicTraits = CTFontGetSymbolicTraits(ctFont.get());
    if (symbolicTraits & kCTFontMonoSpaceTrait) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kFixedPitch_Style;
    }
    if (symbolicTraits & kCTFontItalicTrait) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kItalic_Style;
    }
    CTFontStylisticClass stylisticClass = symbolicTraits & kCTFontClassMaskTrait;
    if (stylisticClass >= kCTFontOldStyleSerifsClass && stylisticClass <= kCTFontSlabSerifsClass) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kSerif_Style;
    } else if (stylisticClass & kCTFontScriptsClass) {
        info->fStyle |= SkAdvancedTypefaceMetrics::kScript_Style;
    }
    info->fItalicAngle = (int16_t) CTFontGetSlantAngle(ctFont.get());
    info->fAscent = (int16_t) CTFontGetAscent(ctFont.get());
    info->fDescent = (int16_t) CTFontGetDescent(ctFont.get());
    info->fCapHeight = (int16_t) CTFontGetCapHeight(ctFont.get());
    CGRect bbox = CTFontGetBoundingBox(ctFont.get());

    SkRect r;
    r.setLTRB(SkScalarFromCGFloat(SkCGRectGetMinX(bbox)),   // Left
              SkScalarFromCGFloat(SkCGRectGetMaxY(bbox)),   // Top
              SkScalarFromCGFloat(SkCGRectGetMaxX(bbox)),   // Right
              SkScalarFromCGFloat(SkCGRectGetMinY(bbox)));  // Bottom

    r.roundOut(&(info->fBBox));

    // Figure out a good guess for StemV - Min width of i, I, !, 1.
    // This probably isn't very good with an italic font.
    int16_t min_width = SHRT_MAX;
    info->fStemV = 0;
    static const UniChar stem_chars[] = {'i', 'I', '!', '1'};
    const size_t count = sizeof(stem_chars) / sizeof(stem_chars[0]);
    CGGlyph glyphs[count];
    CGRect boundingRects[count];
    if (CTFontGetGlyphsForCharacters(ctFont.get(), stem_chars, glyphs, count)) {
        CTFontGetBoundingRectsForGlyphs(ctFont.get(), kCTFontOrientationHorizontal,
                                        glyphs, boundingRects, count);
        for (size_t i = 0; i < count; i++) {
            int16_t width = (int16_t) boundingRects[i].size.width;
            if (width > 0 && width < min_width) {
                min_width = width;
                info->fStemV = min_width;
            }
        }
    }
    return info;
}

static SK_SFNT_ULONG get_font_type_tag(CTFontRef ctFont) {
    SkUniqueCFRef<CFNumberRef> fontFormatRef(
            static_cast<CFNumberRef>(CTFontCopyAttribute(ctFont, kCTFontFormatAttribute)));
    if (!fontFormatRef) {
        return 0;
    }

    SInt32 fontFormatValue;
    if (!CFNumberGetValue(fontFormatRef.get(), kCFNumberSInt32Type, &fontFormatValue)) {
        return 0;
    }

    switch (fontFormatValue) {
        case kCTFontFormatOpenTypePostScript:
            return SkSFNTHeader::fontType_OpenTypeCFF::TAG;
        case kCTFontFormatOpenTypeTrueType:
            return SkSFNTHeader::fontType_WindowsTrueType::TAG;
        case kCTFontFormatTrueType:
            return SkSFNTHeader::fontType_MacTrueType::TAG;
        case kCTFontFormatPostScript:
            return SkSFNTHeader::fontType_PostScript::TAG;
        case kCTFontFormatBitmap:
            return SkSFNTHeader::fontType_MacTrueType::TAG;
        case kCTFontFormatUnrecognized:
        default:
            return 0;
    }
}

std::unique_ptr<SkStreamAsset> SkTypeface_Mac::onOpenStream(int* ttcIndex) const {
    *ttcIndex = 0;

    fInitStream([this]{
    if (fStream) {
        return;
    }

    SK_SFNT_ULONG fontType = get_font_type_tag(fFontRef.get());

    // get table tags
    int numTables = this->countTables();
    SkTDArray<SkFontTableTag> tableTags;
    tableTags.setCount(numTables);
    this->getTableTags(tableTags.begin());

    // CT seems to be unreliable in being able to obtain the type,
    // even if all we want is the first four bytes of the font resource.
    // Just the presence of the FontForge 'FFTM' table seems to throw it off.
    if (fontType == 0) {
        fontType = SkSFNTHeader::fontType_WindowsTrueType::TAG;

        // see https://skbug.com/7630#c7
        bool couldBeCFF = false;
        constexpr SkFontTableTag CFFTag = SkSetFourByteTag('C', 'F', 'F', ' ');
        constexpr SkFontTableTag CFF2Tag = SkSetFourByteTag('C', 'F', 'F', '2');
        for (int tableIndex = 0; tableIndex < numTables; ++tableIndex) {
            if (CFFTag == tableTags[tableIndex] || CFF2Tag == tableTags[tableIndex]) {
                couldBeCFF = true;
            }
        }
        if (couldBeCFF) {
            fontType = SkSFNTHeader::fontType_OpenTypeCFF::TAG;
        }
    }

    // Sometimes CoreGraphics incorrectly thinks a font is kCTFontFormatPostScript.
    // It is exceedingly unlikely that this is the case, so double check
    // (see https://crbug.com/809763 ).
    if (fontType == SkSFNTHeader::fontType_PostScript::TAG) {
        // see if there are any required 'typ1' tables (see Adobe Technical Note #5180)
        bool couldBeTyp1 = false;
        constexpr SkFontTableTag TYPE1Tag = SkSetFourByteTag('T', 'Y', 'P', '1');
        constexpr SkFontTableTag CIDTag = SkSetFourByteTag('C', 'I', 'D', ' ');
        for (int tableIndex = 0; tableIndex < numTables; ++tableIndex) {
            if (TYPE1Tag == tableTags[tableIndex] || CIDTag == tableTags[tableIndex]) {
                couldBeTyp1 = true;
            }
        }
        if (!couldBeTyp1) {
            fontType = SkSFNTHeader::fontType_OpenTypeCFF::TAG;
        }
    }

    // get the table sizes and accumulate the total size of the font
    SkTDArray<size_t> tableSizes;
    size_t totalSize = sizeof(SkSFNTHeader) + sizeof(SkSFNTHeader::TableDirectoryEntry) * numTables;
    for (int tableIndex = 0; tableIndex < numTables; ++tableIndex) {
        size_t tableSize = this->getTableSize(tableTags[tableIndex]);
        totalSize += (tableSize + 3) & ~3;
        *tableSizes.append() = tableSize;
    }

    // reserve memory for stream, and zero it (tables must be zero padded)
    fStream.reset(new SkMemoryStream(totalSize));
    char* dataStart = (char*)fStream->getMemoryBase();
    sk_bzero(dataStart, totalSize);
    char* dataPtr = dataStart;

    // compute font header entries
    uint16_t entrySelector = 0;
    uint16_t searchRange = 1;
    while (searchRange < numTables >> 1) {
        entrySelector++;
        searchRange <<= 1;
    }
    searchRange <<= 4;
    uint16_t rangeShift = (numTables << 4) - searchRange;

    // write font header
    SkSFNTHeader* header = (SkSFNTHeader*)dataPtr;
    header->fontType = fontType;
    header->numTables = SkEndian_SwapBE16(numTables);
    header->searchRange = SkEndian_SwapBE16(searchRange);
    header->entrySelector = SkEndian_SwapBE16(entrySelector);
    header->rangeShift = SkEndian_SwapBE16(rangeShift);
    dataPtr += sizeof(SkSFNTHeader);

    // write tables
    SkSFNTHeader::TableDirectoryEntry* entry = (SkSFNTHeader::TableDirectoryEntry*)dataPtr;
    dataPtr += sizeof(SkSFNTHeader::TableDirectoryEntry) * numTables;
    for (int tableIndex = 0; tableIndex < numTables; ++tableIndex) {
        size_t tableSize = tableSizes[tableIndex];
        this->getTableData(tableTags[tableIndex], 0, tableSize, dataPtr);
        entry->tag = SkEndian_SwapBE32(tableTags[tableIndex]);
        entry->checksum = SkEndian_SwapBE32(SkOTUtils::CalcTableChecksum((SK_OT_ULONG*)dataPtr,
                                                                         tableSize));
        entry->offset = SkEndian_SwapBE32(SkToU32(dataPtr - dataStart));
        entry->logicalLength = SkEndian_SwapBE32(SkToU32(tableSize));

        dataPtr += (tableSize + 3) & ~3;
        ++entry;
    }
    });
    return fStream->duplicate();
}

struct NonDefaultAxesContext {
    SkFixed* axisValue;
    CFArrayRef cgAxes;
};
static void set_non_default_axes(CFTypeRef key, CFTypeRef value, void* context) {
    NonDefaultAxesContext* self = static_cast<NonDefaultAxesContext*>(context);

    if (CFGetTypeID(key) != CFStringGetTypeID() || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return;
    }

    // The key is a CFString which is a string from the 'name' table.
    // Search the cgAxes for an axis with this name, and use its index to store the value.
    CFIndex keyIndex = -1;
    CFStringRef keyString = static_cast<CFStringRef>(key);
    for (CFIndex i = 0; i < CFArrayGetCount(self->cgAxes); ++i) {
        CFTypeRef cgAxis = CFArrayGetValueAtIndex(self->cgAxes, i);
        if (CFGetTypeID(cgAxis) != CFDictionaryGetTypeID()) {
            continue;
        }

        CFDictionaryRef cgAxisDict = static_cast<CFDictionaryRef>(cgAxis);
        CFTypeRef cgAxisName = CFDictionaryGetValue(cgAxisDict, kCGFontVariationAxisName);
        if (!cgAxisName || CFGetTypeID(cgAxisName) != CFStringGetTypeID()) {
            continue;
        }
        CFStringRef cgAxisNameString = static_cast<CFStringRef>(cgAxisName);
        if (CFStringCompare(keyString, cgAxisNameString, 0) == kCFCompareEqualTo) {
            keyIndex = i;
            break;
        }
    }
    if (keyIndex == -1) {
        return;
    }

    CFNumberRef valueNumber = static_cast<CFNumberRef>(value);
    double valueDouble;
    if (!CFNumberGetValue(valueNumber, kCFNumberDoubleType, &valueDouble) ||
        valueDouble < SkFixedToDouble(SK_FixedMin) || SkFixedToDouble(SK_FixedMax) < valueDouble)
    {
        return;
    }
    self->axisValue[keyIndex] = SkDoubleToFixed(valueDouble);
}
static bool get_variations(CTFontRef ctFont, CFIndex* cgAxisCount,
                           SkAutoSTMalloc<4, SkFixed>* axisValues)
{
    // In 10.10 and earlier, CTFontCopyVariationAxes and CTFontCopyVariation do not work when
    // applied to fonts which started life with CGFontCreateWithDataProvider (they simply always
    // return nullptr). As a result, we are limited to CGFontCopyVariationAxes and
    // CGFontCopyVariations here until support for 10.10 and earlier is removed.
    SkUniqueCFRef<CGFontRef> cgFont(CTFontCopyGraphicsFont(ctFont, nullptr));
    if (!cgFont) {
        return false;
    }

    SkUniqueCFRef<CFDictionaryRef> cgVariations(CGFontCopyVariations(cgFont.get()));
    // If a font has no variations CGFontCopyVariations returns nullptr (instead of an empty dict).
    if (!cgVariations) {
        return false;
    }

    SkUniqueCFRef<CFArrayRef> cgAxes(CGFontCopyVariationAxes(cgFont.get()));
    if (!cgAxes) {
        return false;
    }
    *cgAxisCount = CFArrayGetCount(cgAxes.get());
    axisValues->reset(*cgAxisCount);

    // Set all of the axes to their default values.
    // Fail if any default value cannot be determined.
    for (CFIndex i = 0; i < *cgAxisCount; ++i) {
        CFTypeRef cgAxis = CFArrayGetValueAtIndex(cgAxes.get(), i);
        if (CFGetTypeID(cgAxis) != CFDictionaryGetTypeID()) {
            return false;
        }

        CFDictionaryRef cgAxisDict = static_cast<CFDictionaryRef>(cgAxis);
        CFTypeRef axisDefaultValue = CFDictionaryGetValue(cgAxisDict,
                                                          kCGFontVariationAxisDefaultValue);
        if (!axisDefaultValue || CFGetTypeID(axisDefaultValue) != CFNumberGetTypeID()) {
            return false;
        }
        CFNumberRef axisDefaultValueNumber = static_cast<CFNumberRef>(axisDefaultValue);
        double axisDefaultValueDouble;
        if (!CFNumberGetValue(axisDefaultValueNumber, kCFNumberDoubleType, &axisDefaultValueDouble))
        {
            return false;
        }
        if (axisDefaultValueDouble < SkFixedToDouble(SK_FixedMin) ||
                                     SkFixedToDouble(SK_FixedMax) < axisDefaultValueDouble)
        {
            return false;
        }
        (*axisValues)[(int)i] = SkDoubleToFixed(axisDefaultValueDouble);
    }

    // Override the default values with the given font's stated axis values.
    NonDefaultAxesContext c = { axisValues->get(), cgAxes.get() };
    CFDictionaryApplyFunction(cgVariations.get(), set_non_default_axes, &c);

    return true;
}
std::unique_ptr<SkFontData> SkTypeface_Mac::onMakeFontData() const {
    int index;
    std::unique_ptr<SkStreamAsset> stream(this->onOpenStream(&index));

    CFIndex cgAxisCount;
    SkAutoSTMalloc<4, SkFixed> axisValues;
    if (get_variations(fFontRef.get(), &cgAxisCount, &axisValues)) {
        return std::make_unique<SkFontData>(std::move(stream), index,
                                              axisValues.get(), cgAxisCount);
    }
    return std::make_unique<SkFontData>(std::move(stream), index, nullptr, 0);
}

/** Creates a CT variation dictionary {tag, value} from a CG variation dictionary {name, value}. */
static SkUniqueCFRef<CFDictionaryRef> ct_variation_from_cg_variation(CFDictionaryRef cgVariations,
                                                                     CFArrayRef ctAxes) {

    SkUniqueCFRef<CFMutableDictionaryRef> ctVariation(
            CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks));

    CFIndex axisCount = CFArrayGetCount(ctAxes);
    for (CFIndex i = 0; i < axisCount; ++i) {
        CFTypeRef axisInfo = CFArrayGetValueAtIndex(ctAxes, i);
        if (CFDictionaryGetTypeID() != CFGetTypeID(axisInfo)) {
            return nullptr;
        }
        CFDictionaryRef axisInfoDict = static_cast<CFDictionaryRef>(axisInfo);

        // The assumption is that values produced by kCTFontVariationAxisNameKey and
        // kCGFontVariationAxisName will always be equal.
        CFTypeRef axisName = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisNameKey);
        if (!axisName || CFGetTypeID(axisName) != CFStringGetTypeID()) {
            return nullptr;
        }

        CFTypeRef axisValue = CFDictionaryGetValue(cgVariations, axisName);
        if (!axisValue || CFGetTypeID(axisValue) != CFNumberGetTypeID()) {
            return nullptr;
        }

        CFTypeRef axisTag = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisIdentifierKey);
        if (!axisTag || CFGetTypeID(axisTag) != CFNumberGetTypeID()) {
            return nullptr;
        }

        CFDictionaryAddValue(ctVariation.get(), axisTag, axisValue);
    }
    return std::move(ctVariation);
}

int SkTypeface_Mac::onGetVariationDesignPosition(
        SkFontArguments::VariationPosition::Coordinate coordinates[], int coordinateCount) const
{
    // The CGFont variation data does not contain the tag.

    // CTFontCopyVariationAxes returns nullptr for CGFontCreateWithDataProvider fonts with
    // macOS 10.10 and iOS 9 or earlier. When this happens, there is no API to provide the tag.
    SkUniqueCFRef<CFArrayRef> ctAxes(CTFontCopyVariationAxes(fFontRef.get()));
    if (!ctAxes) {
        return -1;
    }
    CFIndex axisCount = CFArrayGetCount(ctAxes.get());
    if (!coordinates || coordinateCount < axisCount) {
        return axisCount;
    }

    // This call always returns nullptr on 10.11 and under for CGFontCreateWithDataProvider fonts.
    // When this happens, try converting the CG variation to a CT variation.
    // On 10.12 and later, this only returns non-default variations.
    SkUniqueCFRef<CFDictionaryRef> ctVariation(CTFontCopyVariation(fFontRef.get()));
    if (!ctVariation) {
        // When 10.11 and earlier are no longer supported, the following code can be replaced with
        // return -1 and ct_variation_from_cg_variation can be removed.
        SkUniqueCFRef<CGFontRef> cgFont(CTFontCopyGraphicsFont(fFontRef.get(), nullptr));
        if (!cgFont) {
            return -1;
        }
        SkUniqueCFRef<CFDictionaryRef> cgVariations(CGFontCopyVariations(cgFont.get()));
        if (!cgVariations) {
            return -1;
        }
        ctVariation = ct_variation_from_cg_variation(cgVariations.get(), ctAxes.get());
        if (!ctVariation) {
            return -1;
        }
    }

    for (int i = 0; i < axisCount; ++i) {
        CFTypeRef axisInfo = CFArrayGetValueAtIndex(ctAxes.get(), i);
        if (CFDictionaryGetTypeID() != CFGetTypeID(axisInfo)) {
            return -1;
        }
        CFDictionaryRef axisInfoDict = static_cast<CFDictionaryRef>(axisInfo);

        CFTypeRef tag = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisIdentifierKey);
        if (!tag || CFGetTypeID(tag) != CFNumberGetTypeID()) {
            return -1;
        }
        CFNumberRef tagNumber = static_cast<CFNumberRef>(tag);
        int64_t tagLong;
        if (!CFNumberGetValue(tagNumber, kCFNumberSInt64Type, &tagLong)) {
            return -1;
        }
        coordinates[i].axis = tagLong;

        CGFloat variationCGFloat;
        CFTypeRef variationValue = CFDictionaryGetValue(ctVariation.get(), tagNumber);
        if (variationValue) {
            if (CFGetTypeID(variationValue) != CFNumberGetTypeID()) {
                return -1;
            }
            CFNumberRef variationNumber = static_cast<CFNumberRef>(variationValue);
            if (!CFNumberGetValue(variationNumber, kCFNumberCGFloatType, &variationCGFloat)) {
                return -1;
            }
        } else {
            CFTypeRef def = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisDefaultValueKey);
            if (!def || CFGetTypeID(def) != CFNumberGetTypeID()) {
                return -1;
            }
            CFNumberRef defNumber = static_cast<CFNumberRef>(def);
            if (!CFNumberGetValue(defNumber, kCFNumberCGFloatType, &variationCGFloat)) {
                return -1;
            }
        }
        coordinates[i].value = SkScalarFromCGFloat(variationCGFloat);

    }
    return axisCount;
}

int SkTypeface_Mac::onGetUPEM() const {
    SkUniqueCFRef<CGFontRef> cgFont(CTFontCopyGraphicsFont(fFontRef.get(), nullptr));
    return CGFontGetUnitsPerEm(cgFont.get());
}

SkTypeface::LocalizedStrings* SkTypeface_Mac::onCreateFamilyNameIterator() const {
    sk_sp<SkTypeface::LocalizedStrings> nameIter =
            SkOTUtils::LocalizedStrings_NameTable::MakeForFamilyNames(*this);
    if (!nameIter) {
        CFStringRef cfLanguageRaw;
        SkUniqueCFRef<CFStringRef> cfFamilyName(
                CTFontCopyLocalizedName(fFontRef.get(), kCTFontFamilyNameKey, &cfLanguageRaw));
        SkUniqueCFRef<CFStringRef> cfLanguage(cfLanguageRaw);

        SkString skLanguage;
        SkString skFamilyName;
        if (cfLanguage) {
            SkStringFromCFString(cfLanguage.get(), &skLanguage);
        } else {
            skLanguage = "und"; //undetermined
        }
        if (cfFamilyName) {
            SkStringFromCFString(cfFamilyName.get(), &skFamilyName);
        }

        nameIter = sk_make_sp<SkOTUtils::LocalizedStrings_SingleName>(skFamilyName, skLanguage);
    }
    return nameIter.release();
}

int SkTypeface_Mac::onGetTableTags(SkFontTableTag tags[]) const {
    SkUniqueCFRef<CFArrayRef> cfArray(
            CTFontCopyAvailableTables(fFontRef.get(), kCTFontTableOptionNoOptions));
    if (!cfArray) {
        return 0;
    }
    int count = SkToInt(CFArrayGetCount(cfArray.get()));
    if (tags) {
        for (int i = 0; i < count; ++i) {
            uintptr_t fontTag = reinterpret_cast<uintptr_t>(
                CFArrayGetValueAtIndex(cfArray.get(), i));
            tags[i] = static_cast<SkFontTableTag>(fontTag);
        }
    }
    return count;
}

// If, as is the case with web fonts, the CTFont data isn't available,
// the CGFont data may work. While the CGFont may always provide the
// right result, leave the CTFont code path to minimize disruption.
static SkUniqueCFRef<CFDataRef> copy_table_from_font(CTFontRef ctFont, SkFontTableTag tag) {
    SkUniqueCFRef<CFDataRef> data(CTFontCopyTable(ctFont, (CTFontTableTag) tag,
                                                  kCTFontTableOptionNoOptions));
    if (!data) {
        SkUniqueCFRef<CGFontRef> cgFont(CTFontCopyGraphicsFont(ctFont, nullptr));
        data.reset(CGFontCopyTableForTag(cgFont.get(), tag));
    }
    return data;
}

size_t SkTypeface_Mac::onGetTableData(SkFontTableTag tag, size_t offset,
                                      size_t length, void* dstData) const {
    SkUniqueCFRef<CFDataRef> srcData = copy_table_from_font(fFontRef.get(), tag);
    if (!srcData) {
        return 0;
    }

    size_t srcSize = CFDataGetLength(srcData.get());
    if (offset >= srcSize) {
        return 0;
    }
    if (length > srcSize - offset) {
        length = srcSize - offset;
    }
    if (dstData) {
        memcpy(dstData, CFDataGetBytePtr(srcData.get()) + offset, length);
    }
    return length;
}

sk_sp<SkData> SkTypeface_Mac::onCopyTableData(SkFontTableTag tag) const {
    SkUniqueCFRef<CFDataRef> srcData = copy_table_from_font(fFontRef.get(), tag);
    if (!srcData) {
        return nullptr;
    }
    const UInt8* data = CFDataGetBytePtr(srcData.get());
    CFIndex length = CFDataGetLength(srcData.get());
    return SkData::MakeWithProc(data, length,
                                [](const void*, void* ctx) {
                                    CFRelease((CFDataRef)ctx);
                                }, (void*)srcData.release());
}

SkScalerContext* SkTypeface_Mac::onCreateScalerContext(const SkScalerContextEffects& effects,
                                                       const SkDescriptor* desc) const {
    return new SkScalerContext_Mac(sk_ref_sp(const_cast<SkTypeface_Mac*>(this)), effects, desc);
}

void SkTypeface_Mac::onFilterRec(SkScalerContextRec* rec) const {
    if (rec->fFlags & SkScalerContext::kLCD_BGROrder_Flag ||
        rec->fFlags & SkScalerContext::kLCD_Vertical_Flag)
    {
        rec->fMaskFormat = SkMask::kA8_Format;
        // Render the glyphs as close as possible to what was requested.
        // The above turns off subpixel rendering, but the user requested it.
        // Normal hinting will cause the A8 masks to be generated from CoreGraphics subpixel masks.
        // See comments below for more details.
        rec->setHinting(SkFontHinting::kNormal);
    }

    unsigned flagsWeDontSupport = SkScalerContext::kForceAutohinting_Flag  |
                                  SkScalerContext::kLCD_BGROrder_Flag |
                                  SkScalerContext::kLCD_Vertical_Flag;

    rec->fFlags &= ~flagsWeDontSupport;

    const SkCTFontSmoothBehavior smoothBehavior = SkCTFontGetSmoothBehavior();

    // Only two levels of hinting are supported.
    // kNo_Hinting means avoid CoreGraphics outline dilation (smoothing).
    // kNormal_Hinting means CoreGraphics outline dilation (smoothing) is allowed.
    if (rec->getHinting() != SkFontHinting::kNone) {
        rec->setHinting(SkFontHinting::kNormal);
    }
    // If smoothing has no effect, don't request it.
    if (smoothBehavior == SkCTFontSmoothBehavior::none) {
        rec->setHinting(SkFontHinting::kNone);
    }

    // FIXME: lcd smoothed un-hinted rasterization unsupported.
    // Tracked by http://code.google.com/p/skia/issues/detail?id=915 .
    // There is no current means to honor a request for unhinted lcd,
    // so arbitrarilly ignore the hinting request and honor lcd.

    // Hinting and smoothing should be orthogonal, but currently they are not.
    // CoreGraphics has no API to influence hinting. However, its lcd smoothed
    // output is drawn from auto-dilated outlines (the amount of which is
    // determined by AppleFontSmoothing). Its regular anti-aliased output is
    // drawn from un-dilated outlines.

    // The behavior of Skia is as follows:
    // [AA][no-hint]: generate AA using CoreGraphic's AA output.
    // [AA][yes-hint]: use CoreGraphic's LCD output and reduce it to a single
    // channel. This matches [LCD][yes-hint] in weight.
    // [LCD][no-hint]: currently unable to honor, and must pick which to respect.
    // Currently side with LCD, effectively ignoring the hinting setting.
    // [LCD][yes-hint]: generate LCD using CoreGraphic's LCD output.
    if (rec->fMaskFormat == SkMask::kLCD16_Format) {
        if (smoothBehavior == SkCTFontSmoothBehavior::subpixel) {
            //CoreGraphics creates 555 masks for smoothed text anyway.
            rec->fMaskFormat = SkMask::kLCD16_Format;
            rec->setHinting(SkFontHinting::kNormal);
        } else {
            rec->fMaskFormat = SkMask::kA8_Format;
            if (smoothBehavior != SkCTFontSmoothBehavior::none) {
                rec->setHinting(SkFontHinting::kNormal);
            }
        }
    }

    // CoreText provides no information as to whether a glyph will be color or not.
    // Fonts may mix outlines and bitmaps, so information is needed on a glyph by glyph basis.
    // If a font contains an 'sbix' table, consider it to be a color font, and disable lcd.
    if (fHasColorGlyphs) {
        rec->fMaskFormat = SkMask::kARGB32_Format;
    }

    // Unhinted A8 masks (those not derived from LCD masks) must respect SK_GAMMA_APPLY_TO_A8.
    // All other masks can use regular gamma.
    if (SkMask::kA8_Format == rec->fMaskFormat && SkFontHinting::kNone == rec->getHinting()) {
#ifndef SK_GAMMA_APPLY_TO_A8
        // SRGBTODO: Is this correct? Do we want contrast boost?
        rec->ignorePreBlend();
#endif
    } else {
        SkColor color = rec->getLuminanceColor();
        if (smoothBehavior == SkCTFontSmoothBehavior::some) {
            // CoreGraphics smoothed text without subpixel coverage blitting goes from a gamma of
            // 2.0 for black foreground to a gamma of 1.0 for white foreground. Emulate this
            // through the mask gamma by reducing the color values to 1/2.
            color = SkColorSetRGB(SkColorGetR(color) * 1/2,
                                  SkColorGetG(color) * 1/2,
                                  SkColorGetB(color) * 1/2);
        } else if (smoothBehavior == SkCTFontSmoothBehavior::subpixel) {
            // CoreGraphics smoothed text with subpixel coverage blitting goes from a gamma of
            // 2.0 for black foreground to a gamma of ~1.4? for white foreground. Emulate this
            // through the mask gamma by reducing the color values to 3/4.
            color = SkColorSetRGB(SkColorGetR(color) * 3/4,
                                  SkColorGetG(color) * 3/4,
                                  SkColorGetB(color) * 3/4);
        }
        rec->setLuminanceColor(color);

        // CoreGraphics dialates smoothed text to provide contrast.
        rec->setContrast(0);
    }
}

/** Takes ownership of the CFStringRef. */
static const char* get_str(CFStringRef ref, SkString* str) {
    if (nullptr == ref) {
        return nullptr;
    }
    SkStringFromCFString(ref, str);
    CFRelease(ref);
    return str->c_str();
}

void SkTypeface_Mac::onGetFamilyName(SkString* familyName) const {
    get_str(CTFontCopyFamilyName(fFontRef.get()), familyName);
}

void SkTypeface_Mac::onGetFontDescriptor(SkFontDescriptor* desc,
                                         bool* isLocalStream) const {
    SkString tmpStr;

    desc->setFamilyName(get_str(CTFontCopyFamilyName(fFontRef.get()), &tmpStr));
    desc->setFullName(get_str(CTFontCopyFullName(fFontRef.get()), &tmpStr));
    desc->setPostscriptName(get_str(CTFontCopyPostScriptName(fFontRef.get()), &tmpStr));
    desc->setStyle(this->fontStyle());
    *isLocalStream = fIsFromStream;
}

void SkTypeface_Mac::onCharsToGlyphs(const SkUnichar uni[], int count, SkGlyphID glyphs[]) const {
    // Undocumented behavior of CTFontGetGlyphsForCharacters with non-bmp code points:
    // When a surrogate pair is detected, the glyph index used is the index of the high surrogate.
    // It is documented that if a mapping is unavailable, the glyph will be set to 0.

    SkAutoSTMalloc<1024, UniChar> charStorage;
    const UniChar* src; // UniChar is a UTF-16 16-bit code unit.
    int srcCount;
    const SkUnichar* utf32 = reinterpret_cast<const SkUnichar*>(uni);
    UniChar* utf16 = charStorage.reset(2 * count);
    src = utf16;
    for (int i = 0; i < count; ++i) {
        utf16 += SkUTF::ToUTF16(utf32[i], utf16);
    }
    srcCount = SkToInt(utf16 - src);

    // If there are any non-bmp code points, the provided 'glyphs' storage will be inadequate.
    SkAutoSTMalloc<1024, uint16_t> glyphStorage;
    uint16_t* macGlyphs = glyphs;
    if (srcCount > count) {
        macGlyphs = glyphStorage.reset(srcCount);
    }

    CTFontGetGlyphsForCharacters(fFontRef.get(), src, macGlyphs, srcCount);

    // If there were any non-bmp, then copy and compact.
    // If all are bmp, 'glyphs' already contains the compact glyphs.
    // If some are non-bmp, copy and compact into 'glyphs'.
    if (srcCount > count) {
        SkASSERT(glyphs != macGlyphs);
        int extra = 0;
        for (int i = 0; i < count; ++i) {
            glyphs[i] = macGlyphs[i + extra];
            if (SkUTF16_IsLeadingSurrogate(src[i + extra])) {
                ++extra;
            }
        }
    } else {
        SkASSERT(glyphs == macGlyphs);
    }
}

int SkTypeface_Mac::onCountGlyphs() const {
    return SkToInt(CTFontGetGlyphCount(fFontRef.get()));
}

/** Creates a dictionary suitable for setting the axes on a CTFont. */
CTFontVariation SkCTVariationFromSkFontArguments(CTFontRef ct, const SkFontArguments& args) {
    OpszVariation opsz;
    constexpr const SkFourByteTag opszTag = SkSetFourByteTag('o','p','s','z');

    SkUniqueCFRef<CFArrayRef> ctAxes(CTFontCopyVariationAxes(ct));
    if (!ctAxes) {
        return CTFontVariation();
    }
    CFIndex axisCount = CFArrayGetCount(ctAxes.get());

    const SkFontArguments::VariationPosition position = args.getVariationDesignPosition();

    SkUniqueCFRef<CFMutableDictionaryRef> dict(
            CFDictionaryCreateMutable(kCFAllocatorDefault, axisCount,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks));

    for (int i = 0; i < axisCount; ++i) {
        CFTypeRef axisInfo = CFArrayGetValueAtIndex(ctAxes.get(), i);
        if (CFDictionaryGetTypeID() != CFGetTypeID(axisInfo)) {
            return CTFontVariation();
        }
        CFDictionaryRef axisInfoDict = static_cast<CFDictionaryRef>(axisInfo);

        CFTypeRef tag = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisIdentifierKey);
        if (!tag || CFGetTypeID(tag) != CFNumberGetTypeID()) {
            return CTFontVariation();
        }
        CFNumberRef tagNumber = static_cast<CFNumberRef>(tag);
        int64_t tagLong;
        if (!CFNumberGetValue(tagNumber, kCFNumberSInt64Type, &tagLong)) {
            return CTFontVariation();
        }

        // The variation axes can be set to any value, but cg will effectively pin them.
        // Pin them here to normalize.
        CFTypeRef min = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisMinimumValueKey);
        CFTypeRef max = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisMaximumValueKey);
        CFTypeRef def = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisDefaultValueKey);
        if (!min || CFGetTypeID(min) != CFNumberGetTypeID() ||
            !max || CFGetTypeID(max) != CFNumberGetTypeID() ||
            !def || CFGetTypeID(def) != CFNumberGetTypeID())
        {
            return CTFontVariation();
        }
        CFNumberRef minNumber = static_cast<CFNumberRef>(min);
        CFNumberRef maxNumber = static_cast<CFNumberRef>(max);
        CFNumberRef defNumber = static_cast<CFNumberRef>(def);
        double minDouble;
        double maxDouble;
        double defDouble;
        if (!CFNumberGetValue(minNumber, kCFNumberDoubleType, &minDouble) ||
            !CFNumberGetValue(maxNumber, kCFNumberDoubleType, &maxDouble) ||
            !CFNumberGetValue(defNumber, kCFNumberDoubleType, &defDouble))
        {
            return CTFontVariation();
        }

        double value = defDouble;
        // The position may be over specified. If there are multiple values for a given axis,
        // use the last one since that's what css-fonts-4 requires.
        for (int j = position.coordinateCount; j --> 0;) {
            if (position.coordinates[j].axis == tagLong) {
                value = SkTPin(SkScalarToDouble(position.coordinates[j].value),
                               minDouble, maxDouble);
                if (tagLong == opszTag) {
                    opsz.isSet = true;
                }
                break;
            }
        }
        if (tagLong == opszTag) {
            opsz.value = value;
        }
        SkUniqueCFRef<CFNumberRef> valueNumber(
            CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &value));
        CFDictionaryAddValue(dict.get(), tagNumber, valueNumber.get());
    }
    return { SkUniqueCFRef<CFDictionaryRef>(std::move(dict)), opsz };
}

sk_sp<SkTypeface> SkTypeface_Mac::onMakeClone(const SkFontArguments& args) const {
    CTFontVariation ctVariation = SkCTVariationFromSkFontArguments(fFontRef.get(), args);

    SkUniqueCFRef<CTFontRef> ctVariant;
    if (ctVariation.dict) {
        SkUniqueCFRef<CFMutableDictionaryRef> attributes(
                CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                          &kCFTypeDictionaryKeyCallBacks,
                                          &kCFTypeDictionaryValueCallBacks));
        CFDictionaryAddValue(attributes.get(),
                             kCTFontVariationAttribute, ctVariation.dict.get());
        SkUniqueCFRef<CTFontDescriptorRef> varDesc(
                CTFontDescriptorCreateWithAttributes(attributes.get()));
        ctVariant.reset(CTFontCreateCopyWithAttributes(fFontRef.get(), 0, nullptr, varDesc.get()));
    } else {
        ctVariant.reset((CTFontRef)CFRetain(fFontRef.get()));
    }
    if (!ctVariant) {
        return nullptr;
    }

    return SkTypeface_Mac::Make(std::move(ctVariant), ctVariation.opsz,
                                fStream ? fStream->duplicate() : nullptr);
}

int SkTypeface_Mac::onGetVariationDesignParameters(SkFontParameters::Variation::Axis parameters[],
                                                   int parameterCount) const
{
    SkUniqueCFRef<CFArrayRef> ctAxes(CTFontCopyVariationAxes(fFontRef.get()));
    if (!ctAxes) {
        return -1;
    }
    CFIndex axisCount = CFArrayGetCount(ctAxes.get());

    if (!parameters || parameterCount < axisCount) {
        return axisCount;
    }

    // Added in 10.13
    CFStringRef* kCTFontVariationAxisHiddenKeyPtr =
            static_cast<CFStringRef*>(dlsym(RTLD_DEFAULT, "kCTFontVariationAxisHiddenKey"));

    for (int i = 0; i < axisCount; ++i) {
        CFTypeRef axisInfo = CFArrayGetValueAtIndex(ctAxes.get(), i);
        if (CFDictionaryGetTypeID() != CFGetTypeID(axisInfo)) {
            return -1;
        }
        CFDictionaryRef axisInfoDict = static_cast<CFDictionaryRef>(axisInfo);

        CFTypeRef tag = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisIdentifierKey);
        if (!tag || CFGetTypeID(tag) != CFNumberGetTypeID()) {
            return -1;
        }
        CFNumberRef tagNumber = static_cast<CFNumberRef>(tag);
        int64_t tagLong;
        if (!CFNumberGetValue(tagNumber, kCFNumberSInt64Type, &tagLong)) {
            return -1;
        }

        CFTypeRef min = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisMinimumValueKey);
        CFTypeRef max = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisMaximumValueKey);
        CFTypeRef def = CFDictionaryGetValue(axisInfoDict, kCTFontVariationAxisDefaultValueKey);
        if (!min || CFGetTypeID(min) != CFNumberGetTypeID() ||
            !max || CFGetTypeID(max) != CFNumberGetTypeID() ||
            !def || CFGetTypeID(def) != CFNumberGetTypeID())
        {
            return -1;
        }
        CFNumberRef minNumber = static_cast<CFNumberRef>(min);
        CFNumberRef maxNumber = static_cast<CFNumberRef>(max);
        CFNumberRef defNumber = static_cast<CFNumberRef>(def);
        double minDouble;
        double maxDouble;
        double defDouble;
        if (!CFNumberGetValue(minNumber, kCFNumberDoubleType, &minDouble) ||
            !CFNumberGetValue(maxNumber, kCFNumberDoubleType, &maxDouble) ||
            !CFNumberGetValue(defNumber, kCFNumberDoubleType, &defDouble))
        {
            return -1;
        }

        SkFontParameters::Variation::Axis& skAxis = parameters[i];
        skAxis.tag = tagLong;
        skAxis.min = minDouble;
        skAxis.max = maxDouble;
        skAxis.def = defDouble;
        skAxis.setHidden(false);
        if (kCTFontVariationAxisHiddenKeyPtr) {
            CFTypeRef hidden = CFDictionaryGetValue(axisInfoDict,*kCTFontVariationAxisHiddenKeyPtr);
            if (hidden) {
                if (CFGetTypeID(hidden) != CFBooleanGetTypeID()) {
                    return -1;
                }
                CFBooleanRef hiddenBoolean = static_cast<CFBooleanRef>(hidden);
                skAxis.setHidden(CFBooleanGetValue(hiddenBoolean));
            }
        }
    }
    return axisCount;
}

#endif
