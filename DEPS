use_relative_paths = True

vars = {
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling different
  # dependencies without interference from each other.
  'sk_tool_revision': 'git_revision:8f39f2978ef8a5fe2a3745b6709f717aaada2ce4',

  # ninja CIPD package version.
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:2@1.8.2.chromium.3',
}

# If you modify this file, you will need to regenerate the Bazel version of this file (bazel/deps.bzl).
# To do so, run:
#     bazelisk run //bazel/deps_parser
#
# To apply the changes for the GN build, you will need to resync the git repositories using:
#     ./tools/git-sync-deps
deps = {
  "third_party/externals/brotli"                 : "https://skia.googlesource.com/external/github.com/google/brotli.git@6d03dfbedda1615c4cba1211f8d81735575209c8",
  "third_party/externals/d3d12allocator"         : "https://skia.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git@169895d529dfce00390a20e69c2f516066fe7a3b",
  "third_party/externals/expat"                  : "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git@441f98d02deafd9b090aea568282b28f66a50e36",
  "third_party/externals/freetype"               : "https://chromium.googlesource.com/chromium/src/third_party/freetype2.git@d857bd535b6c7e877f262a9b61ed21ee11b35dab",
  "third_party/externals/harfbuzz"               : "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git@09a266236147497bd8149240062c31c16fbc81e3",
  "third_party/externals/icu"                    : "https://chromium.googlesource.com/chromium/deps/icu.git@a0718d4f121727e30b8d52c7a189ebf5ab52421f",
  "third_party/externals/libjpeg-turbo"          : "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git@ed683925e4897a84b3bffc5c1414c85b97a129a3",
  "third_party/externals/libpng"                 : "https://skia.googlesource.com/third_party/libpng.git@386707c6d19b974ca2e3db7f5c61873813c6fe44",
  "third_party/externals/libwebp"                : "https://chromium.googlesource.com/webm/libwebp.git@fd7b5d48464475408d32d2611bdb6947d4246b97",
  "third_party/externals/vulkanmemoryallocator"  : "https://chromium.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator@7de5cc00de50e71a3aab22dea52fbb7ff4efceb6",
  "third_party/externals/spirv-cross"            : "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Cross@fc9bee27f445644635e83ef111ef54944bb6e3af",
  #"third_party/externals/v8"                     : "https://chromium.googlesource.com/v8/v8.git@5f1ae66d5634e43563b2d25ea652dfb94c31a3b4",
  "third_party/externals/wuffs"                  : "https://skia.googlesource.com/external/github.com/google/wuffs-mirror-release-c.git@a0041ac0310b3156b963e2f2bea09245f25ec073",
  "third_party/externals/zlib"                   : "https://chromium.googlesource.com/chromium/src/third_party/zlib@c876c8f87101c5a75f6014b0f832499afeb65b73",

  'bin': {
    'packages': [
      {
        'package': 'skia/tools/sk/${{platform}}',
        'version': Var('sk_tool_revision'),
      },
      {
        'package': 'infra/3pp/tools/ninja/${{platform}}',
        'version': Var('ninja_version'),
      }
    ],
    'dep_type': 'cipd',
  },
}
