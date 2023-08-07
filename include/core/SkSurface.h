/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSurface_DEFINED
#define SkSurface_DEFINED

#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTypes.h"

#if defined(SK_GRAPHITE)
#include "include/gpu/GpuTypes.h"
namespace skgpu::graphite {
class BackendTexture;
}
#endif

#if defined(SK_BUILD_FOR_ANDROID) && __ANDROID_API__ >= 26
#include <android/hardware_buffer.h>
class GrDirectContext;
#endif

#if defined(SK_GANESH) && defined(SK_METAL)
#include "include/gpu/mtl/GrMtlTypes.h"
#endif

#include <cstddef>
#include <cstdint>
#include <memory>

class GrBackendRenderTarget;
class GrBackendSemaphore;
class GrBackendTexture;
class GrRecordingContext;
class SkBitmap;
class SkCanvas;
class SkCapabilities;
class SkColorSpace;
class SkDeferredDisplayList;
class SkPaint;
class SkSurface;
class SkSurfaceCharacterization;
enum GrSurfaceOrigin : int;
enum SkColorType : int;
enum class GrSemaphoresSubmitted : bool;
struct GrFlushInfo;
struct SkIRect;
struct SkISize;

namespace skgpu {
class MutableTextureState;
enum class Budgeted : bool;
}

namespace skgpu::graphite {
class Recorder;
}

namespace SkSurfaces {

/** Returns SkSurface without backing pixels. Drawing to SkCanvas returned from SkSurface
    has no effect. Calling makeImageSnapshot() on returned SkSurface returns nullptr.

    @param width   one or greater
    @param height  one or greater
    @return        SkSurface if width and height are positive; otherwise, nullptr

    example: https://fiddle.skia.org/c/@Surface_MakeNull
*/
SK_API sk_sp<SkSurface> Null(int width, int height);

/** Allocates raster SkSurface. SkCanvas returned by SkSurface draws directly into those allocated
    pixels, which are zeroed before use. Pixel memory size is imageInfo.height() times
    imageInfo.minRowBytes() or rowBytes, if provided and non-zero.

    Pixel memory is deleted when SkSurface is deleted.

    Validity constraints include:
      - info dimensions are greater than zero;
      - info contains SkColorType and SkAlphaType supported by raster surface.

    @param imageInfo  width, height, SkColorType, SkAlphaType, SkColorSpace,
                      of raster surface; width and height must be greater than zero
    @param rowBytes   interval from one SkSurface row to the next.
    @param props      LCD striping orientation and setting for device independent fonts;
                      may be nullptr
    @return           SkSurface if parameters are valid and memory was allocated, else nullptr.
*/
SK_API sk_sp<SkSurface> Raster(const SkImageInfo& imageInfo,
                               size_t rowBytes,
                               const SkSurfaceProps* surfaceProps);
inline sk_sp<SkSurface> Raster(const SkImageInfo& imageInfo,
                               const SkSurfaceProps* props = nullptr) {
    return Raster(imageInfo, 0, props);
}

/** Allocates raster SkSurface. SkCanvas returned by SkSurface draws directly into the
    provided pixels.

    SkSurface is returned if all parameters are valid.
    Valid parameters include:
    info dimensions are greater than zero;
    info contains SkColorType and SkAlphaType supported by raster surface;
    pixels is not nullptr;
    rowBytes is large enough to contain info width pixels of SkColorType.

    Pixel buffer size should be info height times computed rowBytes.
    Pixels are not initialized.
    To access pixels after drawing, peekPixels() or readPixels().

    @param imageInfo     width, height, SkColorType, SkAlphaType, SkColorSpace,
                         of raster surface; width and height must be greater than zero
    @param pixels        pointer to destination pixels buffer
    @param rowBytes      interval from one SkSurface row to the next
    @param surfaceProps  LCD striping orientation and setting for device independent fonts;
                         may be nullptr
    @return              SkSurface if all parameters are valid; otherwise, nullptr
*/

SK_API sk_sp<SkSurface> WrapPixels(const SkImageInfo& imageInfo,
                                   void* pixels,
                                   size_t rowBytes,
                                   const SkSurfaceProps* surfaceProps = nullptr);
inline sk_sp<SkSurface> WrapPixels(const SkPixmap& pm, const SkSurfaceProps* props = nullptr) {
    return WrapPixels(pm.info(), pm.writable_addr(), pm.rowBytes(), props);
}

using PixelsReleaseProc = void(void* pixels, void* context);

/** Allocates raster SkSurface. SkCanvas returned by SkSurface draws directly into the provided
    pixels. releaseProc is called with pixels and context when SkSurface is deleted.

    SkSurface is returned if all parameters are valid.
    Valid parameters include:
    info dimensions are greater than zero;
    info contains SkColorType and SkAlphaType supported by raster surface;
    pixels is not nullptr;
    rowBytes is large enough to contain info width pixels of SkColorType.

    Pixel buffer size should be info height times computed rowBytes.
    Pixels are not initialized.
    To access pixels after drawing, call flush() or peekPixels().

    @param imageInfo     width, height, SkColorType, SkAlphaType, SkColorSpace,
                         of raster surface; width and height must be greater than zero
    @param pixels        pointer to destination pixels buffer
    @param rowBytes      interval from one SkSurface row to the next
    @param releaseProc   called when SkSurface is deleted; may be nullptr
    @param context       passed to releaseProc; may be nullptr
    @param surfaceProps  LCD striping orientation and setting for device independent fonts;
                         may be nullptr
    @return              SkSurface if all parameters are valid; otherwise, nullptr
*/
SK_API sk_sp<SkSurface> WrapPixels(const SkImageInfo& imageInfo,
                                   void* pixels,
                                   size_t rowBytes,
                                   PixelsReleaseProc,
                                   void* context,
                                   const SkSurfaceProps* surfaceProps = nullptr);
}  // namespace SkSurfaces

