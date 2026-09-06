#pragma once

#include <cstdint>
#include <vector>

namespace GameEngine::Assets
{

/// <summary>
/// 재생 준비가 끝난 디코딩된 오디오 클립이다. MeshData·TextureData와 같은 공유 페이로드
/// 관례를 따른다: 데이터베이스가 상주를 쥐고, 재생 시스템은 보이스가 사는 동안 shared_ptr로
/// 함께 쥔다 — 플랫폼 출력은 이 메모리에서 복사 없이 재생한다.
/// </summary>
struct AudioData
{
    /// <summary>모든 리소스 종류에 걸쳐 유일한 id이다. `ResourceIdDomain::Audio`로 만든다.</summary>
    std::uint64_t id = 0;
    /// <summary>채널 수다. 1(모노) 또는 2(스테레오)다.</summary>
    std::uint32_t channelCount = 0;
    /// <summary>초당 샘플 프레임 수다.</summary>
    std::uint32_t sampleRate = 0;
    /// <summary>인터리브된 16비트 PCM 샘플들이다.</summary>
    std::vector<std::int16_t> samples;

    /// <summary>재생할 수 있는 소리를 담고 있는지다.</summary>
    [[nodiscard]] bool IsValid() const
    {
        return id != 0 && channelCount > 0 && sampleRate > 0 && !samples.empty();
    }

    /// <summary>디코딩된 PCM의 원래 길이다. 재생할 수 없는 데이터는 0초다.</summary>
    [[nodiscard]] double GetDurationSeconds() const
    {
        // samples는 채널별 값의 개수다. PCM 프레임 수로 환산하며 디코더가 남긴 패딩도 포함한다.
        return IsValid()
            ? static_cast<double>(samples.size()) / static_cast<double>(channelCount) /
                static_cast<double>(sampleRate)
            : 0.0;
    }
};

}
