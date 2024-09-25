# This file manages the dependencies used by Skia developers for local, stand-alone Skia builds and
# Skia testing infrastructure. The versions specified by the commit hashes represent the revisions
# we happen to be currently testing. Skia provides no endorsement or recommendation on the revision
# to use for these libraries.

use_relative_paths = True

vars = {
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling different
  # dependencies without interference from each other.
  'sk_tool_revision': 'git_revision:db27834aff1f9d282ce9296092480c68528005f1',

  # ninja CIPD package version.
  # https://chrome-infra-packages.appspot.com/p/infra/3pp/tools/ninja
  'ninja_version': 'version:2@1.12.1.chromium.4',

  # googlefonts_testdata CIPD package version
  # https://chrome-infra-packages.appspot.com/p/chromium/third_party/googlefonts_testdata/
  'googlefonts_testdata_version': 'version:20230913',
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
  "third_party/externals/freetype"               : "https://chromium.googlesource.com/chromium/src/third_party/freetype2.git@73720c7c9958e87b3d134a7574d1720ad2d24442",
  "third_party/externals/harfbuzz"               : "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git@b74a7ecc93e283d059df51ee4f46961a782bcdb8",
  "third_party/externals/icu"                    : "https://chromium.googlesource.com/chromium/deps/icu.git@364118a1d9da24bb5b770ac3d762ac144d6da5a4",
  "third_party/externals/libjpeg-turbo"          : "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git@ccfbe1c82a3b6dbe8647ceb36a3f9ee711fba3cf",
  "third_party/externals/libpng"                 : "https://skia.googlesource.com/third_party/libpng.git@ed217e3e601d8e462f7fd1e04bed43ac42212429",
  "third_party/externals/libwebp"                : "https://chromium.googlesource.com/webm/libwebp.git@845d5476a866141ba35ac133f856fa62f0b7445f",
  "third_party/externals/vulkanmemoryallocator"  : "https://chromium.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator@a6bfc237255a6bac1513f7c1ebde6d8aed6b5191",
  "third_party/externals/spirv-cross"            : "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Cross@b8fcf307f1f347089e3c46eb4451d27f32ebc8d3",
  #"third_party/externals/v8"                     : "https://chromium.googlesource.com/v8/v8.git@5f1ae66d5634e43563b2d25ea652dfb94c31a3b4",
  "third_party/externals/wuffs"                  : "https://skia.googlesource.com/external/github.com/google/wuffs-mirror-release-c.git@e3f919ccfe3ef542cfc983a82146070258fb57f8",
  "third_party/externals/zlib"                   : "https://chromium.googlesource.com/chromium/src/third_party/zlib@646b7f569718921d7d4b5b8e22572ff6c76f2596",

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