/** \class SkSurface
    SkSurface is responsible for managing the pixels that a canvas draws into. The pixels can be
    allocated either in CPU memory (a raster surface) or on the GPU (a GrRenderTarget surface).
    SkSurface takes care of allocating a SkCanvas that will draw into the surface. Call
    surface->getCanvas() to use that canvas (but don't delete it, it is owned by the surface).
    SkSurface always has non-zero dimensions. If there is a request for a new surface, and either
    of the requested dimensions are zero, then nullptr will be returned.

    Clients should *not* subclass SkSurface as there is a lot of internal machinery that is
    not publicly accessible.
*/
class SK_API SkSurface : public SkRefCnt {
public:
    /** Is this surface compatible with the provided characterization?

        This method can be used to determine if an existing SkSurface is a viable destination
        for an SkDeferredDisplayList.

        @param characterization  The characterization for which a compatibility check is desired
        @return                  true if this surface is compatible with the characterization;
                                 false otherwise
    */
    bool isCompatible(const SkSurfaceCharacterization& characterization) const;

    /** Returns pixel count in each row; may be zero or greater.

        @return  number of pixel columns
    */
    int width() const { return fWidth; }

    /** Returns pixel row count; may be zero or greater.

        @return  number of pixel rows
    */
    int height() const { return fHeight; }

    /** Returns an ImageInfo describing the surface.
     */
    virtual SkImageInfo imageInfo() const { return SkImageInfo::MakeUnknown(fWidth, fHeight); }

    /** Returns unique value identifying the content of SkSurface. Returned value changes
        each time the content changes. Content is changed by drawing, or by calling
        notifyContentWillChange().

        @return  unique content identifier

        example: https://fiddle.skia.org/c/@Surface_notifyContentWillChange
    */
    uint32_t generationID();

    /** \enum SkSurface::ContentChangeMode
        ContentChangeMode members are parameters to notifyContentWillChange().
    */
    enum ContentChangeMode {
        kDiscard_ContentChangeMode, //!< discards surface on change
        kRetain_ContentChangeMode,  //!< preserves surface on change
    };

    /** Notifies that SkSurface contents will be changed by code outside of Skia.
        Subsequent calls to generationID() return a different value.

        TODO: Can kRetain_ContentChangeMode be deprecated?

        example: https://fiddle.skia.org/c/@Surface_notifyContentWillChange
    */
    void notifyContentWillChange(ContentChangeMode mode);

    /** Returns the recording context being used by the SkSurface.

        @return the recording context, if available; nullptr otherwise
     */
    GrRecordingContext* recordingContext() const;

    /** Returns the recorder being used by the SkSurface.

        @return the recorder, if available; nullptr otherwise
     */
    skgpu::graphite::Recorder* recorder() const;

    enum class BackendHandleAccess {
        kFlushRead,     //!< back-end object is readable
        kFlushWrite,    //!< back-end object is writable
        kDiscardWrite,  //!< back-end object must be overwritten

        // Legacy names, remove when clients are migrated
        kFlushRead_BackendHandleAccess = kFlushRead,
        kFlushWrite_BackendHandleAccess = kFlushWrite,
        kDiscardWrite_BackendHandleAccess = kDiscardWrite,
    };

    // Legacy names, remove when clients are migrated
    static constexpr BackendHandleAccess kFlushRead_BackendHandleAccess =
            BackendHandleAccess::kFlushRead;
    static constexpr BackendHandleAccess kFlushWrite_BackendHandleAccess =
            BackendHandleAccess::kFlushWrite;
    static constexpr BackendHandleAccess kDiscardWrite_BackendHandleAccess =
            BackendHandleAccess::kDiscardWrite;

    /** Caller data passed to TextureReleaseProc; may be nullptr. */
    using ReleaseContext = void*;
    /** User function called when supplied texture may be deleted. */
    using TextureReleaseProc = void (*)(ReleaseContext);

    /** If the surface was made via MakeFromBackendTexture then it's backing texture may be
        substituted with a different texture. The contents of the previous backing texture are
        copied into the new texture. SkCanvas state is preserved. The original sample count is
        used. The GrBackendFormat and dimensions of replacement texture must match that of
        the original.

        Upon success textureReleaseProc is called when it is safe to delete the texture in the
        backend API (accounting only for use of the texture by this surface). If SkSurface creation
        fails textureReleaseProc is called before this function returns.

        @param backendTexture      the new backing texture for the surface
        @param mode                Retain or discard current Content
        @param TextureReleaseProc  function called when texture can be released
        @param ReleaseContext      state passed to textureReleaseProc
     */
    virtual bool replaceBackendTexture(const GrBackendTexture& backendTexture,
                                       GrSurfaceOrigin origin,
                                       ContentChangeMode mode = kRetain_ContentChangeMode,
                                       TextureReleaseProc = nullptr,
                                       ReleaseContext = nullptr) = 0;

