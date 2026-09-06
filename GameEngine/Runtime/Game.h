#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>

#include "../Platform/IContentSource.h"
#include "SceneLoader.h"

namespace GameEngine::Assets
{
class AssetDatabase;
}

namespace GameEngine::Platform
{
class IAudioOutput;
class ITextMeasure;
}

namespace GameEngine::Runtime
{

class AudioSystem;
class Physics2DSystem;
class Physics3DSystem;
class UILayoutSystem;
class UIEventSystem;
class Input;
class Object;
class ObjectRegistry;
class RuntimeContext;
class Scene;
class SceneManager;
enum class SceneLoadResult;

/// <summary>
/// 객체 레지스트리와 활성 장면을 소유하며 게임 런타임의 초기화와 업데이트를 관리한다.
/// </summary>
class Game final
{
public:
    /// <summary>
    /// 이 런타임이 소리를 낼 출력을 받는다. 런타임은 플랫폼 구현을 이름 부르지 않는다: 무엇을
    /// 쓸지 고르는 일은 합성 루트의 것이고, 여기서 정적으로 집어 오면 소리 없는 런타임을 세우는
    /// 것도 테스트가 가짜를 꽂는 것도 우회로가 필요해진다.
    /// </summary>
    /// <param name="audioOutput">
    /// 초기화되지 않은 오디오 출력이다. 실제 장치는 첫 재생까지 열리지 않으므로, 소리 낼 일 없는
    /// 런타임은 여기에 무엇이 오든 장치를 열지 않는다. null이면 이 런타임은 소리를 내지 않는다.
    /// </param>
    /// <param name="textMeasure">
    /// 글자 크기를 답하는 잣대다. UI 배치가 요소의 선호 크기를 물을 때 쓰이며, 폰트 스택은
    /// 첫 질문까지 열리지 않는다. null이면 내용에 맞추는 UI 요소가 선언된 최소 크기만 지킨다.
    /// </param>
    Game(
        std::unique_ptr<Platform::IAudioOutput> audioOutput,
        std::unique_ptr<Platform::ITextMeasure> textMeasure);
    ~Game();

    /// <summary>프로젝트의 콘텐츠 소스와 사용할 장면 파일 목록을 등록한다.</summary>
    /// <param name="projectContent">
    /// 이 게임의 파일들. 디렉터리일 수도, 실행 파일 안에 packed된 것일 수도 있다. 소유하지 않으므로
    /// 이 Game보다 오래 살아야 한다.
    /// </param>
    /// <param name="scenePaths">장면 ID와 프로젝트 상대 장면 파일 경로의 대응표이다.</param>
    /// <returns>에셋과 모든 장면 경로가 유효하면 true이다.</returns>
    bool Initialize(
        const Platform::IContentSource& projectContent,
        const std::unordered_map<unsigned int,
        std::filesystem::path>& scenePaths);

    /// <summary>활성 장면의 게임 오브젝트를 한 프레임 업데이트한다.</summary>
    /// <param name="deltaTime">이전 프레임부터 흐른 초 단위 시간이다.</param>
    /// <summary>장면 파일 로더를 둔다. 파일 형식은 런타임 위의 것이라 밖에서 받는다.</summary>
    void SetSceneLoader(SceneLoader loader);

    void Update(float deltaTime);

    /// <summary>활성 장면의 모든 카메라를 현재 렌더링 영역 비율과 동기화한다.</summary>
    void SetRenderAspectRatio(float aspectRatio);

    /// <summary>가장 최근에 동기화한 렌더링 영역 비율이다. 카메라가 없는 장면의 기본 투영에 사용한다.</summary>
    [[nodiscard]] float GetRenderAspectRatio() const { return mRenderAspectRatio; }

    /// <summary>
    /// 이 런타임이 그려지는 면의 픽셀 크기를 알린다. 화면 공간 UI 계층의 바깥 사각형이 이것이다.
    ///
    /// 비율과 따로 받는 이유는 UI가 비율이 아니라 픽셀을 필요로 하기 때문이다 — 8픽셀 여백은
    /// 창이 어떤 모양이든 8픽셀이다. 픽셀 두 개로 받는 것은 런타임이 렌더링 계층의 타입을 알지
    /// 않기 위해서다.
    /// </summary>
    /// <param name="width">그리는 면의 가로 픽셀 수이다.</param>
    /// <param name="height">그리는 면의 세로 픽셀 수이다.</param>
    void SetRenderSurfaceSize(const float width, const float height)
    {
        mRenderSurfaceWidth = width;
        mRenderSurfaceHeight = height;
    }

