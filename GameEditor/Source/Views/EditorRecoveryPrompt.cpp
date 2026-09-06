#include "Views/EditorRecoveryPrompt.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "App/ProjectFile.h"
#include "Diagnostics/Debug.h"
#include "Rules/EditorConfirmation.h"
#include "Document/EditorContext.h"
#include "Rules/EditorRecovery.h"

namespace GameEditor
{
namespace
{
    /// <summary>이 사본을 두고 물을 글이다.</summary>
    [[nodiscard]] std::string MakeSnapshotQuestion(const RecoverySnapshot& snapshot)
    {
        std::string question =
            "The editor closed with unsaved changes in scene " + std::to_string(snapshot.sceneId) +
            " of this project, and a copy of that work was kept.";
        const std::string when = FormatSnapshotTime(snapshot.writeTime);
        if (!when.empty())
        {
            question += " It was left at " + when + ".";
        }
        question += " Restore it?";
        return question;
    }

    /// <summary>
    /// 이 이름의 프로젝트를 이 에디터가 아직 찾을 수 있는지 답한다. 아는 자리는 둘뿐이다 —
    /// 지금 열린 프로젝트가 있는 디렉터리와, 설정이 기억하는 지난번 프로젝트의 디렉터리.
    /// 찾을 수 없다고 해서 세상에 없는 것은 아니므로, 묻는 글도 "찾을 수 없다"라고만 적는다.
    /// </summary>
    [[nodiscard]] bool CanFindProject(
        const std::string& projectStem, const std::vector<std::filesystem::path>& searchDirectories)
    {
        for (const std::filesystem::path& directory : searchDirectories)
        {
            if (directory.empty())
            {
                continue;
            }
            std::error_code error;
            const std::filesystem::path candidate =
                directory / (projectStem + std::string(".gameproject"));
            if (std::filesystem::exists(candidate, error) && !error)
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>주인이 사라진 사본들을 날짜와 함께 적고 지울지 묻는다.</summary>
    void AskAboutOrphans(
        ConfirmationQueue& queue, const std::vector<RecoverySnapshot>& snapshots,
        const std::vector<std::filesystem::path>& searchDirectories)
    {
        // 주인이 사라진 사본은 아무도 다시 열지 않으므로 저 혼자 쌓인다. 지울지 정하려면
        // 무엇이 언제 남은 것인지 보여야 하므로, 이름과 날짜를 그대로 적는다.
        const std::vector<RecoverySnapshot> orphans = FindOrphanSnapshots(
            snapshots,
            [&searchDirectories](const std::string& projectStem)
            { return CanFindProject(projectStem, searchDirectories); });
        if (orphans.empty())
        {
            return;
        }

        std::string question =
            "These recovered copies belong to projects this editor can no longer find:";
        for (const RecoverySnapshot& orphan : orphans)
        {
            const std::string when = FormatSnapshotTime(orphan.writeTime);
            question += "\n  " + orphan.projectStem + " scene " + std::to_string(orphan.sceneId);
            if (!when.empty())
            {
                question += "  (" + when + ")";
            }
        }

        ConfirmationRequest request;
        request.title = "Recovered copies with no project";
        request.question = question;
        request.choices = { "Keep them", "Delete them" };
        request.onAnswered = [orphans](const std::optional<std::size_t> chosen)
        {
            // 답이 없으면 아무것도 하지 않는다. 답을 듣지 못했을 때 지우는 것이 가장 나쁘다.
            if (!chosen || *chosen != 1)
            {
                return;
            }
            for (const RecoverySnapshot& orphan : orphans)
            {
                static_cast<void>(DeleteRecoverySnapshot(orphan.filePath));
            }
        };
        queue.Ask(std::move(request));
    }

    /// <summary>
    /// 사본들을 하나씩 묻는다. 앞의 답이 온 뒤에 다음이 서는 이유는 겹침이다 — 물음 둘이 함께
    /// 서면 사람은 자기가 어느 사본에 답하는지 모른다.
    ///
    /// 목록을 공유 포인터로 쥐는 이유는 수명이다. 답은 프레임 뒤에 오므로 이 함수가 돌아간
    /// 뒤에도 목록이 살아 있어야 한다.
    /// </summary>
    void AskAboutSnapshotAt(
        EditorContext& context, ConfirmationQueue& queue,
        const std::shared_ptr<const std::vector<RecoverySnapshot>>& snapshots,
        const std::size_t index, const std::function<void()>& whenDone)
    {
        if (!snapshots || index >= snapshots->size())
        {
            if (whenDone)
            {
                whenDone();
            }
            return;
        }

        const RecoverySnapshot& snapshot = (*snapshots)[index];
        ConfirmationRequest request;
        request.title = "Unsaved work was recovered";
        request.question = MakeSnapshotQuestion(snapshot);
        request.choices = { "Restore it", "Delete the copy" };
        request.cancelLabel = "Decide later";
        request.onAnswered =
            [&context, &queue, snapshots, index, whenDone](const std::optional<std::size_t> chosen)
        {
            static_cast<void>(ApplyRecoveryChoice(
                (*snapshots)[index], ChoiceFromDialogAnswer(chosen, 0, 1),
                [&context](const RecoverySnapshot& one)
                { return context.RestoreRecoverySnapshot(one.filePath, one.sceneId); }));
            AskAboutSnapshotAt(context, queue, snapshots, index + 1, whenDone);
        };
        queue.Ask(std::move(request));
    }
}

void AskAboutRecoverySnapshots(
    EditorContext& context, ConfirmationQueue& queue,
    const std::filesystem::path& recoveryDirectory)
{
    const GameEngine::App::ProjectFileData* const project = context.GetOpenProject();
    if (!project)
    {
        return;
    }

    std::vector<RecoverySnapshot> all;
    for (const RecoverySnapshot& snapshot : CollectRecoverySnapshots(recoveryDirectory))
    {
        if (snapshot.projectFilePath.empty())
        {
            // 옛 파일명에는 프로젝트 이름만 있어 같은 이름의 체크아웃을 가를 수 없다.
            // 소유자를 추측해 복구하거나 지우지 않고, 수동 복구할 원본을 그대로 남긴다.
            GameEngine::Diagnostics::Debug::LogWarning(
                "A legacy recovery copy has no project path and was kept for manual recovery. "
                "Inspect it before copying it into the intended project's scene. path=",
                snapshot.filePath.string());
            continue;
        }
        all.push_back(snapshot);
    }
    const auto mine = std::make_shared<const std::vector<RecoverySnapshot>>(
        FindSnapshotsForProject(all, project->filePath));

    // 아는 자리 둘로 주인을 찾는다: 지금 프로젝트 옆과, 설정이 기억하는 지난번 프로젝트 옆.
    const GameEngine::App::EditorSettingsData& settings = context.GetSettings();
    const std::vector<std::filesystem::path> searchDirectories = {
        project->filePath.parent_path(),
        settings.lastProjectPath.empty() ? std::filesystem::path{}
                                         : settings.lastProjectPath.parent_path() };

    // 주인 없는 사본은 이 프로젝트의 것을 다 묻고 나서 묻는다. 사람이 자기 작업부터 보고
    // 나서 남의 찌꺼기를 보는 순서다.
    AskAboutSnapshotAt(
        context, queue, mine, 0,
        [&queue, all, searchDirectories]
        { AskAboutOrphans(queue, all, searchDirectories); });
}

}