    /** Returns SkCanvas that draws into SkSurface. Subsequent calls return the same SkCanvas.
        SkCanvas returned is managed and owned by SkSurface, and is deleted when SkSurface
        is deleted.

        @return  drawing SkCanvas for SkSurface

        example: https://fiddle.skia.org/c/@Surface_getCanvas
    */
    SkCanvas* getCanvas();

    /** Returns SkCapabilities that describes the capabilities of the SkSurface's device.

        @return  SkCapabilities of SkSurface's device.
    */
    sk_sp<const SkCapabilities> capabilities();

    /** Returns a compatible SkSurface, or nullptr. Returned SkSurface contains
        the same raster, GPU, or null properties as the original. Returned SkSurface
        does not share the same pixels.

        Returns nullptr if imageInfo width or height are zero, or if imageInfo
        is incompatible with SkSurface.

        @param imageInfo  width, height, SkColorType, SkAlphaType, SkColorSpace,
                          of SkSurface; width and height must be greater than zero
        @return           compatible SkSurface or nullptr

        example: https://fiddle.skia.org/c/@Surface_makeSurface
    */
    sk_sp<SkSurface> makeSurface(const SkImageInfo& imageInfo);

    /** Calls makeSurface(ImageInfo) with the same ImageInfo as this surface, but with the
     *  specified width and height.
     */
    sk_sp<SkSurface> makeSurface(int width, int height);

    /** Returns SkImage capturing SkSurface contents. Subsequent drawing to SkSurface contents
        are not captured. SkImage allocation is accounted for if SkSurface was created with
        skgpu::Budgeted::kYes.

        @return  SkImage initialized with SkSurface contents

        example: https://fiddle.skia.org/c/@Surface_makeImageSnapshot
    */
    sk_sp<SkImage> makeImageSnapshot();

    /**
     *  Like the no-parameter version, this returns an image of the current surface contents.
     *  This variant takes a rectangle specifying the subset of the surface that is of interest.
     *  These bounds will be sanitized before being used.
     *  - If bounds extends beyond the surface, it will be trimmed to just the intersection of
     *    it and the surface.
     *  - If bounds does not intersect the surface, then this returns nullptr.
     *  - If bounds == the surface, then this is the same as calling the no-parameter variant.

        example: https://fiddle.skia.org/c/@Surface_makeImageSnapshot_2
     */
    sk_sp<SkImage> makeImageSnapshot(const SkIRect& bounds);

#if defined(SK_GRAPHITE)
    /**
     * The 'asImage' and 'makeImageCopy' API/entry points are currently only available for
     * Graphite.
     *
     * In this API, SkSurface no longer supports copy-on-write behavior. Instead, when creating
     * an image for a surface, the client must explicitly indicate if a copy should be made.
     * In both of the below calls the resource backing the surface will never change.
     *
     * The 'asImage' entry point has some major ramifications for the mutability of the
     * returned SkImage. Since the originating surface and the returned image share the
     * same backing, care must be taken by the client to ensure that the contents of the image
     * reflect the desired contents when it is consumed by the gpu.
     * Note: if the backing GPU buffer isn't textureable this method will return null. Graphite
     * will not attempt to make a copy.
     * Note: For 'asImage', the mipmapping of the image will match that of the source surface.
     *
     * The 'makeImageCopy' entry point allows subsetting and the addition of mipmaps (since
     * a copy is already being made).
     *
     * In Graphite, the legacy API call (i.e., makeImageSnapshot) will just always make a copy.
     */
    sk_sp<SkImage> asImage();

    sk_sp<SkImage> makeImageCopy(const SkIRect* subset = nullptr,
                                 skgpu::Mipmapped mipmapped = skgpu::Mipmapped::kNo);
#endif

    /** Draws SkSurface contents to canvas, with its top-left corner at (x, y).

        If SkPaint paint is not nullptr, apply SkColorFilter, alpha, SkImageFilter, and SkBlendMode.

        @param canvas  SkCanvas drawn into
        @param x       horizontal offset in SkCanvas
        @param y       vertical offset in SkCanvas
        @param sampling what technique to use when sampling the surface pixels
        @param paint   SkPaint containing SkBlendMode, SkColorFilter, SkImageFilter,
                       and so on; or nullptr

        example: https://fiddle.skia.org/c/@Surface_draw
    */
    void draw(SkCanvas* canvas, SkScalar x, SkScalar y, const SkSamplingOptions& sampling,
              const SkPaint* paint);

    void draw(SkCanvas* canvas, SkScalar x, SkScalar y, const SkPaint* paint = nullptr) {
        this->draw(canvas, x, y, SkSamplingOptions(), paint);
    }

    /** Copies SkSurface pixel address, row bytes, and SkImageInfo to SkPixmap, if address
        is available, and returns true. If pixel address is not available, return
        false and leave SkPixmap unchanged.

        pixmap contents become invalid on any future change to SkSurface.

        @param pixmap  storage for pixel state if pixels are readable; otherwise, ignored
        @return        true if SkSurface has direct access to pixels

        example: https://fiddle.skia.org/c/@Surface_peekPixels
    */
    bool peekPixels(SkPixmap* pixmap);

