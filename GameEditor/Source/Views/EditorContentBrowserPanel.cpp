#include "Views/EditorContentBrowserPanel.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <system_error>
#include <utility>

#include "Document/EditorCommands.h"
#include "Document/EditorContext.h"
#include "Rules/EditorPanelCommon.h"
#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Diagnostics/Debug.h"
#include "Runtime/GameObject.h"
#include "Runtime/ModelInstantiation.h"
#include "Runtime/Scene.h"

namespace GameEditor
{

namespace
{
    using GameEngine::UI::UIRect;

    [[nodiscard]] bool IsPathWithinRoot(
        const std::filesystem::path& path, const std::filesystem::path& root)
    {
        auto rootIterator = root.begin();
        for (auto part = path.begin(); rootIterator != root.end(); ++part, ++rootIterator)
        {
            if (part == path.end() || *part != *rootIterator)
            {
                return false;
            }
        }
        return true;
    }
}

EditorContentBrowserPanel::EditorContentBrowserPanel(
    IEditorScale& scale, IAssetDragHost& assetDrag, EditorContext& context, GameEngine::UI::UIContext& ui)
    : mScale(scale), mAssetDrag(assetDrag), mContext(context), mUI(ui)
{
}

float EditorContentBrowserPanel::S(const float logical) const
{
    return mScale.S(logical);
}

void EditorContentBrowserPanel::RefreshBrowserEntries()
{
    mBrowserEntries.clear();
    const GameEngine::Assets::AssetDatabase* const assetDatabase =
        mContext.GetProjectAssetDatabase();
    if (!assetDatabase)
    {
        return;
    }
    mProjectRootPath = assetDatabase->GetProjectRootPath();
    if (mCurrentDirectory.empty() || !IsPathWithinRoot(mCurrentDirectory, mProjectRootPath))
    {
        mCurrentDirectory = mProjectRootPath;
    }

    std::error_code error;
    std::vector<std::filesystem::directory_entry> entries;
    for (std::filesystem::directory_iterator iterator(
             mCurrentDirectory, std::filesystem::directory_options::skip_permission_denied, error);
         !error && iterator != std::filesystem::directory_iterator();
         iterator.increment(error))
    {
        const std::filesystem::directory_entry entry = *iterator;
        const std::filesystem::path resolvedPath =
            std::filesystem::weakly_canonical(entry.path(), error);
        if (error)
        {
            error.clear();
            continue;
        }
        if (IsPathWithinRoot(resolvedPath, mProjectRootPath))
        {
            entries.push_back(entry);
        }
    }
    std::ranges::sort(entries, [](const auto& left, const auto& right)
    {
        std::error_code sortError;
        const bool leftIsDirectory = left.is_directory(sortError);
        const bool rightIsDirectory = right.is_directory(sortError);
        if (leftIsDirectory != rightIsDirectory)
        {
            return leftIsDirectory;
        }
        return left.path().filename() < right.path().filename();
    });

    for (const std::filesystem::directory_entry& entry : entries)
    {
        BrowserEntry browserEntry;
        browserEntry.path = entry.path();
        std::error_code entryError;
        browserEntry.isDirectory = entry.is_directory(entryError);
        if (!browserEntry.isDirectory)
        {
            // 에셋의 메타데이터 파일은 그 에셋의 일부다. 따로 보이면 사람이 고를 수 없는 것을
            // 고르게 하고, 목록을 두 배로 만든다.
            std::error_code sidecarError;
            const std::filesystem::path relativeToRoot =
                std::filesystem::relative(entry.path(), mProjectRootPath, sidecarError);
            if (!sidecarError && assetDatabase->FindSidecarOwner(relativeToRoot))
            {
                continue;
            }
        }
        const std::string name = entry.path().filename().generic_string();
        if (browserEntry.isDirectory)
        {
            browserEntry.label = "[Dir] " + name;
        }
        else
        {
            const std::optional<GameEngine::Assets::AssetType> type =
                GameEngine::Assets::AssetDatabase::GetAssetType(entry.path());
            browserEntry.label = type
                ? "[" +
                    std::string(GameEngine::Assets::AssetDatabase::GetAssetTypeName(*type)) +
                    "] " + name
                : "[File] " + name;

            // 참조는 사람이 경로를 옮겨 적어 만드는 것이 아니라 에셋 자신에게 묻는다. 등록되지
            // 않은 파일은 가리킬 이름이 없으므로 비어 있고, 클릭해도 복사할 것이 없다.
            const std::filesystem::path relativePath =
                std::filesystem::relative(entry.path(), mProjectRootPath, entryError);
            if (const GameEngine::Assets::Asset* const asset =
                    entryError ? nullptr : assetDatabase->FindAsset(relativePath))
            {
                // 끌어다 놓을 때 놓일 문자다. 이관된 프로젝트라면 정체성으로, 아직이면
                // 경로로 적는다 — 한 장면에 두 형식이 섞이지 않게 하는 것이 이 갈라짐의 목적이다.
                browserEntry.reference =
                    GameEngine::Assets::MakeAssetReference(
                        *asset,
                        mContext.AreSceneReferencesMigrated()
                            ? GameEngine::Assets::AssetReferenceForm::Identity
                            : GameEngine::Assets::AssetReferenceForm::Path)
                        .ToString();
            }
        }
        mBrowserEntries.push_back(std::move(browserEntry));
    }
}

void EditorContentBrowserPanel::CopyEntryReference(const BrowserEntry& entry)
{
    if (entry.isDirectory)
    {
        return;
    }
    if (entry.reference.empty())
    {
        // 에셋으로 등록되지 않은 파일이다 — 임포터가 없는 확장자이거나, 아직 스캔되지 않았다.
        // 조용히 아무 일도 하지 않으면 사람은 클릭이 먹히지 않은 것과 구분할 수 없다.
        GameEngine::Diagnostics::Debug::LogWarning(
            "This file is not a registered asset, so nothing refers to it and there is no "
            "reference to copy. file=", entry.path.filename().generic_string());
        return;
    }

    mUI.SetClipboardText(entry.reference);
    mCopiedReference = entry.reference;

    // 등록된 파일이라도 임포트에 실패했으면 그 안에 에셋이 없어 참조가 해석되지 않는다. 그것을
    // 말하지 않으면, 붙여넣은 사람은 브라우저에서 복사한 참조가 인스펙터에서 "not found"로
    // 뜨는 것을 보고 자기 손을 의심하게 된다.
    const GameEngine::Assets::AssetDatabase* const assetDatabase =
        mContext.GetProjectAssetDatabase();
    if (assetDatabase &&
        GameEngine::Assets::ClassifyAssetReference(
            *assetDatabase, GameEngine::Assets::AssetReference::Parse(entry.reference)) !=
            GameEngine::Assets::AssetReferenceStatus::Resolved)
    {
        GameEngine::Diagnostics::Debug::LogWarning(
            "Copied a reference to a file this project could not import, so the reference will "
            "not resolve until the file imports. Earlier log lines say why it failed. reference=",
            entry.reference);
        return;
    }

    GameEngine::Diagnostics::Debug::Log(
        "Copied an asset reference to the clipboard; paste it into an inspector reference field. "
        "reference=", entry.reference);
}


void EditorContentBrowserPanel::SelectEntry(const BrowserEntry& entry)
{
    if (entry.isDirectory || entry.reference.empty())
    {
        // 디렉터리, 그리고 아직 에셋으로 등록되지 않은 파일은 인스펙터가 보일 것이 없다 — 지금
        // 고른 것을 그대로 둔다.
        return;
    }
    std::error_code error;
    const std::filesystem::path relativePath =
        std::filesystem::relative(entry.path, mProjectRootPath, error);
    if (!error)
    {
        mContext.SelectAsset(relativePath);
    }
}

void EditorContentBrowserPanel::ActivateBrowserEntry(const BrowserEntry& entry)
{
    if (entry.isDirectory)
    {
        std::error_code error;
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(entry.path, error);
        if (!error && std::filesystem::is_directory(canonical, error) && !error &&
            IsPathWithinRoot(canonical, mProjectRootPath))
        {
            mCurrentDirectory = canonical;
            mBrowserNeedsRefresh = true;
        }
        return;
    }

    // 모델 파일의 더블클릭이 모델을 열린 장면에 넣는다. `MeshRenderer`는 메시 하나를 가리키므로,
    // 여러 부품을 담은 파일을 손으로 조립하는 대신 이 한 번의 제스처가 전체를 인스턴스화한다.
    const GameEngine::Assets::AssetDatabase* const assetDatabase =
        mContext.GetProjectAssetDatabase();
    if (!assetDatabase)
    {
        return;
    }
    std::error_code error;
    const std::filesystem::path relativePath =
        std::filesystem::relative(entry.path, mProjectRootPath, error);
    const GameEngine::Assets::Asset* const asset =
        error ? nullptr : assetDatabase->FindAsset(relativePath);
    if (!asset || asset->GetType() != GameEngine::Assets::AssetType::Mesh)
    {
        return;
    }
    GameEngine::Runtime::Scene* targetScene = mContext.GetOpenScene();
    if (!targetScene)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Cannot instantiate a model without an open scene. path=", relativePath.string());
        return;
    }
    GameEngine::Runtime::GameObject* const instantiated = GameEngine::Runtime::InstantiateModel(
        *targetScene, *assetDatabase, relativePath);
    if (instantiated)
    {
        mContext.SelectObject(instantiated->GetInstanceId());
        // 인스턴스화도 되돌릴 수 있는 편집이다: 만들어진 부분 트리를 스냅숏해 기록하면, undo가
        // 지우고 redo가 스냅숏에서 되세운다 — 모델을 다시 임포트하지 않는다.
        mContext.RecordEdit(std::make_unique<InstantiateModelCommand>(mContext, *instantiated));
    }
}

