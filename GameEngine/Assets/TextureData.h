#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace GameEngine::Assets
{

/// <summary>
/// 프론트엔드가 한 머티리얼을 위해 만든 디코딩된 이미지 픽셀이다. 픽셀당 4바이트, 위에서 아래
/// 순서다.
///
/// 이미지 디코딩은 플랫폼 설비라서, 텍스트 래스터화와 같은 이유로 프론트엔드에 속한다: 백엔드는
/// 해석할 파일이 아니라 바이트를 받고, 모든 백엔드가 같은 바이트를 받는다. 프레임이 파일 경로를
/// 실으면 백엔드마다 자기 이미지 디코더로 파일을 여는 셈이 된다.
///
/// draw들은 shared_ptr로 이미지를 공유하므로 텍스처 하나는 디코드 한 번 비용이고, 백엔드가
/// 끝나기 전에 프론트엔드 캐시가 항목을 퇴거해도 프레임은 유효하게 남는다.
/// </summary>
struct TextureData
{
    /// <summary>
    /// 백엔드 GPU 캐시를 위한 정체성이다. 하나의 id가 다른 픽셀에 재사용되는 일은 없고, 같은
    /// 파일은 프론트엔드가 이미지를 캐시하지 못한 프레임을 지나서도 자기 id를 유지하므로,
    /// 백엔드는 픽셀을 다시 들여다보지 않고 id에 대고 업로드를 캐시해도 된다.
    /// </summary>
    std::uint64_t id = 0;
    /// <summary>
    /// 같은 id 아래에서 픽셀이 바뀐 횟수다. 에셋은 0에 머물지만, 매 프레임 새 그림을 받는
    /// 텍스처 — 에디터의 뷰 이미지 — 는 크기를 그대로 두고 이것만 올린다. 백엔드는 id로 캐시한
    /// 리소스의 revision이 다르면 새로 만들지 않고 픽셀만 다시 올린다. 크기가 바뀌면 새 id다.
    /// </summary>
    std::uint64_t revision = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<std::byte> pixels;

    static constexpr unsigned int BytesPerPixel = 4;

    [[nodiscard]] bool IsValid() const
    {
        return id != 0 && width > 0 && height > 0 && pixels.size() == GetByteSize();
    }

    [[nodiscard]] std::size_t GetByteSize() const
    {
        return static_cast<std::size_t>(width) * height * BytesPerPixel;
    }

    [[nodiscard]] unsigned int GetRowPitch() const { return width * BytesPerPixel; }
};

}