    /** Copies SkRect of pixels to dst.

        Source SkRect corners are (srcX, srcY) and SkSurface (width(), height()).
        Destination SkRect corners are (0, 0) and (dst.width(), dst.height()).
        Copies each readable pixel intersecting both rectangles, without scaling,
        converting to dst.colorType() and dst.alphaType() if required.

        Pixels are readable when SkSurface is raster, or backed by a GPU.

        The destination pixel storage must be allocated by the caller.

        Pixel values are converted only if SkColorType and SkAlphaType
        do not match. Only pixels within both source and destination rectangles
        are copied. dst contents outside SkRect intersection are unchanged.

        Pass negative values for srcX or srcY to offset pixels across or down destination.

        Does not copy, and returns false if:
        - Source and destination rectangles do not intersect.
        - SkPixmap pixels could not be allocated.
        - dst.rowBytes() is too small to contain one row of pixels.

        @param dst   storage for pixels copied from SkSurface
        @param srcX  offset into readable pixels on x-axis; may be negative
        @param srcY  offset into readable pixels on y-axis; may be negative
        @return      true if pixels were copied

        example: https://fiddle.skia.org/c/@Surface_readPixels
    */
    bool readPixels(const SkPixmap& dst, int srcX, int srcY);

    /** Copies SkRect of pixels from SkCanvas into dstPixels.

        Source SkRect corners are (srcX, srcY) and SkSurface (width(), height()).
        Destination SkRect corners are (0, 0) and (dstInfo.width(), dstInfo.height()).
        Copies each readable pixel intersecting both rectangles, without scaling,
        converting to dstInfo.colorType() and dstInfo.alphaType() if required.

        Pixels are readable when SkSurface is raster, or backed by a GPU.

        The destination pixel storage must be allocated by the caller.

        Pixel values are converted only if SkColorType and SkAlphaType
        do not match. Only pixels within both source and destination rectangles
        are copied. dstPixels contents outside SkRect intersection are unchanged.

        Pass negative values for srcX or srcY to offset pixels across or down destination.

        Does not copy, and returns false if:
        - Source and destination rectangles do not intersect.
        - SkSurface pixels could not be converted to dstInfo.colorType() or dstInfo.alphaType().
        - dstRowBytes is too small to contain one row of pixels.

        @param dstInfo      width, height, SkColorType, and SkAlphaType of dstPixels
        @param dstPixels    storage for pixels; dstInfo.height() times dstRowBytes, or larger
        @param dstRowBytes  size of one destination row; dstInfo.width() times pixel size, or larger
        @param srcX         offset into readable pixels on x-axis; may be negative
        @param srcY         offset into readable pixels on y-axis; may be negative
        @return             true if pixels were copied
    */
    bool readPixels(const SkImageInfo& dstInfo, void* dstPixels, size_t dstRowBytes,
                    int srcX, int srcY);

    /** Copies SkRect of pixels from SkSurface into bitmap.

        Source SkRect corners are (srcX, srcY) and SkSurface (width(), height()).
        Destination SkRect corners are (0, 0) and (bitmap.width(), bitmap.height()).
        Copies each readable pixel intersecting both rectangles, without scaling,
        converting to bitmap.colorType() and bitmap.alphaType() if required.

        Pixels are readable when SkSurface is raster, or backed by a GPU.

        The destination pixel storage must be allocated by the caller.

        Pixel values are converted only if SkColorType and SkAlphaType
        do not match. Only pixels within both source and destination rectangles
        are copied. dst contents outside SkRect intersection are unchanged.

        Pass negative values for srcX or srcY to offset pixels across or down destination.

        Does not copy, and returns false if:
        - Source and destination rectangles do not intersect.
        - SkSurface pixels could not be converted to dst.colorType() or dst.alphaType().
        - dst pixels could not be allocated.
        - dst.rowBytes() is too small to contain one row of pixels.

        @param dst   storage for pixels copied from SkSurface
        @param srcX  offset into readable pixels on x-axis; may be negative
        @param srcY  offset into readable pixels on y-axis; may be negative
        @return      true if pixels were copied

        example: https://fiddle.skia.org/c/@Surface_readPixels_3
    */
    bool readPixels(const SkBitmap& dst, int srcX, int srcY);

    using AsyncReadResult = SkImage::AsyncReadResult;

    /** Client-provided context that is passed to client-provided ReadPixelsContext. */
    using ReadPixelsContext = void*;

    /**  Client-provided callback to asyncRescaleAndReadPixels() or
         asyncRescaleAndReadPixelsYUV420() that is called when read result is ready or on failure.
     */
    using ReadPixelsCallback = void(ReadPixelsContext, std::unique_ptr<const AsyncReadResult>);

    /** Controls the gamma that rescaling occurs in for asyncRescaleAndReadPixels() and
        asyncRescaleAndReadPixelsYUV420().
     */
    using RescaleGamma = SkImage::RescaleGamma;
    using RescaleMode  = SkImage::RescaleMode;

