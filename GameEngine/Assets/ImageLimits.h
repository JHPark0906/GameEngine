#pragma once

#include <cstddef>

#include "../Platform/IImageDecoder.h"

namespace GameEngine::Assets
{

/// <summary>
/// 디코딩된 이미지 하나의 CPU 예산이다. 장치의 치수 한계 안에 드는 입력도 메모리를 소진할 만큼
/// 클 수 있으므로, 픽셀 버퍼를 할당하기 전에 디코딩을 거부한다.
///
/// 이것은 임포트 정책이며 공유로 남는다. 장치가 얼마나 큰 텍스처를 받는지는 그렇지 않다:
/// 그것은 장치가 보고하는 능력이고, 인자로 전달된다.
/// </summary>
inline constexpr std::size_t MaximumDecodedImageBytes = 64ull * 1024ull * 1024ull;

/// <summary>
/// 나중에 어떤 장치에서 그려지든, 엔진이 디코딩해 줄 가장 긴 변이다.
///
/// 이것은 정책 상한이지 어느 장치에 대한 주장도 아니다: 디코딩은 임포트 시점에 일어나고 그
/// 자리에는 장치가 없으므로, 자기 하드웨어가 받을 수 없는 이미지는 여전히 백엔드가 거부한다.
/// 두 한계는 서로 다른 질문에 답한다 — 이것은 일의 양을 제한하고, 장치 능력은 업로드할 수 있는
/// 것을 제한한다.
/// </summary>
inline constexpr unsigned int MaximumDecodedImageDimension = 16384;

/// <summary>임포터가 플랫폼 이미지 디코더에 넘기는 디코드 한계이다.</summary>
[[nodiscard]] inline Platform::ImageDecodeLimits GetImageDecodeLimits()
{
    return { MaximumDecodedImageDimension, MaximumDecodedImageBytes };
}

}
