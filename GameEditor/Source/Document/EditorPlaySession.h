#pragma once

// editor-layer: 1 (Document)

#include <string>

namespace GameEditor
{

/// <summary>
/// 플레이 중인지, 그리고 플레이에 들어갈 때 떠 둔 장면이 무엇인지다.
///
/// 이탈이 장면을 되돌릴 수 있는 것은 진입 시점의 텍스트를 들고 있기 때문이다. 그 텍스트가
/// 곧 「사람이 편집한 상태」이기도 하다 — 플레이 중의 런타임은 스크립트가 만든 것이라 지킬
/// 작업물이 아니므로, 사고 사본도 플레이 중에는 이 스냅숏을 쓴다.
///
/// 플레이 표시, 입력 포획, 진입 스냅숏은 이 부품이 소유한다.
/// <see cref="EditorContext"/>는 플레이 상태 관련 호출을 이 부품에 전달한다.
///
/// <b>장면을 실제로 내리고 다시 올리는 일은 여기 없다.</b> 그것은 런타임과 프로젝트 설정을
/// 모두 알아야 하는 일이라 문맥의 몫이고, 이 부품이 답하는 것은 「지금 플레이 중인가」와
/// 「되돌릴 텍스트는 무엇인가」 둘뿐이다.
/// </summary>
class EditorPlaySession final
{
public:
    /// <summary>지금 플레이 중인지다.</summary>
    [[nodiscard]] bool IsPlaying() const { return mIsPlaying; }

    /// <summary>플레이 중인 게임이 키와 마우스를 받고 있는지다.</summary>
    [[nodiscard]] bool IsInputCaptured() const { return mInputCaptured; }

    /// <summary>입력 포획을 켜거나 끈다.</summary>
    void SetInputCaptured(const bool captured) { mInputCaptured = captured; }

    /// <summary>
    /// 진입 시점의 장면 텍스트를 들고 플레이를 시작한다. 이미 플레이 중이면 거짓이고 아무것도
    /// 바꾸지 않는다.
    /// </summary>
    /// <param name="sceneSnapshot">진입 시점의 장면을 직렬화한 텍스트다.</param>
    [[nodiscard]] bool Begin(std::string sceneSnapshot);

    /// <summary>
    /// 플레이를 끝내고 진입 시점의 텍스트를 넘긴다. 문서는 복원 성공 후에만 호출한다.
    /// 복원 시도 중에는 GetSnapshot을 사용해 실패 후 재시도할 수 있는 원본을 보존한다.
    /// </summary>
    [[nodiscard]] std::string End();

    /// <summary>
    /// 진입 시점의 장면 텍스트다. 플레이 중이 아니면 비어 있다. 사고 사본이 플레이 중에 읽는
    /// 것이 이것이다.
    /// </summary>
    [[nodiscard]] const std::string& GetSnapshot() const { return mSnapshot; }

    /// <summary>
    /// 편집 대상 문서가 통째로 바뀌었다. 플레이 표시와 스냅숏을 함께 버린다 — 없어진 장면으로
    /// 되돌릴 수는 없다.
    ///
    /// 입력 포획은 건드리지 않는다. 그것은 사람이 게임 뷰를 눌러 잡고 Esc로 놓는 상태라 문서의
    /// 수명과 다른 축에 있다.
    /// </summary>
    void Clear();

private:
    bool mIsPlaying = false;
    bool mInputCaptured = false;
    /// <summary>진입 시점의 장면 텍스트다. 이탈이 이것으로 되돌린다.</summary>
    std::string mSnapshot;
};

}