    /** Makes surface pixel data available to caller, possibly asynchronously. It can also rescale
        the surface pixels.

        Currently asynchronous reads are only supported on the GPU backend and only when the
        underlying 3D API supports transfer buffers and CPU/GPU synchronization primitives. In all
        other cases this operates synchronously.

        Data is read from the source sub-rectangle, is optionally converted to a linear gamma, is
        rescaled to the size indicated by 'info', is then converted to the color space, color type,
        and alpha type of 'info'. A 'srcRect' that is not contained by the bounds of the surface
        causes failure.

        When the pixel data is ready the caller's ReadPixelsCallback is called with a
        AsyncReadResult containing pixel data in the requested color type, alpha type, and color
        space. The AsyncReadResult will have count() == 1. Upon failure the callback is called
        with nullptr for AsyncReadResult. For a GPU surface this flushes work but a submit must
        occur to guarantee a finite time before the callback is called.

        The data is valid for the lifetime of AsyncReadResult with the exception that if the
        SkSurface is GPU-backed the data is immediately invalidated if the context is abandoned
        or destroyed.

        @param info            info of the requested pixels
        @param srcRect         subrectangle of surface to read
        @param rescaleGamma    controls whether rescaling is done in the surface's gamma or whether
                               the source data is transformed to a linear gamma before rescaling.
        @param rescaleMode     controls the technique of the rescaling
        @param callback        function to call with result of the read
        @param context         passed to callback
     */
    void asyncRescaleAndReadPixels(const SkImageInfo& info,
                                   const SkIRect& srcRect,
                                   RescaleGamma rescaleGamma,
                                   RescaleMode rescaleMode,
                                   ReadPixelsCallback callback,
                                   ReadPixelsContext context);

    /**
        Similar to asyncRescaleAndReadPixels but performs an additional conversion to YUV. The
        RGB->YUV conversion is controlled by 'yuvColorSpace'. The YUV data is returned as three
        planes ordered y, u, v. The u and v planes are half the width and height of the resized
        rectangle. The y, u, and v values are single bytes. Currently this fails if 'dstSize'
        width and height are not even. A 'srcRect' that is not contained by the bounds of the
        surface causes failure.

        When the pixel data is ready the caller's ReadPixelsCallback is called with a
        AsyncReadResult containing the planar data. The AsyncReadResult will have count() == 3.
        Upon failure the callback is called with nullptr for AsyncReadResult. For a GPU surface this
        flushes work but a submit must occur to guarantee a finite time before the callback is
        called.

        The data is valid for the lifetime of AsyncReadResult with the exception that if the
        SkSurface is GPU-backed the data is immediately invalidated if the context is abandoned
        or destroyed.

        @param yuvColorSpace  The transformation from RGB to YUV. Applied to the resized image
                              after it is converted to dstColorSpace.
        @param dstColorSpace  The color space to convert the resized image to, after rescaling.
        @param srcRect        The portion of the surface to rescale and convert to YUV planes.
        @param dstSize        The size to rescale srcRect to
        @param rescaleGamma   controls whether rescaling is done in the surface's gamma or whether
                              the source data is transformed to a linear gamma before rescaling.
        @param rescaleMode    controls the sampling technique of the rescaling
        @param callback       function to call with the planar read result
        @param context        passed to callback
     */
    void asyncRescaleAndReadPixelsYUV420(SkYUVColorSpace yuvColorSpace,
                                         sk_sp<SkColorSpace> dstColorSpace,
                                         const SkIRect& srcRect,
                                         const SkISize& dstSize,
                                         RescaleGamma rescaleGamma,
                                         RescaleMode rescaleMode,
                                         ReadPixelsCallback callback,
                                         ReadPixelsContext context);

    /** Copies SkRect of pixels from the src SkPixmap to the SkSurface.

        Source SkRect corners are (0, 0) and (src.width(), src.height()).
        Destination SkRect corners are (dstX, dstY) and
        (dstX + Surface width(), dstY + Surface height()).

        Copies each readable pixel intersecting both rectangles, without scaling,
        converting to SkSurface colorType() and SkSurface alphaType() if required.

        @param src   storage for pixels to copy to SkSurface
        @param dstX  x-axis position relative to SkSurface to begin copy; may be negative
        @param dstY  y-axis position relative to SkSurface to begin copy; may be negative

        example: https://fiddle.skia.org/c/@Surface_writePixels
    */
    void writePixels(const SkPixmap& src, int dstX, int dstY);

    /** Copies SkRect of pixels from the src SkBitmap to the SkSurface.

        Source SkRect corners are (0, 0) and (src.width(), src.height()).
        Destination SkRect corners are (dstX, dstY) and
        (dstX + Surface width(), dstY + Surface height()).

        Copies each readable pixel intersecting both rectangles, without scaling,
        converting to SkSurface colorType() and SkSurface alphaType() if required.

        @param src   storage for pixels to copy to SkSurface
        @param dstX  x-axis position relative to SkSurface to begin copy; may be negative
        @param dstY  y-axis position relative to SkSurface to begin copy; may be negative

        example: https://fiddle.skia.org/c/@Surface_writePixels_2
    */
    void writePixels(const SkBitmap& src, int dstX, int dstY);

