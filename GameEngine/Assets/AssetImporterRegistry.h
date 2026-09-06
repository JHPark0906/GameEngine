#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetImporter.h"

namespace GameEngine::Assets
{

/// <summary>
/// 어느 임포터가 어느 확장자를 읽는지이다.
///
/// 엔진 자체의 임포터들은 정적 초기화로 스스로 등록하는 대신 하나의 표에 나열된다. 엔진은 정적
/// 라이브러리이고 링커는 아무도 참조하지 않는 오브젝트 파일을 자유롭게 버릴 수 있다 — 스스로
/// 등록하는 임포터라면 어떤 애플리케이션에는 있고 다른 것에는 없었을 것이고, 그 증상은 링크
/// 오류가 아니라 메시 없는 모델이었을 것이다. `Rendering/GraphicsBackendRegistry.cpp`가
/// 백엔드를 직접 나열하는 것도 같은 이유다.
///
/// 프로젝트는 <see cref="Register"/>로 포맷을 추가할 수 있다. 레지스트리는 받은 것을 소유하지
/// 않는다: 임포터는 등록보다 오래 살아야 하고, 함수 지역 static이나 네임스페이스 상수면
/// 충분하다. 등록은 스레드 안전하지 않으며 그럴 의도도 없다 — 프로젝트를 여는 동안, 무언가
/// 데이터베이스를 refresh하기 전에 일어나는 일이다.
/// </summary>
class AssetImporterRegistry final
{
public:
    /// <summary>
    /// 파일의 임포터이다. 그 확장자를 임포트하는 것이 없으면 null이며 — 데이터베이스가 파일이
    /// 에셋이 아니라고 판정하는 방법이 그것이다. 경로의 확장자는 대소문자 구분 없이 맞춘다.
    /// </summary>
    [[nodiscard]] static const IAssetImporter* Find(const std::filesystem::path& path);

    /// <summary>
    /// 경로 없이도, 확장자가 임포트되기는 하는지 여부이다. `.gameproject`는 데이터베이스가 이
    /// 위에 얹는 특례다: 프로젝트 루트에서만 에셋이다.
    /// </summary>
    [[nodiscard]] static bool IsSupportedExtension(std::string_view lowercaseExtension);

    /// <summary>
    /// 확장자에 임포터를 추가한다. 확장자는 소문자여야 하고 점으로 시작해야 한다. 엔진 자체
    /// 포맷을 포함해 이미 임포트하는 것이 있으면 false를 반환한다: `.fbx`의 의미가 조용히 바뀌면
    /// 알아채기 매우 어려우므로, 내장 포맷을 바꾸려면 먼저 명시적 <see cref="Unregister"/>가
    /// 필요하다.
    /// </summary>
    static bool Register(std::string_view lowercaseExtension, const IAssetImporter& importer);

    /// <summary>확장자의 임포터를 제거한다. 임포트하던 것이 없었으면 false를 반환한다.</summary>
    static bool Unregister(std::string_view lowercaseExtension);

    /// <summary>
    /// 지금 이 표에 있는 것 전부다. 확장자와, 그것을 임포트하는 것의 주소 — 레지스트리가
    /// 임포터를 소유하지 않으므로 스냅숏도 소유하지 않는다. 따라서 스냅숏은 그 임포터들보다
    /// 오래 살아서는 안 된다.
    /// </summary>
    using Registrations = std::vector<std::pair<std::string, const IAssetImporter*>>;

    /// <summary>
    /// 표를 통째로 복사해 둔다.
    ///
    /// <b>시험이 서로에게서 격리되기 위한 것이며, 제품 경로는 부르지 않는다.</b> 시험은 자기
    /// 포맷을 등록해 두고 끝에서 지우는데, 그 지우기를 한 번 잊으면 다음 시험이 「이미 있는
    /// 확장자」를 만나 이유 없이 붉어진다. 등록과 해제를 짝으로 맞추는 규율 대신, 들어올 때
    /// 찍어 두고 나갈 때 되돌리면 그 잊음이 불가능해진다.
    /// </summary>
    [[nodiscard]] static Registrations Snapshot();

    /// <summary>
    /// 표를 스냅숏 시점으로 되돌린다. 그 뒤에 더해진 것은 사라지고, 그 사이에 지워진 것은
    /// 돌아오며, 순서까지 그때 그대로가 된다.
    /// </summary>
    static void Restore(const Registrations& registrations);
};

}
