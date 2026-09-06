#pragma once

// editor-layer: 1 (Document)

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "App/EditorSettings.h"
#include "Math/Vector.h"

namespace GameEditor
{

/// <summary>
/// 지속되는 에디터 설정과 그것을 파일에 남기는 일이다.
///
/// 설정 파일은 실행 파일 옆의 GameEditor.settings.json이다. 이 저장소의 배포 철학은 실행
/// 파일이 자기 파일을 자기 옆에서 찾는 것이고 — 에디터의 에셋도, 플레이어의 프로젝트도
/// 그렇다 — 에디터는 개발 도구라 자기 디렉터리에 쓸 수 있다. 사용자 디렉터리는 그 철학에
/// 없는 두 번째 탐색 규칙을 만들었을 것이다.
///
/// 저장은 상태가 바뀌는 <b>사건</b>의 시점이다: 패널 스왑, 프로젝트/장면 열기, 카메라 제스처
/// 종료. 즉시 모드 UI는 매 프레임 값을 다시 말하므로 프레임이 아니라 사건에 걸어야 디스크가
/// 조용하고, 내용이 같은 저장은 건너뛴다. 읽기는 실패해도 조용히 기본값이다 — 첫 실행과
/// 손상된 파일은 오류가 아니라 기본 상태다.
///
/// 설정 저장은 문맥의 다른 상태에 의존하지 않으며 독립적으로 시험할 수 있다.
/// <see cref="EditorContext"/>는 이 부품을 소유하고 설정 관련 호출을 전달한다.
/// </summary>
class EditorSettingsStore final
{
public:
    /// <summary>
    /// 에디터 설정 파일의 경로다. 이름과 자리가 여기 한 번만 적히도록, 컨텍스트가 세워지기
    /// 전에 설정을 읽어야 하는 부팅 경로도 이것을 쓴다.
    /// </summary>
    [[nodiscard]] static std::filesystem::path GetDefaultFilePath();

    /// <summary>실행 파일 옆의 설정을 읽어 세운다. 편집기가 쓰는 것이다.</summary>
    EditorSettingsStore();

    /// <summary>
    /// 그 경로의 설정을 읽어 세운다. 자리를 인자로 받는 것은 시험을 위해서다 — 실행 파일
    /// 옆에 쓰면 같은 기계의 다른 실행과 파일 하나를 두고 다투게 되고, 그러면 시험이 코드가
    /// 아니라 기계를 재게 된다.
    /// </summary>
    explicit EditorSettingsStore(std::filesystem::path filePath);

    /// <summary>지속되는 설정의 현재 값이다. 셸과 패널이 부팅 시 되살릴 값을 읽는다.</summary>
    [[nodiscard]] const GameEngine::App::EditorSettingsData& Get() const { return mSettings; }

    /// <summary>이 저장소가 쓰는 파일의 경로다.</summary>
    [[nodiscard]] const std::filesystem::path& GetFilePath() const { return mFilePath; }

    /// <summary>패널 배치가 바뀌었음을 알린다. 이전 값과 같으면 저장하지 않는다.</summary>
    /// <param name="panelInSlot">슬롯 i에 놓인 패널 번호들이다.</param>
    void UpdatePanelLayout(std::vector<std::size_t> panelInSlot);

    /// <summary>
    /// 콘솔 창이 떠 있는지와 그 자리를 남긴다. 값이 그대로면 파일을 쓰지 않는다.
    ///
    /// 사람이 배치한 것이 실행 때마다 사라지면 창을 옮기는 기능 자체를 쓰지 않게 된다.
    ///
    /// 사각형이 아니라 수 넷을 받는다. 이 문서 모델은 UI를 모르는 것이 규칙이고, 설정 파일에
    /// 적히는 것도 수 넷이다 — 사각형 타입을 여기까지 끌고 오면 그 규칙이 무너진다.
    /// </summary>
    /// <param name="floating">떠 있으면 true다.</param>
    /// <param name="x">창 왼쪽 변이다. 논리 픽셀이다.</param>
    /// <param name="y">창 위쪽 변이다. 논리 픽셀이다.</param>
    /// <param name="width">창의 폭이다. 논리 픽셀이다.</param>
    /// <param name="height">창의 높이다. 논리 픽셀이다.</param>
    void UpdateConsoleWindow(bool floating, float x, float y, float width, float height);

    /// <summary>격자 스냅을 켜거나 끈다. 값이 같으면 저장하지 않는다.</summary>
    /// <param name="enabled">스냅을 켜려면 true이다.</param>
    void SetGridSnapEnabled(bool enabled);

    /// <summary>씬 뷰 카메라가 바뀌었음을 알린다. 이전 값과 같으면 저장하지 않는다.</summary>
    /// <param name="pivot">궤도의 중심점이다.</param>
    /// <param name="distance">중심점까지의 거리이다.</param>
    /// <param name="yawDegrees">요 각도이다. 도 단위다.</param>
    /// <param name="pitchDegrees">피치 각도이다. 도 단위다.</param>
    void UpdateSceneCamera(
        const GameEngine::Math::Vector3& pivot, float distance, float yawDegrees,
        float pitchDegrees);

    /// <summary>다음 부팅이 돌아올 프로젝트를 적는다.</summary>
    void RememberLastProject(const std::filesystem::path& projectFilePath);

    /// <summary>다음 부팅이 돌아올 장면을 적는다.</summary>
    void RememberLastScene(unsigned int projectSceneId);

    /// <summary>설정을 파일로 쓴다. 마지막으로 쓴 텍스트와 같으면 건너뛴다.</summary>
    void SaveIfChanged();

private:
    std::filesystem::path mFilePath;
    GameEngine::App::EditorSettingsData mSettings;
    /// <summary>마지막으로 파일에 쓴 텍스트다. 같은 내용의 저장을 건너뛰는 근거다.</summary>
    std::string mLastSavedText;
};

}