    /// <summary>그리는 면의 가로 픽셀이다. 배치가 이 폭 안에서 자리를 잡는다.</summary>
    [[nodiscard]] float GetRenderSurfaceWidth() const { return mRenderSurfaceWidth; }

    /// <summary>그리는 면의 세로 픽셀이다.</summary>
    [[nodiscard]] float GetRenderSurfaceHeight() const { return mRenderSurfaceHeight; }

    /// <summary>
    /// 글자 폭을 답하는 잣대다. 없을 수 있다 — 창 없는 실행이 그렇다. 배치가 쓰는 것과 같은
    /// 것을 내주는 이유는, 스스로 폭을 셈해야 하는 UI가 배치와 다른 답을 얻으면 그린 것과
    /// 잰 것이 어긋나기 때문이다.
    /// </summary>
    [[nodiscard]] Platform::ITextMeasure* GetTextMeasure() const { return mTextMeasure.get(); }

    /// <summary>이 런타임의 레지스트리에서 인스턴스 ID에 해당하는 살아 있는 객체를 찾는다.</summary>
    [[nodiscard]] Object* FindObject(unsigned int instanceId) const;

    /// <summary>등록된 장면 파일을 읽어 활성 장면으로 추가한다.</summary>
    /// <param name="sceneId">불러올 장면의 ID이다.</param>
    /// <returns>즉시 완료, 지연 예약 또는 실패를 구분하는 결과이다.</returns>
    [[nodiscard]] SceneLoadResult LoadScene(unsigned int sceneId);

    /// <summary>
    /// 이미 만들어진 장면을 활성 목록에 넣고, 그 컴포넌트들이 참조하는 에셋을 미리 로드한 뒤,
    /// 장면이 살게 된 id를 반환한다. 실패하면 0이다.
    ///
    /// 에디터가 연 프로젝트의 장면이 이 길로 들어온다: 그 장면의 파일은 등록된 장면 경로 밖에
    /// 있을 수 있고, 프로젝트가 적어 둔 id는 이미 차지되어 있을 수 있다.
    /// </summary>
    [[nodiscard]] unsigned int AddScene(std::unique_ptr<Scene> scene);

    /// <summary>활성 장면을 제거하고 소유한 객체를 파괴한다.</summary>
    /// <param name="sceneId">내릴 장면의 ID이다.</param>
    /// <returns>활성 장면을 찾아 제거했으면 true이다.</returns>
    bool UnloadScene(unsigned int sceneId);

    /// <summary>
    /// 지정 장면만 남긴다. 제거 콜백은 장면 집합을 바꿀 수 없으며, 성공 후 종료된 장면의
    /// 오디오와 미사용 에셋을 즉시 정리한다. 실패하면 활성 장면은 그대로다.
    /// </summary>
    [[nodiscard]] bool RetainOnlyScene(unsigned int sceneId);

    [[nodiscard]] SceneManager& GetSceneManager() { return *mSceneManager; }
    [[nodiscard]] const SceneManager& GetSceneManager() const { return *mSceneManager; }

    /// <summary>
    /// 이 런타임이 쓰는 2D 물리 시스템이다. 앱 조립부가 전역 중력 같은 장면 공통 설정을 한 번
    /// 정할 때 쓴다. 컴포넌트는 이를 직접 소유하지 않고 SceneManager를 통해 수집된다.
    /// </summary>
    [[nodiscard]] Physics2DSystem& GetPhysics2DSystem() { return *mPhysics2DSystem; }
    [[nodiscard]] const Physics2DSystem& GetPhysics2DSystem() const { return *mPhysics2DSystem; }

    /// <summary>
    /// 이 런타임이 쓰는 3D 물리 시스템이다. 2D 물리와 같은 Game이 소유하되, 각 차원의
    /// 콜라이더와 몸체는 서로 만나지 않는다.
    /// </summary>
    [[nodiscard]] Physics3DSystem& GetPhysics3DSystem() { return *mPhysics3DSystem; }
    [[nodiscard]] const Physics3DSystem& GetPhysics3DSystem() const { return *mPhysics3DSystem; }

    [[nodiscard]] Assets::AssetDatabase& GetAssetDatabase() { return *mAssetDatabase; }
    [[nodiscard]] const Assets::AssetDatabase& GetAssetDatabase() const { return *mAssetDatabase; }