    /** Returns SkSurfaceProps for surface.

        @return  LCD striping orientation and setting for device independent fonts
    */
    const SkSurfaceProps& props() const { return fProps; }

    /** Call to ensure all reads/writes of the surface have been issued to the underlying 3D API.
        Skia will correctly order its own draws and pixel operations. This must to be used to ensure
        correct ordering when the surface backing store is accessed outside Skia (e.g. direct use of
        the 3D API or a windowing system). GrDirectContext has additional flush and submit methods
        that apply to all surfaces and images created from a GrDirectContext. This is equivalent to
        calling SkSurface::flush with a default GrFlushInfo followed by
        GrDirectContext::submit(syncCpu).
    */
    void flushAndSubmit(bool syncCpu = false);

    enum class BackendSurfaceAccess {
        kNoAccess,  //!< back-end object will not be used by client
        kPresent,   //!< back-end surface will be used for presenting to screen
    };

#if defined(SK_GANESH)
    /** If a surface is GPU texture backed, is being drawn with MSAA, and there is a resolve
        texture, this call will insert a resolve command into the stream of gpu commands. In order
        for the resolve to actually have an effect, the work still needs to be flushed and submitted
        to the GPU after recording the resolve command. If a resolve is not supported or the
        SkSurface has no dirty work to resolve, then this call is a no-op.

        This call is most useful when the SkSurface is created by wrapping a single sampled gpu
        texture, but asking Skia to render with MSAA. If the client wants to use the wrapped texture
        outside of Skia, the only way to trigger a resolve is either to call this command or use
        SkSurface::flush.
     */
    void resolveMSAA();

    /** Issues pending SkSurface commands to the GPU-backed API objects and resolves any SkSurface
        MSAA. A call to GrDirectContext::submit is always required to ensure work is actually sent
        to the gpu. Some specific API details:
            GL: Commands are actually sent to the driver, but glFlush is never called. Thus some
                sync objects from the flush will not be valid until a submission occurs.

            Vulkan/Metal/D3D/Dawn: Commands are recorded to the backend APIs corresponding command
                buffer or encoder objects. However, these objects are not sent to the gpu until a
                submission occurs.

        The work that is submitted to the GPU will be dependent on the BackendSurfaceAccess that is
        passed in.

        If BackendSurfaceAccess::kNoAccess is passed in all commands will be issued to the GPU.

        If BackendSurfaceAccess::kPresent is passed in and the backend API is not Vulkan, it is
        treated the same as kNoAccess. If the backend API is Vulkan, the VkImage that backs the
        SkSurface will be transferred back to its original queue. If the SkSurface was created by
        wrapping a VkImage, the queue will be set to the queue which was originally passed in on
        the GrVkImageInfo. Additionally, if the original queue was not external or foreign the
        layout of the VkImage will be set to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.

        The GrFlushInfo describes additional options to flush. Please see documentation at
        GrFlushInfo for more info.

        If the return is GrSemaphoresSubmitted::kYes, only initialized GrBackendSemaphores will be
        submitted to the gpu during the next submit call (it is possible Skia failed to create a
        subset of the semaphores). The client should not wait on these semaphores until after submit
        has been called, but must keep them alive until then. If a submit flag was passed in with
        the flush these valid semaphores can we waited on immediately. If this call returns
        GrSemaphoresSubmitted::kNo, the GPU backend will not submit any semaphores to be signaled on
        the GPU. Thus the client should not have the GPU wait on any of the semaphores passed in
        with the GrFlushInfo. Regardless of whether semaphores were submitted to the GPU or not, the
        client is still responsible for deleting any initialized semaphores.
        Regardless of semaphore submission the context will still be flushed. It should be
        emphasized that a return value of GrSemaphoresSubmitted::kNo does not mean the flush did not
        happen. It simply means there were no semaphores submitted to the GPU. A caller should only
        take this as a failure if they passed in semaphores to be submitted.

        Pending surface commands are flushed regardless of the return result.

        @param access  type of access the call will do on the backend object after flush
        @param info    flush options
    */
    GrSemaphoresSubmitted flush(BackendSurfaceAccess access, const GrFlushInfo& info);

