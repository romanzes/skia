use_relative_paths = True

vars = {
  "checkout_chromium": False,
}

deps = {
  "third_party/externals/d3d12allocator"  : "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git@169895d529dfce00390a20e69c2f516066fe7a3b",
  "third_party/externals/expat"           : "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git@e976867fb57a0cd87e3b0fe05d59e0ed63c6febb",
  "third_party/externals/freetype"        : "https://skia.googlesource.com/third_party/freetype2.git@40c5681ab92e7db1298273ccf3c816e6a1498260",
  "third_party/externals/harfbuzz"        : "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git@3a74ee528255cc027d84b204a87b5c25e47bff79",
  "third_party/externals/icu"             : "https://chromium.googlesource.com/chromium/deps/icu.git@dbd3825b31041d782c5b504c59dcfb5ac7dda08c",
  "third_party/externals/libgifcodec"     : "https://skia.googlesource.com/libgifcodec@d06d2a6d42baf6c0c91cacc28df2542a911d05fe",
  "third_party/externals/libjpeg-turbo"   : "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git@64fc43d52351ed52143208ce6a656c03db56462b",
  "third_party/externals/libpng"          : "https://skia.googlesource.com/third_party/libpng.git@386707c6d19b974ca2e3db7f5c61873813c6fe44",
  "third_party/externals/libwebp"         : "https://chromium.googlesource.com/webm/libwebp.git@55a080e50af655d1fbe0a5c22954835cdd59ff92",
  "third_party/externals/piex"            : "https://android.googlesource.com/platform/external/piex.git@bb217acdca1cc0c16b704669dd6f91a1b509c406",
  "third_party/externals/spirv-cross"     : "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Cross@bdbef7b1f3982fe99a62d076043036abe6dd6d80",
  #"third_party/externals/v8"              : "https://chromium.googlesource.com/v8/v8.git@5f1ae66d5634e43563b2d25ea652dfb94c31a3b4",
  "third_party/externals/zlib"            : "https://chromium.googlesource.com/chromium/src/third_party/zlib@eaf99a4e2009b0e5759e6070ad1760ac1dd75461",
  "third_party/externals/brotli"          : "https://skia.googlesource.com/external/github.com/google/brotli.git@e61745a6b7add50d380cfd7d3883dd6c62fc2c71",

  "../src": {
    "url": "https://chromium.googlesource.com/chromium/src.git@2cd5dabd77439bd357fa6c3213a7fdb4fbbee030",
    "condition": "checkout_chromium",
  },
}

recursedeps = [
  "common",
  "../src",
]

gclient_gn_args_from = 'src'