    /// <summary>
    /// 이번 프레임에 키보드와 마우스가 한 일이다. Unity처럼 프로세스 전역 `Input`을 내놓는 대신
    /// 런타임이 이것을 소유하는데, `ObjectRegistry`가 전역이 아니라 소유되는 것과 같은 이유다:
    /// 한 프로세스의 두 런타임 — 편집하는 프로젝트 옆에서 자기 것을 돌리는 에디터가 바로 그것이다
    /// — 이 하나를 공유해서는 안 된다.
    /// </summary>
    [[nodiscard]] Input& GetInput() { return *mInput; }
    [[nodiscard]] const Input& GetInput() const { return *mInput; }

    /// <summary>
    /// 이번 프레임의 포인터를 이 런타임의 UI가 가져갔는지다.
    ///
    /// 같은 클릭을 두 체계가 처리하는 것을 막는 자리다: 유지 모드 UI 위에서 눌린 클릭은 그
    /// 위에 얹힌 즉시 모드 UI의 것이 아니고, 그 반대도 아니다. 판정은 UI 요소가 커서 아래에
    /// 있거나 눌림을 잡고 있을 때만 참이므로, 애매하면 거짓으로 기운다 — 거짓의 대가는 한
    /// 클릭이 두 곳에 가는 것이고, 참의 대가는 위에 있는 UI 전체가 눌리지 않는 것이다.
    /// </summary>
    [[nodiscard]] bool DidUIConsumePointer() const { return mUIConsumedPointer; }

    /// <summary>이 런타임이 장면의 컴포넌트들에게 허락하는 것들이다.</summary>
    [[nodiscard]] RuntimeContext& GetRuntimeContext() { return *mRuntimeContext; }

    /// <summary>
    /// 어느 로드된 장면도 더는 참조하지 않는 모든 로드된 에셋을 내린다. 장면이 내려갈 때와 그
    /// 이후 주기적으로 스스로 돈다. 로딩 화면이 있는 게임이라면 거기서 호출해도 된다.
    /// </summary>
    void UnloadUnusedAssets();

private:
    std::unique_ptr<Assets::AssetDatabase> mAssetDatabase;
    std::unique_ptr<Input> mInput;
    std::unique_ptr<ObjectRegistry> mObjectRegistry;
    std::unique_ptr<RuntimeContext> mRuntimeContext;
    std::unique_ptr<SceneManager> mSceneManager;
    /// <summary>AudioSource들의 상태를 소리로 만드는 시스템이다. Update가 동기화를 돌린다.</summary>
    std::unique_ptr<AudioSystem> mAudioSystem;
    /// <summary>UI 계층의 사각형을 화면 크기에 맞추는 시스템이다. Update가 돌린다.</summary>
    std::unique_ptr<UILayoutSystem> mUILayoutSystem;
    /// <summary>고정 스텝 몸체와 프레임 마지막의 콜라이더 겹침을 함께 맡는 시스템이다.</summary>
    std::unique_ptr<Physics2DSystem> mPhysics2DSystem;
    /// <summary>3D 고정 스텝 몸체와 3D 콜라이더 겹침을 함께 맡는 시스템이다.</summary>
    std::unique_ptr<Physics3DSystem> mPhysics3DSystem;
    /// <summary>글자 크기를 답하는 잣대다. 배치와, 앞으로 글자를 다루는 시스템들이 함께 쓴다.</summary>
    std::unique_ptr<Platform::ITextMeasure> mTextMeasure;
    /// <summary>배치된 사각형에 커서를 맞혀 버튼에 반응을 나눠 주는 시스템이다.</summary>
    std::unique_ptr<UIEventSystem> mUIEventSystem;
    float mRenderAspectRatio = 1.0f;
    /// <summary>이번 프레임에 이 런타임의 UI가 포인터를 가져갔는지다. 매 업데이트에 다시 정해진다.</summary>
    bool mUIConsumedPointer = false;
    float mRenderSurfaceWidth = 0.0f;
    float mRenderSurfaceHeight = 0.0f;

    bool LoadAssets(const Platform::IContentSource& source);

    /// <summary>장면의 컴포넌트들이 참조하는 모든 에셋을, 무엇이 그리기 전에 로드한다.</summary>
    void LoadSceneAssets(const Scene& scene);

    /// <summary>미사용 에셋 청소가 도는 주기이다. 초 단위다.</summary>
    static constexpr float UnusedAssetSweepInterval = 5.0f;

    float mUnusedAssetSweepTime = 0.0f;
};

}