    /** Issues pending SkSurface commands to the GPU-backed API objects and resolves any SkSurface
        MSAA. A call to GrDirectContext::submit is always required to ensure work is actually sent
        to the gpu. Some specific API details:
            GL: Commands are actually sent to the driver, but glFlush is never called. Thus some
                sync objects from the flush will not be valid until a submission occurs.

            Vulkan/Metal/D3D/Dawn: Commands are recorded to the backend APIs corresponding command
                buffer or encoder objects. However, these objects are not sent to the gpu until a
                submission occurs.

        The GrFlushInfo describes additional options to flush. Please see documentation at
        GrFlushInfo for more info.

        If a skgpu::MutableTextureState is passed in, at the end of the flush we will transition
        the surface to be in the state requested by the skgpu::MutableTextureState. If the surface
        (or SkImage or GrBackendSurface wrapping the same backend object) is used again after this
        flush the state may be changed and no longer match what is requested here. This is often
        used if the surface will be used for presenting or external use and the client wants backend
        object to be prepped for that use. A finishedProc or semaphore on the GrFlushInfo will also
        include the work for any requested state change.

        If the backend API is Vulkan, the caller can set the skgpu::MutableTextureState's
        VkImageLayout to VK_IMAGE_LAYOUT_UNDEFINED or queueFamilyIndex to VK_QUEUE_FAMILY_IGNORED to
        tell Skia to not change those respective states.

        If the return is GrSemaphoresSubmitted::kYes, only initialized GrBackendSemaphores will be
        submitted to the gpu during the next submit call (it is possible Skia failed to create a
        subset of the semaphores). The client should not wait on these semaphores until after submit
        has been called, but must keep them alive until then. If a submit flag was passed in with
        the flush these valid semaphores can we waited on immediately. If this call returns
        GrSemaphoresSubmitted::kNo, the GPU backend will not submit any semaphores to be signaled on
        the GPU. Thus the client should not have the GPU wait on any of the semaphores passed in
        with the GrFlushInfo. Regardless of whether semaphores were submitted to the GPU or not, the
        client is still responsible for deleting any initialized semaphores.
        Regardleess of semaphore submission the context will still be flushed. It should be
        emphasized that a return value of GrSemaphoresSubmitted::kNo does not mean the flush did not
        happen. It simply means there were no semaphores submitted to the GPU. A caller should only
        take this as a failure if they passed in semaphores to be submitted.

        Pending surface commands are flushed regardless of the return result.

        @param info    flush options
        @param access  optional state change request after flush
    */
    GrSemaphoresSubmitted flush(const GrFlushInfo& info,
                                const skgpu::MutableTextureState* newState = nullptr);
#endif // defined(SK_GANESH)

    void flush();

    /** Inserts a list of GPU semaphores that the current GPU-backed API must wait on before
        executing any more commands on the GPU for this surface. If this call returns false, then
        the GPU back-end will not wait on any passed in semaphores, and the client will still own
        the semaphores, regardless of the value of deleteSemaphoresAfterWait.

        If deleteSemaphoresAfterWait is false then Skia will not delete the semaphores. In this case
        it is the client's responsibility to not destroy or attempt to reuse the semaphores until it
        knows that Skia has finished waiting on them. This can be done by using finishedProcs
        on flush calls.

        @param numSemaphores               size of waitSemaphores array
        @param waitSemaphores              array of semaphore containers
        @paramm deleteSemaphoresAfterWait  who owns and should delete the semaphores
        @return                            true if GPU is waiting on semaphores
    */
    bool wait(int numSemaphores, const GrBackendSemaphore* waitSemaphores,
              bool deleteSemaphoresAfterWait = true);

    /** Initializes SkSurfaceCharacterization that can be used to perform GPU back-end
        processing in a separate thread. Typically this is used to divide drawing
        into multiple tiles. SkDeferredDisplayListRecorder records the drawing commands
        for each tile.

        Return true if SkSurface supports characterization. raster surface returns false.

        @param characterization  properties for parallel drawing
        @return                  true if supported

        example: https://fiddle.skia.org/c/@Surface_characterize
    */
    bool characterize(SkSurfaceCharacterization* characterization) const;

    /** Draws the deferred display list created via a SkDeferredDisplayListRecorder.
        If the deferred display list is not compatible with this SkSurface, the draw is skipped
        and false is return.

        The xOffset and yOffset parameters are experimental and, if not both zero, will cause
        the draw to be ignored.
        When implemented, if xOffset or yOffset are non-zero, the DDL will be drawn offset by that
        amount into the surface.

        @param deferredDisplayList  drawing commands
        @param xOffset              x-offset at which to draw the DDL
        @param yOffset              y-offset at which to draw the DDL
        @return                     false if deferredDisplayList is not compatible

        example: https://fiddle.skia.org/c/@Surface_draw_2
    */
    bool draw(sk_sp<const SkDeferredDisplayList> deferredDisplayList,
              int xOffset = 0,
              int yOffset = 0);

protected:
    SkSurface(int width, int height, const SkSurfaceProps* surfaceProps);
    SkSurface(const SkImageInfo& imageInfo, const SkSurfaceProps* surfaceProps);

    // called by subclass if their contents have changed
    void dirtyGenerationID() {
        fGenerationID = 0;
    }

private:
    const SkSurfaceProps fProps;
    const int            fWidth;
    const int            fHeight;
    uint32_t             fGenerationID;

    using INHERITED = SkRefCnt;

public:
#if !defined(SK_DISABLE_LEGACY_SKSURFACE_METHODS) && defined(SK_GANESH)
    GrBackendTexture getBackendTexture(BackendHandleAccess backendHandleAccess);
    GrBackendRenderTarget getBackendRenderTarget(BackendHandleAccess backendHandleAccess);
#endif

#if !defined(SK_DISABLE_LEGACY_SKSURFACE_FACTORIES)
    using RenderTargetReleaseProc = void (*)(ReleaseContext);

