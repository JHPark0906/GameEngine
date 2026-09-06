#pragma once

#include <cstddef>

namespace GameEngine::Rendering
{

/// <summary>
/// 한 프레임이 제출할 수 있는 draw의 상한이다.
///
/// 이 값이 공용인 이유는, 상한이 그래픽 API의 속성이 아니라 렌더링 정책이기 때문이다. 상한이
/// 백엔드마다 다르면 타일 수천 개짜리 레이어 하나가 한쪽에서는 도중에 잘리고 다른 쪽에서는
/// 전부 그려진다 — 같은 프레임이 두 백엔드에서 다른 픽셀을 내는 것이고, 그것은 이 엔진이
/// 지키려는 바로 그 약속을 깨는 일이다. 상한을 여기에 두면 두 백엔드는 같은 지점에서 함께
/// 멈춘다.
///
/// `RenderResourceCachePolicy`가 캐시 예산에 대해 같은 원칙이다.
/// </summary>
struct RenderSubmissionBudget
{
    /// <summary>프레임 하나가 그릴 수 있는 quad의 최대 개수이다.</summary>
    std::size_t maximumQuadsPerFrame = 0;
    /// <summary>프레임 하나의 스킨드 메시 제출 상한이다. 뼈 행렬 저장소는 이 예산으로 잡는다.</summary>
    std::size_t maximumSkinnedMeshesPerFrame = 0;
};

/// <summary>
/// 모든 백엔드가 지키는 제출 예산이다.
///
/// 16384는 64x64 타일맵 레이어(4096칸) 네 장을 한 프레임에 담고도 남는 크기이고, 백엔드가
/// 프레임마다 미리 잡아 두는 상수 저장소로 환산하면 몇 MB 수준이다. 늘리는 것이 공짜는 아니므로
/// — 상수 저장소는 in flight 프레임 수만큼 곱해진다 — 무한대가 아니라 선언된 유한한 수이며,
/// 넘으면 두 백엔드 모두 같은 자리에서 경고와 함께 멈춘다.
/// </summary>
/// <summary>스킨드 메시 256개는 128뼈 기준 슬롯당 2MiB의 뼈 상수를 사용한다.</summary>
inline constexpr RenderSubmissionBudget FrameSubmissionBudget{ 16384, 256 };

}
