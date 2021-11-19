use_relative_paths = True

vars = {
  "checkout_chromium": False,

  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling different
  # dependencies without interference from each other.
  'sk_tool_revision': 'git_revision:af23468c7173e1f17987a6aa12c41bf6b1730c60',
}

deps = {
  "third_party/externals/d3d12allocator"  : "https://skia.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git@169895d529dfce00390a20e69c2f516066fe7a3b",
  "third_party/externals/expat"           : "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git@a28238bdeebc087071777001245df1876a11f5ee",
  "third_party/externals/freetype"        : "https://chromium.googlesource.com/chromium/src/third_party/freetype2.git@d3dc2da9b27af5b90575d62989389cc65fe7977c",
  "third_party/externals/harfbuzz"        : "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git@3a74ee528255cc027d84b204a87b5c25e47bff79",
  "third_party/externals/icu"             : "https://chromium.googlesource.com/chromium/deps/icu.git@a0718d4f121727e30b8d52c7a189ebf5ab52421f",
  "third_party/externals/libgifcodec"     : "https://skia.googlesource.com/libgifcodec@fd59fa92a0c86788dcdd84d091e1ce81eda06a77",
  "third_party/externals/libjpeg-turbo"   : "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git@24e310554f07c0fdb8ee52e3e708e4f3e9eb6e20",
  "third_party/externals/libpng"          : "https://skia.googlesource.com/third_party/libpng.git@386707c6d19b974ca2e3db7f5c61873813c6fe44",
  "third_party/externals/libwebp"         : "https://chromium.googlesource.com/webm/libwebp.git@fedac6cc69cda3e9e04b780d324cf03921fb3ff4",
  "third_party/externals/piex"            : "https://android.googlesource.com/platform/external/piex.git@bb217acdca1cc0c16b704669dd6f91a1b509c406",
  "third_party/externals/spirv-cross"     : "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Cross@bdbef7b1f3982fe99a62d076043036abe6dd6d80",
  #"third_party/externals/v8"              : "https://chromium.googlesource.com/v8/v8.git@5f1ae66d5634e43563b2d25ea652dfb94c31a3b4",
  "third_party/externals/zlib"            : "https://chromium.googlesource.com/chromium/src/third_party/zlib@c876c8f87101c5a75f6014b0f832499afeb65b73",

  "../src": {
    "url": "https://chromium.googlesource.com/chromium/src.git@17781a5cd82e61a304559e9adb7d5a9c78dbbd59",
    "condition": "checkout_chromium",
  },

  'bin': {
    'packages': [
      {
        'package': 'skia/tools/sk/${{platform}}',
        'version': Var('sk_tool_revision'),
      }
    ],
    'dep_type': 'cipd',
  },
}

recursedeps = [
  "../src",
]

gclient_gn_args_from = 'src'