    static sk_sp<SkSurface> MakeNull(int width, int height);
    static sk_sp<SkSurface> MakeRasterDirect(const SkImageInfo& imageInfo,
                                             void* pixels,
                                             size_t rowBytes,
                                             const SkSurfaceProps* surfaceProps = nullptr);
    static sk_sp<SkSurface> MakeRasterDirect(const SkPixmap& pm,
                                             const SkSurfaceProps* props = nullptr);
    static sk_sp<SkSurface> MakeRasterDirectReleaseProc(
            const SkImageInfo& imageInfo,
            void* pixels,
            size_t rowBytes,
            void (*releaseProc)(void* pixels, void* context),
            void* context,
            const SkSurfaceProps* surfaceProps = nullptr);
    static sk_sp<SkSurface> MakeRaster(const SkImageInfo& imageInfo,
                                       size_t rowBytes,
                                       const SkSurfaceProps* surfaceProps);
    static sk_sp<SkSurface> MakeRaster(const SkImageInfo& imageInfo,
                                       const SkSurfaceProps* props = nullptr);
    static sk_sp<SkSurface> MakeRasterN32Premul(int width,
                                                int height,
                                                const SkSurfaceProps* surfaceProps = nullptr);

#if defined(SK_GANESH)
    static sk_sp<SkSurface> MakeFromBackendTexture(GrRecordingContext* context,
                                                   const GrBackendTexture& backendTexture,
                                                   GrSurfaceOrigin origin,
                                                   int sampleCnt,
                                                   SkColorType colorType,
                                                   sk_sp<SkColorSpace> colorSpace,
                                                   const SkSurfaceProps* surfaceProps,
                                                   TextureReleaseProc textureReleaseProc = nullptr,
                                                   ReleaseContext releaseContext = nullptr);
    static sk_sp<SkSurface> MakeFromBackendRenderTarget(
            GrRecordingContext* context,
            const GrBackendRenderTarget& backendRenderTarget,
            GrSurfaceOrigin origin,
            SkColorType colorType,
            sk_sp<SkColorSpace> colorSpace,
            const SkSurfaceProps* surfaceProps,
            RenderTargetReleaseProc releaseProc = nullptr,
            ReleaseContext releaseContext = nullptr);
    static sk_sp<SkSurface> MakeRenderTarget(GrRecordingContext* context,
                                             skgpu::Budgeted budgeted,
                                             const SkImageInfo& imageInfo,
                                             int sampleCount,
                                             GrSurfaceOrigin surfaceOrigin,
                                             const SkSurfaceProps* surfaceProps,
                                             bool shouldCreateWithMips = false);
    static sk_sp<SkSurface> MakeRenderTarget(GrRecordingContext* context,
                                             skgpu::Budgeted budgeted,
                                             const SkImageInfo& imageInfo,
                                             int sampleCount,
                                             const SkSurfaceProps* surfaceProps);
    static sk_sp<SkSurface> MakeRenderTarget(GrRecordingContext* context,
                                             skgpu::Budgeted budgeted,
                                             const SkImageInfo& imageInfo);
    static sk_sp<SkSurface> MakeRenderTarget(GrRecordingContext* context,
                                             const SkSurfaceCharacterization& characterization,
                                             skgpu::Budgeted budgeted);
#endif  // defined(SK_GANESH)

#if defined(SK_BUILD_FOR_ANDROID) && __ANDROID_API__ >= 26
    static sk_sp<SkSurface> MakeFromAHardwareBuffer(GrDirectContext* context,
                                                    AHardwareBuffer* hardwareBuffer,
                                                    GrSurfaceOrigin origin,
                                                    sk_sp<SkColorSpace> colorSpace,
                                                    const SkSurfaceProps* surfaceProps,
                                                    bool fromWindow = false);
#endif

#if defined(SK_GRAPHITE)
    static sk_sp<SkSurface> MakeGraphite(skgpu::graphite::Recorder*,
                                         const SkImageInfo& imageInfo,
                                         skgpu::Mipmapped = skgpu::Mipmapped::kNo,
                                         const SkSurfaceProps* surfaceProps = nullptr);
    static sk_sp<SkSurface> MakeGraphiteFromBackendTexture(skgpu::graphite::Recorder*,
                                                           const skgpu::graphite::BackendTexture&,
                                                           SkColorType colorType,
                                                           sk_sp<SkColorSpace> colorSpace,
                                                           const SkSurfaceProps* props);
#endif  // defined(SK_GRAPHITE)

#if defined(SK_GANESH) && defined(SK_METAL)
    static sk_sp<SkSurface> MakeFromCAMetalLayer(GrRecordingContext* context,
                                                 GrMTLHandle layer,
                                                 GrSurfaceOrigin origin,
                                                 int sampleCnt,
                                                 SkColorType colorType,
                                                 sk_sp<SkColorSpace> colorSpace,
                                                 const SkSurfaceProps* surfaceProps,
                                                 GrMTLHandle* drawable)
            SK_API_AVAILABLE_CA_METAL_LAYER;
    static sk_sp<SkSurface> MakeFromMTKView(GrRecordingContext* context,
                                            GrMTLHandle mtkView,
                                            GrSurfaceOrigin origin,
                                            int sampleCnt,
                                            SkColorType colorType,
                                            sk_sp<SkColorSpace> colorSpace,
                                            const SkSurfaceProps* surfaceProps)
            SK_API_AVAILABLE(macos(10.11), ios(9.0));
#endif  // defined(SK_GANESH) && defined(SK_METAL)

#endif  // !defined(SK_DISABLE_LEGACY_SKSURFACE_FACTORIES)
};

#endif
