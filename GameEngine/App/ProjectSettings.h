#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "../Platform/IWindow.h"

namespace GameEngine::App
{

// Window appearance is a platform concept. App configures it; it does not define it.
using WindowChrome = Platform::WindowChrome;
using WindowTheme = Platform::WindowTheme;

/// <summary>
/// 플레이어 애플리케이션을 초기화하는 데 필요한 프로젝트 설정을 보관한다.
/// </summary>
struct ProjectSettings
{
    ProjectSettings() = default;

    /// <summary>필수 문자열, 크기, 프레임 속도 및 초기 장면 설정을 검증한다.</summary>
    /// <returns>애플리케이션 실행에 필요한 값이 모두 유효하면 true이다.</returns>
    [[nodiscard]] bool IsValid() const
    {
        return !projectName.empty()
            && windowWidth > 0
            && windowHeight > 0
            && targetFrameRate > 0.0f
            && scenePaths.contains(initialSceneId);
    }

    /// <summary>창 제목으로 사용할 프로젝트 이름이다.</summary>
    std::wstring projectName;
    /// <summary>클라이언트 영역의 가로 크기이다.</summary>
    int windowWidth = 1280;
    /// <summary>클라이언트 영역의 세로 크기이다.</summary>
    int windowHeight = 720;
    /// <summary>프로젝트가 목표로 하는 초당 프레임 수이다.</summary>
    float targetFrameRate = 60.0f;

    /// <summary>
    /// 이 프로젝트의 <b>코드</b>가 사는 디렉터리이다. 프로젝트 파일 기준 상대 경로이며,
    /// 비어 있거나 <c>"."</c>이면 프로젝트 파일이 있는 자리다.
    ///
    /// 에셋은 이것을 묻지 않는다 — <c>.gameproject</c>가 있는 디렉터리가 언제나 에셋
    /// 루트다. 코드만 따로 물을 수 있는 이유는 <b>코드가 에셋과 같은 자리가 아닐 수 있기
    /// 때문</b>이다 — 프로젝트 파일과 에셋을 <c>Content/</c>에 두고 <c>CMakeLists.txt</c>는
    /// 저장소 루트에 두는 모양이 그것이고, 그때 이 값이 <c>".."</c>이다.
    ///
    /// 이것을 <b>물어서</b> 아는 이유는, 답을 짐작하면 규칙이 둘이 되기 때문이다. 프로젝트
    /// 파일이 있는 자리에서 위로 올라가며 <c>CMakeLists.txt</c>를 찾는 쪽을 택했다면, 「프로젝트가
    /// 어디서 시작하는가」에 답하는 자리가 파일 하나와 탐색 규칙 하나로 갈라졌을 것이다.
    ///
    /// 읽는 곳은 편집기의 빌드 하나뿐이다. 에셋 데이터베이스도 플레이어도 패키징도 이 값을
    /// 보지 않는다 — 출하되는 것은 콘텐츠이지 코드가 아니다.
    /// </summary>
    std::filesystem::path sourceRootPath;
    /// <summary>장면 ID와 배포된 장면 파일 경로의 대응표이다.</summary>
    std::unordered_map<unsigned int, std::filesystem::path> scenePaths;
    /// <summary>애플리케이션 시작 시 불러올 장면 ID이다.</summary>
    unsigned int initialSceneId = 0;
    /// <summary>
    /// 프로젝트가 요청한 그래픽 백엔드 식별자이다. 비어 있거나 "Auto"이면 실행 환경이 지원하는
    /// 백엔드를 자동 선택한다. App 계층은 유효한 식별자 목록을 알지 않으며, 해석은 Rendering의
    /// 백엔드 레지스트리가 담당한다.
    /// </summary>
    std::string graphicsApi;
    /// <summary>창 장식 방식이다.</summary>
    WindowChrome windowChrome = WindowChrome::System;
    /// <summary>창의 색상 모드이다.</summary>
    WindowTheme windowTheme = WindowTheme::System;
    /// <summary>
    /// 실행 파일과 창의 얼굴이 될 <c>.ico</c> 에셋의 guid다. 비어 있으면 아이콘 없음이고,
    /// 유효한 설정이다 — 모든 프로젝트가 자기 아이콘을 가질 필요는 없다.
    ///
    /// 경로가 아니라 guid인 이유는 에셋의 다른 식별자들과 같다: 사람이 파일을 옮기거나
    /// 이름을 바꿔도 참조가 따라간다. 실제 경로로 푸는 일은 살아있는 에셋 데이터베이스가
    /// 있는 쪽(빌드 스크립트를 쓰는 자리)의 몫이고, 이 구조체는 값을 나르기만 한다.
    /// </summary>
    std::string icon;
};

}