void EditorContentBrowserPanel::Draw(GameEngine::UI::UIRect content)
{
    if (mBrowserRevision != mContext.GetProjectRevision() || mBrowserNeedsRefresh)
    {
        if (mBrowserRevision != mContext.GetProjectRevision())
        {
            mCurrentDirectory.clear();
        }
        mBrowserRevision = mContext.GetProjectRevision();
        mBrowserNeedsRefresh = false;
        RefreshBrowserEntries();
    }

    if (!mContext.GetProjectAssetDatabase())
    {
        mUI.DrawLabel(
            { content.x + S(Padding), content.y, content.width - 2.0f * S(Padding), S(RowHeight) },
            "Open a project to browse its content.", DimTextColor, RowFontSize);
        return;
    }

    // 위로 가기 버튼과 현재 위치. 루트 밖으로는 올라가지 않는다.
    const UIRect toolbarRow{ content.x, content.y, content.width, S(RowHeight) + 2.0f * S(Padding) };
    if (mUI.DrawButton(
            GameEngine::UI::MakeWidgetId("browser-up"),
            { toolbarRow.x + S(Padding), toolbarRow.y + S(Padding), S(40.0f), S(RowHeight) }, "Up") &&
        !mCurrentDirectory.empty() && mCurrentDirectory != mProjectRootPath)
    {
        mCurrentDirectory = mCurrentDirectory.parent_path();
        mBrowserNeedsRefresh = true;
    }
    std::error_code error;
    const std::filesystem::path displayedPath =
        std::filesystem::relative(mCurrentDirectory, mProjectRootPath, error);
    mUI.DrawLabel(
        { toolbarRow.x + S(40.0f) + 2.0f * S(Padding), toolbarRow.y,
          toolbarRow.width - S(40.0f) - 3.0f * S(Padding), toolbarRow.height },
        error || displayedPath.empty() ? "." : displayedPath.generic_string(),
        DimTextColor, SecondaryFontSize);
    content = { content.x, content.y + toolbarRow.height,
        content.width, content.height - toolbarRow.height };

    const float offset = mUI.ApplyScroll(
        GameEngine::UI::MakeWidgetId("browser-scroll"), content,
        static_cast<float>(mBrowserEntries.size()) * S(RowHeight));
    float y = content.y - offset;
    for (std::size_t index = 0; index < mBrowserEntries.size(); ++index)
    {
        const UIRect rowRect{ content.x, y, content.width, S(RowHeight) };
        y += S(RowHeight);
        if (rowRect.y < content.y || rowRect.GetBottom() > content.GetBottom())
        {
            continue;
        }
        const BrowserEntry& entry = mBrowserEntries[index];
        // 마지막으로 복사한 항목을 선택 상태로 보인다: 클립보드는 보이지 않는 곳이라, 무엇이
        // 들어갔는지 화면이 말하지 않으면 복사됐는지조차 알 수 없다.
        const bool isCopied =
            !entry.reference.empty() && entry.reference == mCopiedReference;
        const auto result = mUI.DrawSelectable(
            GameEngine::UI::MakeWidgetId("browser-entry", index), rowRect, "", isCopied);
        mUI.DrawLabel(
            { rowRect.x + S(Padding), rowRect.y, rowRect.width - 2.0f * S(Padding), rowRect.height },
            entry.label, entry.isDirectory ? TextColor : DimTextColor, RowFontSize);
        // 누른 그 프레임에 집는다. 문턱을 넘지 못하면 끌기가 아니라 클릭이므로, 아래의 복사는
        // 그대로 일어난다 — 집는 것이 기존 제스처를 뺏지 않는다.
        if (result.clicked && !entry.reference.empty())
        {
            mAssetDrag.BeginAssetDrag(
                GameEngine::Assets::AssetReference::Parse(entry.reference));
        }
        // 클릭은 참조를 복사하고 인스펙터의 대상으로 고르며, 더블클릭은 그대로 연다 — 셋 다
        // 기존 제스처를 뺏지 않는다.
        if (result.clicked && !result.doubleClicked)
        {
            CopyEntryReference(entry);
            SelectEntry(entry);
        }
        if (result.doubleClicked)
        {
            ActivateBrowserEntry(entry);
            // 디렉터리를 이동하면 인덱스가 다른 파일을 가리키므로, 이 프레임의 목록은 여기까지다.
            if (mBrowserNeedsRefresh)
            {
                break;
            }
        }
    }
}

}
