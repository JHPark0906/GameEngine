# Build dependencies

## Bundled font assets

GameEditor includes 14 D2Coding, NanumSquareNeo, and MaruBuri font files under
`GameEditor/Content/Fonts/`; the engine's text regression tests also use these files.
They are third-party assets under the SIL Open Font License, Version 1.1, with their
complete notices preserved in
[LICENSE-D2Coding.txt](../GameEditor/Content/Fonts/LICENSE-D2Coding.txt) and
[LICENSE-NanumSquareNeo-MaruBuri.txt](../GameEditor/Content/Fonts/LICENSE-NanumSquareNeo-MaruBuri.txt).
Keep those notices with the fonts. [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)
lists the included files and their embedded copyright credits. The project's
[MIT-0 license](../LICENSE) applies separately to project-owned code and documentation;
it does not replace these font licenses.

## Engine and platform dependencies

The engine has no third-party runtime parsing dependencies. Project/scene JSON and binary
FBX meshes, skeletons, and animation clips are parsed by engine-owned code. Compressed FBX arrays are decoded by the
engine's built-in zlib/DEFLATE reader.

Shared engine data follows its domain: `Core` owns GUIDs, JSON, and text encoding;
`Math/Float.h` owns plain-float storage; `Assets/ResourceId.h` and `Assets/VertexLayout.h`
own resource IDs and imported mesh layouts; and `Rendering/SpriteVertex.h` owns sprite/text
vertices. `Rendering/ShaderInterop.h` uses those types for the shared shader contract.
`UIModel` provides selection and text-edit models to Runtime and UI without depending on
Platform or rendering. `Platform/TextFile` and `Platform/RelativePath` provide file I/O and
root-relative path checks. These modules require no additional third-party library.

PNG/JPEG decoding uses Windows Imaging Component and requires no additional package.

The renderer supports D3D11 and native D3D12 from the Windows SDK. The D3D12 backend owns the
`ID3D12Device`, direct command queue, flip-model swap chain, synchronization fence, back-buffer
state transitions, command allocators/lists, upload buffers, root signatures, descriptor heaps,
and native mesh, 9-sliced sprite, and glyph-atlas text passes. The D3D12 passes reuse the same HLSL
source files as D3D11, but record their commands directly through D3D12. No third-party graphics dependency is required; applications link
the Windows SDK's `d3d11.lib`, `d3d12.lib`, and `dxgi.lib` components.

The project descriptor accepts `"graphicsApi": "D3D11"`, `"D3D12"`, or `"Auto"`. `Auto` probes
hardware D3D12 support first and falls back to hardware D3D11. An explicitly requested unsupported
backend fails during bootstrap instead of silently selecting another API.

Runtime text rasterization uses the engine's `Text` module: sfnt/cmap metrics, TrueType outlines,
CFF/Type 2 outlines, line breaking, and coverage rasterization. Applications register bundled
fonts explicitly; fallback searches those fonts in registration order, never installed system
fonts. Missing characters produce a visible box. The current cmap reader supports format 4
(the Basic Multilingual Plane); complex-script shaping is not implemented. Coverage is stored in
1024x1024 atlas pages, up to eight per epoch. A full atlas starts another epoch while published
pages remain alive through the frames that reference them. No DirectWrite runtime is required.

PCM WAV decoding is engine-owned. Other supported audio, including MP3, is decoded by the Win32
Media Foundation implementation and requires the Windows SDK Media Foundation libraries.

The FBX importer targets binary FBX 7.x geometry. It reads positions, polygon
indices, normals, and the first UV layer; triangulates polygons; restores Geometry-to-Model
instances and Model parent transforms; evaluates rotation/scaling pivots and pre/post
rotations; converts the declared axis system to the renderer's left-handed coordinates; and
normalizes the declared system unit to centimeters. Supported deformers and animation data
produce skinned meshes, skeletons, and clips; rigid model animation is also imported. ASCII FBX,
blend shapes, embedded materials, and transform inheritance modes beyond the implemented
composed-parent path remain outside the importer boundary. Unsupported animation interpolation
and tangent modes are rejected rather than approximated silently.
