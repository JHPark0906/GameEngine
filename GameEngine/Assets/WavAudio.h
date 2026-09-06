#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace GameEngine::Assets
{

/// <summary>WAV 파일에서 디코딩된 PCM이다. 리소스 id는 아직 없다 — 임포터가 정체성을 입힌다.</summary>
struct DecodedWavAudio
{
    std::uint32_t channelCount = 0;
    std::uint32_t sampleRate = 0;
    /// <summary>인터리브된 16비트 샘플들이다.</summary>
    std::vector<std::int16_t> samples;
};

/// <summary>
/// WAV(RIFF) 바이트를 16비트 PCM으로 디코딩한다. 16/24비트 정수 PCM 모노/스테레오를 지원한다.
/// 24비트는 하위 8비트를 버리며 샘플 수·채널·샘플레이트를 유지한다. 그 밖의 포맷(부동소수,
/// 압축, 3채널 이상)과 잘린 샘플 프레임은 이유와 함께 거절한다. 파일을 열지 않고 바이트만
/// 받으므로 화면도 장치도 없이 시험된다. 실패 시 기존 decoded는 유지된다.
/// </summary>
/// <param name="fileBytes">WAV 파일의 전체 바이트다.</param>
/// <param name="decoded">성공 시 채워지는 PCM이다.</param>
/// <param name="error">실패 시 이유가 담긴다.</param>
/// <returns>디코딩했으면 true이다.</returns>
[[nodiscard]] bool DecodeWavAudio(
    std::span<const std::byte> fileBytes, DecodedWavAudio& decoded, std::string& error);

}
