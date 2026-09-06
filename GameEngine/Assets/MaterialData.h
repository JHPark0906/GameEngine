#pragma once

#include "AssetReference.h"
#include "../Math/Color.h"

namespace GameEngine::Assets
{

/// <summary>
/// 렌더러가 사용하는 표면 성질이다. 데이터베이스가 상주를 소유하며 필요한 쪽은
/// shared_ptr로 함께 소유한다. 사람이 작성하는 .material 파일의 내용이 이 페이로드를 정의한다.
///
/// GPU 리소스 캐시용 id는 없다. 장면 프론트엔드가 이 값에서 텍스처 참조와 tint를 읽어
/// draw에 반영하고, 텍스처 페이로드가 자기 GPU 캐시 id를 제공한다.
/// </summary>
struct MaterialData
{
    /// <summary>표면에 입힐 텍스처다. 비어 있으면 텍스처 없이 tint만으로 그려진다.</summary>
    AssetReference texture;
    /// <summary>텍스처에 곱해지는 색이다. 텍스처가 없으면 이 색 자체가 표면 색이다.</summary>
    Math::Color tint{ 1.0f, 1.0f, 1.0f, 1.0f };

    [[nodiscard]] bool operator==(const MaterialData&) const = default;
};

}
