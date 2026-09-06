#pragma once

// editor-layer: 2 (Views)

#include <filesystem>
#include <string>
#include <vector>

#include "UI/UIContext.h"
#include "Rules/EditorPanelHosts.h"

namespace GameEditor
{

class EditorContext;


/// <summary>
/// 콘텐츠 브라우저 패널이다: 프로젝트 루트 아래의 디렉터리를 오르내리며 에셋 파일을 보이고,
/// 모델 파일의 더블클릭이 그 모델을 열린 장면에 인스턴스화한다. 탐색 상태 — 현재 디렉터리,
/// 항목 목록, 프로젝트 리비전 — 를 자신이 소유한다.
/// </summary>
class EditorContentBrowserPanel final
{
public:
    EditorContentBrowserPanel(
        IEditorScale& scale, IAssetDragHost& assetDrag, EditorContext& context, GameEngine::UI::UIContext& ui);

    /// <summary>패널 내용을 그린다. 제목줄 프레임은 셸이 이미 그렸고, 그 아래 영역을 받는다.</summary>
    void Draw(GameEngine::UI::UIRect content);

private:
    /// <summary>콘텐츠 브라우저의 행 하나이다.</summary>
    struct BrowserEntry
    {
        std::filesystem::path path;
        std::string label;
        bool isDirectory = false;
        /// <summary>
        /// 이 파일을 가리키는 참조 문자열이다. 인스펙터의 참조 칸이 기대하는 형태 그대로이며,
        /// 에셋으로 등록되지 않은 파일은 비어 있다 — 그런 파일은 가리킬 이름이 없다.
        /// </summary>
        std::string reference;
    };

    /// <summary>96 DPI 기준의 논리 길이를 이 화면의 픽셀로 바꾼다. 셸의 배율을 따른다.</summary>
    [[nodiscard]] float S(float logical) const;

    void RefreshBrowserEntries();
    void ActivateBrowserEntry(const BrowserEntry& entry);
    /// <summary>
    /// 이 항목의 참조를 클립보드에 놓아 경로를 손으로 옮겨 적을 필요를 줄인다.
    /// </summary>
    void CopyEntryReference(const BrowserEntry& entry);
    /// <summary>
    /// 이 항목을 인스펙터의 대상으로 삼는다. 선택은 한 번에 하나뿐이라, 계층에서 골라 둔
    /// GameObject가 있었으면 그 선택이 풀린다.
    /// </summary>
    void SelectEntry(const BrowserEntry& entry);

    IEditorScale& mScale;
    IAssetDragHost& mAssetDrag;
    EditorContext& mContext;
    GameEngine::UI::UIContext& mUI;

    // 콘텐츠 브라우저의 탐색 상태. 프로젝트가 바뀌면 루트로 돌아간다.
    std::filesystem::path mProjectRootPath;
    std::filesystem::path mCurrentDirectory;
    std::vector<BrowserEntry> mBrowserEntries;
    /// <summary>마지막으로 복사한 참조다. 그 행을 선택 상태로 보여 "복사됐다"를 말한다.</summary>
    std::string mCopiedReference;
    unsigned int mBrowserRevision = 0;
    bool mBrowserNeedsRefresh = true;
};

}
