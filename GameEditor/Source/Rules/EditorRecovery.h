#pragma once

// editor-layer: 0 (Rules)

#include <filesystem>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GameEditor
{

/// <summary>
/// 사고로 닫힌 에디터가 남긴 장면 사본 하나다. 파일이 곧 전부이므로, 되살릴지 물을 때
/// 필요한 것 — 어느 프로젝트의 몇 번 장면인지, 언제 남았는지 — 을 이름과 파일 시각에서
/// 읽어 담는다.
/// </summary>
struct RecoverySnapshot
{
    /// <summary>복구 파일의 전체 경로다.</summary>
    std::filesystem::path filePath;
    /// <summary>남긴 프로젝트 파일의 이름이다(확장자 없음).</summary>
    std::string projectStem;
    /// <summary>프로젝트 안에서의 장면 id다.</summary>
    unsigned int sceneId = 0;
    /// <summary>파일이 마지막으로 써진 시각이다.</summary>
    std::filesystem::file_time_type writeTime{};
    /// <summary>사본을 남긴 프로젝트의 절대 경로다. 예전 이름 전용 사본은 비어 있다.</summary>
    std::filesystem::path projectFilePath;
};

/// <summary>
/// 복구 파일이 사는 디렉터리다. 프로젝트 옆이 아니라 임시 디렉터리인 이유는, 사고가 난
/// 프로젝트가 읽기 전용이거나 사라진 뒤일 수 있기 때문이다.
///
/// 디렉터리를 인자로 받는 함수들이 따로 있는 이유는 시험 때문이다. 시험이 이 자리에 고정된
/// 이름으로 쓰면, 같은 순간에 도는 다른 시험 프로세스와 같은 파일을 두고 다투게 되고, 그
/// 다툼은 시험이 아니라 기계의 상태를 재는 것이 된다.
/// </summary>
/// <returns>디렉터리 경로다. 임시 디렉터리를 얻지 못하면 빈 경로다.</returns>
[[nodiscard]] std::filesystem::path GetRecoveryDirectory();

/// <summary>사본 하나를 두고 사람이 고른 답이다.</summary>
enum class RecoveryChoice
{
    /// <summary>사본을 되살린다.</summary>
    Restore,
    /// <summary>사본을 버리고 지운다.</summary>
    Discard,
    /// <summary>지금은 아무것도 하지 않는다. 사본은 남는다.</summary>
    Keep,
};

/// <summary>
/// 사본의 시각을 사람이 읽는 글로 적는다. 어느 것이 언제 남은 것인지가 되살릴지 정하는
/// 유일한 단서이므로, 묻는 자리에 늘 함께 선다.
/// </summary>
/// <param name="writeTime">파일이 써진 시각이다.</param>
/// <returns>"2026-09-02 19:26" 꼴이다. 시각을 읽지 못하면 빈 글이다.</returns>

/// <summary>
/// 대화상자가 낸 답을 사본에 대한 결정으로 옮긴다. 답이 없다는 것 — 취소, Esc, 닫기 상자,
/// 그리고 창을 닫으려 해서 물음이 밖에서 끝난 경우 — 은 모두 「나중에 정하기」다.
///
/// 답 없음을 버리기로 읽으면 사람이 고르지 않은 삭제가 일어나고, 되살리기로 읽으면 고르지
/// 않은 편집이 일어난다. 아무것도 하지 않는 것만이 답을 듣지 못했을 때 옳다.
/// </summary>
/// <param name="chosen">고른 버튼의 첨자다. 답이 없으면 비어 있다.</param>
/// <param name="restoreIndex">되살리기 버튼의 첨자다.</param>
/// <param name="discardIndex">버리기 버튼의 첨자다.</param>
/// <returns>사본에 대한 결정이다.</returns>
[[nodiscard]] RecoveryChoice ChoiceFromDialogAnswer(
    std::optional<std::size_t> chosen, std::size_t restoreIndex, std::size_t discardIndex);
[[nodiscard]] std::string FormatSnapshotTime(std::filesystem::file_time_type writeTime);

/// <summary>
/// 사본 하나에 대한 답을 수행한다.
///
/// 되살리기는 사본을 지우지 않고 버리기만 지운다 — 되살린 뒤 저장하기 전에 또 사고가 나면
/// 그 파일이 다시 유일한 되돌릴 길이다. 되살리기가 실패해도 지우지 않는다: 실패한 뒤 파일까지
/// 없으면 사람은 두 번 잃는다.
/// </summary>
/// <param name="snapshot">답을 받은 사본이다.</param>
/// <param name="choice">사람이 고른 답이다.</param>
/// <param name="restore">사본을 되살리고 성공 여부를 답하는 것이다.</param>
/// <returns>되살렸으면 true다.</returns>
bool ApplyRecoveryChoice(
    const RecoverySnapshot& snapshot, RecoveryChoice choice,
    const std::function<bool(const RecoverySnapshot&)>& restore);

/// <summary>
/// 이 프로젝트의 이 장면이 쓸 복구 파일 경로다. 쓰는 쪽과 읽는 쪽이 같은 규칙을 보게
/// 하려고 여기 하나로 둔다 — 규칙이 두 벌이면 쓴 파일을 못 찾는 것이 조용한 실패가 된다.
/// </summary>
/// <param name="projectFilePath">.gameproject 파일의 경로다.</param>
/// <param name="sceneId">프로젝트 안에서의 장면 id다.</param>
/// <param name="recoveryDirectory">사본들이 사는 디렉터리다.</param>
/// <returns>복구 파일 경로다. 디렉터리가 비어 있으면 빈 경로다.</returns>
[[nodiscard]] std::filesystem::path MakeRecoveryPath(
    const std::filesystem::path& projectFilePath, unsigned int sceneId,
    const std::filesystem::path& recoveryDirectory);

/// <summary>프로젝트 경로로 구분된 디렉터리에 소유 정보를 확인하고 장면 사본을 쓴다.</summary>
[[nodiscard]] std::filesystem::path WriteRecoveryFile(
    const std::filesystem::path& projectFilePath, unsigned int sceneId, std::string_view sceneText,
    const std::filesystem::path& recoveryDirectory);

/// <summary>이 사본의 소유 정보와 장면 번호가 현재 프로젝트와 일치하는지 확인한다.</summary>
[[nodiscard]] bool RecoveryFileBelongsToProject(
    const std::filesystem::path& filePath, const std::filesystem::path& projectFilePath,
    unsigned int sceneId);

/// <summary>
/// 복구 파일 이름을 되읽는다. <c>MakeRecoveryPath</c>가 만든 이름만 참이며, 그렇지 않은
/// 파일은 남의 것이므로 건드리지 않는다.
/// </summary>
/// <param name="fileName">파일 이름이다(디렉터리 없이).</param>
/// <param name="projectStem">읽어 낸 프로젝트 이름을 담는다.</param>
/// <param name="sceneId">읽어 낸 장면 id를 담는다.</param>
/// <returns>이 형식이면 true다.</returns>
[[nodiscard]] bool ParseRecoveryFileName(
    const std::filesystem::path& fileName, std::string& projectStem, unsigned int& sceneId);

/// <summary>
/// 복구 디렉터리에 남아 있는 사본 전부다. 이름이 형식에 맞지 않는 파일은 건너뛴다.
/// 최근에 써진 것이 앞에 온다.
/// </summary>
/// <param name="recoveryDirectory">사본들이 사는 디렉터리다.</param>
/// <returns>사본 목록이다. 디렉터리가 없으면 비어 있다.</returns>
[[nodiscard]] std::vector<RecoverySnapshot> CollectRecoverySnapshots(
    const std::filesystem::path& recoveryDirectory);

/// <summary>
/// 목록에서 이 프로젝트가 남긴 것만 고른다. 프로젝트를 열 때 물을 대상이 이것이다.
/// </summary>
/// <param name="snapshots">전체 목록이다.</param>
/// <param name="projectFilePath">.gameproject 파일의 경로다.</param>
/// <returns>이 프로젝트의 사본들이다.</returns>
[[nodiscard]] std::vector<RecoverySnapshot> FindSnapshotsForProject(
    const std::vector<RecoverySnapshot>& snapshots, const std::filesystem::path& projectFilePath);

/// <summary>
/// 목록에서 가리키는 프로젝트가 더 이상 없는 것들이다. 남아 쌓이기만 하는 파일이라
/// 사람이 날짜를 보고 지울 수 있어야 한다.
/// </summary>
/// <param name="snapshots">전체 목록이다.</param>
/// <param name="projectExists">프로젝트 이름을 받아 그것이 아직 있는지 답하는 것이다.</param>
/// <returns>주인이 없는 사본들이다.</returns>
[[nodiscard]] std::vector<RecoverySnapshot> FindOrphanSnapshots(
    const std::vector<RecoverySnapshot>& snapshots,
    const std::function<bool(const std::string&)>& projectExists);

/// <summary>
/// 사본 하나를 지운다. 실패해도 로그를 남긴다 — 지운 줄 알고 다음에 또 물으면 사람은
/// 같은 질문을 영영 받는다.
/// </summary>
/// <param name="filePath">복구 파일 경로다.</param>
/// <returns>지웠거나 이미 없으면 true다.</returns>
bool DeleteRecoverySnapshot(const std::filesystem::path& filePath);

}
