use_relative_paths = True

vars = {
  "checkout_chromium": False,
}

deps = {
  "third_party/externals/expat"           : "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git@e976867fb57a0cd87e3b0fe05d59e0ed63c6febb",
  "third_party/externals/freetype"        : "https://skia.googlesource.com/third_party/freetype2.git@0a3d2bb99b45b72e1d45185ab054efa993d97210",
  "third_party/externals/harfbuzz"        : "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git@3a74ee528255cc027d84b204a87b5c25e47bff79",
  "third_party/externals/icu"             : "https://chromium.googlesource.com/chromium/deps/icu.git@dbd3825b31041d782c5b504c59dcfb5ac7dda08c",
  "third_party/externals/libgifcodec"     : "https://skia.googlesource.com/libgifcodec@d06d2a6d42baf6c0c91cacc28df2542a911d05fe",
  "third_party/externals/libjpeg-turbo"   : "https://skia.googlesource.com/external/github.com/libjpeg-turbo/libjpeg-turbo.git@574f3a772c96dc9db2c98ef24706feb3f6dbda9a",
  "third_party/externals/libpng"          : "https://skia.googlesource.com/third_party/libpng.git@386707c6d19b974ca2e3db7f5c61873813c6fe44",
  "third_party/externals/libwebp"         : "https://chromium.googlesource.com/webm/libwebp.git@0fa56f307f813fa9e43c7a332fa3efc2fae410dd",
  "third_party/externals/spirv-cross"     : "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Cross@d385bf096f5dabbc4cdaeb6872b0f64be1a63ad0",
  #"third_party/externals/v8"              : "https://chromium.googlesource.com/v8/v8.git@5f1ae66d5634e43563b2d25ea652dfb94c31a3b4",
  "third_party/externals/zlib"            : "https://chromium.googlesource.com/chromium/src/third_party/zlib@eaf99a4e2009b0e5759e6070ad1760ac1dd75461",

  "../src": {
    "url": "https://chromium.googlesource.com/chromium/src.git@6d759c2e5e1f8b1239a87c5f23123dbbe42789d7",
    "condition": "checkout_chromium",
  },
}

recursedeps = [
  "common",
  "../src",
]

gclient_gn_args_from = 'src'
