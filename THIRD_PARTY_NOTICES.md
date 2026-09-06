# Third-party notices

This file documents the third-party fonts bundled with GameEditor and used by its
text regression tests. Project-owned code and documentation are provided under
the [MIT No Attribution (MIT-0) license](LICENSE). That license does not replace
the third-party font licenses and notices below.

## Bundled fonts

The font files remain under the SIL Open Font License, Version 1.1. The complete
license text and reserved font name notices are included in the files linked below.
Keep those notice files with the corresponding fonts when copying or distributing
the source or an application bundle. Preserve the fonts' embedded copyright metadata.

| Font family | Included files in `GameEditor/Content/Fonts/` | Complete notice |
| --- | --- | --- |
| D2Coding | `D2Coding-Ver1.3.3-20260725.ttf`, `D2Coding-Ver1.3.3-20260725-ligature.ttf`, `D2CodingBold-Ver1.3.3-20260725.ttf`, `D2CodingBold-Ver1.3.3-20260725-ligature.ttf` | [LICENSE-D2Coding.txt](GameEditor/Content/Fonts/LICENSE-D2Coding.txt) |
| NanumSquareNeo | `NanumSquareNeoOTF-Bd.otf`, `NanumSquareNeoOTF-Eb.otf`, `NanumSquareNeoOTF-Hv.otf`, `NanumSquareNeoOTF-Lt.otf`, `NanumSquareNeoOTF-Rg.otf` | [LICENSE-NanumSquareNeo-MaruBuri.txt](GameEditor/Content/Fonts/LICENSE-NanumSquareNeo-MaruBuri.txt) |
| MaruBuri | `MaruBuri-Bold.otf`, `MaruBuri-ExtraLight.otf`, `MaruBuri-Light.otf`, `MaruBuri-Regular.otf`, `MaruBuri-SemiBold.otf` | [LICENSE-NanumSquareNeo-MaruBuri.txt](GameEditor/Content/Fonts/LICENSE-NanumSquareNeo-MaruBuri.txt) |

The bundled font files also carry these copyright and design credits in their
OpenType `name` tables:

- D2Coding: Copyright (c) 2015-2016 NAVER Corporation. All rights reserved.
  Font designed by FONTRIX Inc.
- NanumSquareNeo: Copyright © 2022 NAVER Corp. All rights reserved.
  Font Designed by Sandoll Inc.
- MaruBuri: © NAVER Corp. © NAVER Cultural Foundation Corp.
  The manufacturer field names AG Typography Institute.

These credits supplement the complete notices above; they do not replace them.
The files under [GameEngineTests/TextBaselines](GameEngineTests/TextBaselines) are
glyph images produced by this engine from the bundled fonts, as described in their
[README](GameEngineTests/TextBaselines/README.md).

## Platform dependencies

Windows SDK, Direct3D, Windows Imaging Component, and Media Foundation dependencies
come from the build environment or operating system; their implementation binaries
are not vendored in this source tree. See [build dependencies](GameEngine/DEPENDENCIES.md)
for the engine's dependency boundaries.
