#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../Math/Vector.h"

namespace GameEngine::App
{

/// <summary>
/// 에디터의 사용자·머신 상태다: 패널 배치, 마지막으로 열었던 프로젝트/장면, 씬 뷰의 궤도
/// 카메라. 프로젝트 콘텐츠가 아니다 — 장면 파일이나 .gameproject에 섞이면 다른 머신의 다른
/// 사람에게 흘러가므로, 자기 파일을 따로 갖는다.
///
/// 스키마는 순수 값 — 번호, 문자열, 실수 — 뿐이라 엔진은 에디터의 타입을 하나도 모른 채 형식만
/// 안다. `ProjectSettings`가 플레이어와 에디터가 함께 읽는 파일 계약이듯, 이 계약도 App에 살아
/// 화면 없이 테스트된다; 값을 UI 상태로 옮기는 일은 에디터의 몫이다.
/// </summary>
struct EditorSettingsData
{
    /// <summary>슬롯 i에 놓인 패널 번호다. 비어 있으면 기본 배정이다.</summary>
    std::vector<std::size_t> panelInSlot;
    /// <summary>마지막으로 열었던 프로젝트 파일이다. 비어 있으면 없다.</summary>
    std::filesystem::path lastProjectPath;
    /// <summary>마지막으로 열었던 프로젝트 장면 id다. hasLastScene이 거짓이면 무의미하다.</summary>
    unsigned int lastSceneId = 0;
    bool hasLastScene = false;
    /// <summary>씬 뷰 궤도 카메라다. hasSceneCamera가 거짓이면 아래 값들은 무의미하다.</summary>
    bool hasSceneCamera = false;
    Math::Vector3 cameraPivot;
    float cameraDistance = 0.0f;
    float cameraYawDegrees = 0.0f;
    float cameraPitchDegrees = 0.0f;

    /// <summary>
    /// 에디터를 열 때 쓸 그래픽 백엔드 설정이다. `.gameproject`의 같은 이름 항목과 같은 값을
    /// 받는다: 백엔드 식별자("D3D11", "D3D12"), 엔진이 고르는 "Auto", 시작할 때 묻는 "Select".
    /// 쓸 수 있는 값은 설정 파일에도 함께 적히므로, 파일을 연 사람이 코드를 읽지 않아도 된다.
    ///
    /// 에디터의 것이 프로젝트의 것과 따로 있는 이유는 순서다. 에디터는 프로젝트를 열기 전에
    /// 창을 세우므로, 그 시점에 편집 대상 프로젝트의 설정은 아직 읽히지 않았다.
    /// <para>개발 단계 동안 두 백엔드를 자주 갈아 보기 위해 "Select"가 기본이다. 백엔드 작업이
    /// 끝나면 "Auto"로 되돌린다.</para>
    /// </summary>
    std::string graphicsApi = "Select";

    /// <summary>
    /// 콘솔이 도킹 칸을 떠나 창으로 떠 있는지, 그리고 그 창이 있던 자리다.
    ///
    /// 자리를 적어 두지 않으면 사람이 배치한 것이 실행 때마다 사라지고, 그러면 창을 옮기는
    /// 기능 자체를 쓰지 않게 된다. 사각형은 논리 픽셀의 수 넷으로 적는다 — 사각형을 나타내는
    /// 새 타입을 만들지 않는 것이 조건이고, Rect 모델이 바뀌어도 옮길 자리가 늘지 않는다.
    /// </summary>
    bool consoleFloating = false;
    float consoleWindowX = 0.0f;
    float consoleWindowY = 0.0f;
    float consoleWindowWidth = 0.0f;
    float consoleWindowHeight = 0.0f;

    /// <summary>씬 뷰 이동 기즈모가 격자에 맞춰 움직이는지다.</summary>
    bool gridSnapEnabled = false;
    /// <summary>격자 간격이다. 월드 단위이며 0 이하면 스냅이 없는 것과 같다.</summary>
    float gridSnapSpacing = 1.0f;

    [[nodiscard]] bool operator==(const EditorSettingsData&) const = default;
};

/// <summary>에디터 설정 파일의 읽기/쓰기 계약이다. 텍스트 왕복이 테스트의 대상이다.</summary>
class EditorSettings final
{
public:
    /// <summary>스키마 버전이다. 버전이 다른 파일은 통째로 기본값 취급이다.</summary>
    static constexpr int Version = 1;

    /// <summary>
    /// 설정을 결정적 JSON 텍스트로 쓴다: 같은 값은 같은 바이트다(Json::Dump가 키를 정렬한다).
    /// 없는 상태 — 빈 경로, 카메라 없음 — 는 키를 생략한다.
    /// </summary>
    /// <param name="data">쓸 설정 값이다.</param>
    /// <returns>버전 필드를 포함한 JSON 텍스트이다.</returns>
    [[nodiscard]] static std::string ToText(const EditorSettingsData& data);

    /// <summary>
    /// JSON 텍스트에서 설정을 읽는다. 첫 실행과 깨진 파일은 오류가 아니라 기본 상태다: 파싱
    /// 실패나 버전 불일치는 전부 기본값이고, 개별 키가 없거나 형태가 틀리면 그 부분만
    /// 기본값이다. 모르는 키는 무시한다 — 미래 버전이 적은 파일을 과거 에디터가 열어도 아는
    /// 것만 읽는다.
    /// </summary>
    /// <param name="text">설정 파일의 전체 텍스트이다.</param>
    /// <returns>읽어 낸 설정이며, 읽지 못한 부분은 기본값이다.</returns>
    [[nodiscard]] static EditorSettingsData FromText(std::string_view text);

    /// <summary>파일에서 읽는다. 없거나 읽을 수 없으면 조용히 기본값이다.</summary>
    /// <param name="filePath">설정 파일의 경로이다.</param>
    /// <returns>읽어 낸 설정이며, 파일이 없으면 기본값이다.</returns>
    [[nodiscard]] static EditorSettingsData Load(const std::filesystem::path& filePath);

    /// <summary>파일로 쓴다. 실패해도 던지지 않는다 — 로그는 호출자의 몫이다.</summary>
    /// <param name="data">쓸 설정 값이다.</param>
    /// <param name="filePath">설정 파일의 경로이다.</param>
    /// <returns>끝까지 썼으면 true이다.</returns>
    [[nodiscard]] static bool Save(
        const EditorSettingsData& data, const std::filesystem::path& filePath);
};

}
