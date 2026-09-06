#pragma once

// editor-layer: 2 (Views)

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/Asset.h"
#include "Assets/AssetReference.h"
#include "Assets/MaterialData.h"
#include "Core/ChoiceModel.h"
#include "Math/Color.h"
#include "Math/Vector.h"
#include "Runtime/PropertyDescriptor.h"
#include "Core/Json.h"
#include "Serialization/ComponentSchema.h"
#include "Serialization/PreservedComponent.h"
#include "UI/UIContext.h"
#include "Rules/EditorPanelHosts.h"

namespace GameEngine::Runtime
{
class Component;
class GameObject;
}

namespace GameEngine::Assets
{
class AssetDatabase;
}

namespace GameEditor
{

class EditorContext;


/// <summary>
/// 인스펙터 패널이다: 선택된 GameObject의 이름·활성과 컴포넌트들의 속성 서술을 위젯 행으로
/// 그린다. 칸 텍스트, Add Component 목록의 펼침, 지연 제거 대상 같은 편집 상태를 자신이
/// 소유하고, 모든 속성 쓰기는 셸의 ApplyProperty — undo가 걸리는 길목 — 로 보낸다.
/// </summary>
class EditorInspectorPanel final
{
public:
    EditorInspectorPanel(IEditorScale& scale, IPropertyEditHost& propertyEdit, IAssetDragHost& assetDrag, EditorContext& context, GameEngine::UI::UIContext& ui);

    /// <summary>패널 내용을 그린다. 제목줄 프레임은 셸이 이미 그렸고, 그 아래 영역을 받는다.</summary>
    void Draw(GameEngine::UI::UIRect content);

    /// <summary>
    /// 모든 칸 텍스트를 버린다. 장면 상태가 위젯 밑에서 바뀌었을 때 — undo/redo, 객체 삭제 —
    /// 셸이 부른다. 남은 텍스트는 이전 값의 것이라 다음에 그 id를 얻는 위젯의 것이 아니다.
    /// </summary>
    void ClearFieldTexts() { mFieldTexts.clear(); }

private:
    /// <summary>96 DPI 기준의 논리 길이를 이 화면의 픽셀로 바꾼다. 셸의 배율을 따른다.</summary>
    [[nodiscard]] float S(float logical) const;

    /// <summary>
    /// 고른 GameObject가 없고 콘텐츠 브라우저에서 고른 에셋이 있을 때 보이는 내용이다. 종류에
    /// 맞는 설정 UI가 있으면(지금은 Sprite의 시트뿐이다) 그것을 그리고, 없으면 편집할 것이
    /// 없다는 문구만 보인다.
    /// </summary>
    void DrawAssetInspector(
        const GameEngine::UI::UIRect& content, const std::filesystem::path& assetPath);
    /// <summary>
    /// 스프라이트의 시트(columns·rows·frameCount·frameRate) 네 칸이다. 값을 바꾸면
    /// WriteSpriteSheet가 사이드카에 바로 쓴다 — 씬 저장(Ctrl+S)과는 별개 흐름이고, undo도
    /// 걸리지 않는다: 이것은 장면 상태가 아니라 에셋의 임포트 설정이다.
    /// </summary>
    void DrawSpriteSheetSettings(
        const GameEngine::UI::UIRect& content, float& y, const GameEngine::Assets::Sprite& sprite);
    /// <summary>
    /// 머티리얼의 texture(에셋 참조 드롭다운)·tint(색 편집) 두 칸이다. 값을 바꾸면 WriteMaterial이
    /// 그 <c>.material</c> 파일에 바로 쓴다 — 스프라이트의 시트와 같은 이유로 undo가 걸리지
    /// 않는다: 장면 상태가 아니라 에셋 자신의 내용이다.
    /// </summary>
    void DrawMaterialSettings(
        const GameEngine::UI::UIRect& content, float& y,
        const GameEngine::Assets::AssetDatabase& database,
        const GameEngine::Assets::Material& material);
    void DrawComponentInspector(
        GameEngine::Runtime::Component& component, const GameEngine::UI::UIRect& content, float& y);
    /// <summary>인스펙터 끝의 "Add Component" 버튼과, 펼쳐졌을 때의 컴포넌트 목록이다.</summary>
    void DrawAddComponent(
        GameEngine::Runtime::GameObject& gameObject, const GameEngine::UI::UIRect& content, float& y);
    /// <summary>
    /// 목록에서 고른 타입을 객체에 붙인다. prototype이 null이면 이 프로세스가 만들 수 있는
    /// 타입을 기본값으로 만들고, 아니면 그 JSON 그대로 — 게임 컴포넌트의 스키마 기본값이다.
    /// </summary>
    void AddComponentOfType(
        GameEngine::Runtime::GameObject& gameObject, std::string typeName,
        GameEngine::Core::Json prototype);
    /// <summary>레이블 하나와 값 칸 count개로 이뤄진 속성 행의 칸 사각형들을 만든다.</summary>
    void LayoutPropertyRow(
        const GameEngine::UI::UIRect& content, float y, const char* label, int count,
        std::vector<GameEngine::UI::UIRect>& cells);
    [[nodiscard]] bool IsRowVisible(const GameEngine::UI::UIRect& content, float y) const;
    /// <summary>실수 한 칸. 온전한 숫자가 입력됐을 때만 apply가 불린다.</summary>
    void DrawNumberCell(
        GameEngine::UI::WidgetId id, const GameEngine::UI::UIRect& rect, float value,
        const std::function<void(float)>& apply);
    void DrawStringCell(
        GameEngine::UI::WidgetId id, const GameEngine::UI::UIRect& rect, const std::string& value,
        const std::function<void(const std::string&)>& apply);
    /// <summary>두 상태를 오가는 버튼. 현재 상태의 레이블을 보이고, 누르면 반대로 간다.</summary>
    void DrawToggleCell(
        GameEngine::UI::WidgetId id, const GameEngine::UI::UIRect& rect, bool value,
        const char* onLabel, const char* offLabel, const std::function<void(bool)>& apply);
    /// <summary>
    /// 속성 서술 하나를 종류에 맞는 위젯 행으로 그린다.
    /// </summary>
    void DrawPropertyRow(
        GameEngine::Runtime::Component& component,
        const GameEngine::Runtime::PropertyDescriptor& descriptor,
        const GameEngine::UI::UIRect& content, float& y);
    /// <summary>
    /// 지금 끌고 있는 에셋을 이 종류의 칸이 받을 수 있는지다. 종류를 말하지 않는 칸은 무엇이든
    /// 받고, 말하는 칸은 그 종류의 에셋만 받는다 — 목록을 거르는 규칙과 같은 규칙이다.
    /// </summary>
    /// <param name="assetType">받는 칸이 원하는 에셋 종류다.</param>
    [[nodiscard]] bool AcceptsDraggedAsset(
        std::optional<GameEngine::Assets::AssetType> assetType) const;
    /// <summary>
    /// 에셋 참조 한 줄이다. 머리 칸이 지금 값을 보이고, 누르면 그 아래로 이 속성이 원하는 종류의
    /// 에셋 목록이 펼쳐진다. 항목을 누르면 그것이 값이 되고 목록은 접힌다. 값이 이 프로젝트에서
    /// 해석되지 않으면 그 사실을 칸 옆에 적는다.
    /// </summary>
    /// <param name="assetType">목록에 보일 에셋 종류다. 비어 있으면 모든 종류다.</param>
    void DrawAssetReferenceRow(
        const GameEngine::UI::UIRect& content, float& y, const std::string& label,
        GameEngine::UI::WidgetId id, const GameEngine::Assets::AssetReference& value,
        std::optional<GameEngine::Assets::AssetType> assetType,
        const std::function<void(const GameEngine::Assets::AssetReference&)>& apply);
    /// <summary>
    /// 값 하나를 그 종류에 맞는 위젯 행으로 그린다. 값이 어디서 왔는지는 묻지 않는다: 살아 있는
    /// 컴포넌트의 속성 서술에서 온 값과, 게임 컴포넌트의 보존 JSON에서 온 값이 같은 행을 쓴다 —
    /// 종류마다의 위젯 선택이 두 벌이 되면 언젠가 한 벌만 고쳐진다.
    /// </summary>
    /// <param name="apply">사람이 값을 바꿨을 때 불린다. 기록은 호출자의 몫이다.</param>
    void DrawValueRow(
        const GameEngine::UI::UIRect& content, float& y, const std::string& label,
        GameEngine::UI::WidgetId id, GameEngine::Runtime::PropertyKind kind,
        std::span<const std::string> enumNames,
        std::optional<GameEngine::Assets::AssetType> assetType,
        const GameEngine::Runtime::PropertyValue& value,
        const std::function<void(const GameEngine::Runtime::PropertyValue&)>& apply);
    /// <summary>
    /// 이 프로세스에 실행 코드가 없는 게임 컴포넌트의 속성 행이다.
    /// 보존 JSON에서 값을 읽고, 그 멤버를 바꾸는 커맨드로 편집을 기록한다.
    /// </summary>
    void DrawSchemaPropertyRow(
        GameEngine::Serialization::PreservedComponent& component,
        const GameEngine::Serialization::ComponentSchemaProperty& property,
        const GameEngine::UI::UIRect& content, float& y);
    /// <summary>셸의 ApplyProperty — undo가 걸리는 길목 — 에 이 패널의 병합 키를 실어 보낸다.</summary>
    void ApplyProperty(
        GameEngine::Runtime::Component& component,
        const GameEngine::Runtime::PropertyDescriptor& descriptor,
        const GameEngine::Runtime::PropertyValue& value);
    /// <summary>
    /// 지금 적용 중인 편집의 병합 키다. 텍스트 필드에서 온 편집(mMergeSourceField가 그 필드)만
    /// 0이 아니고, 그 필드의 포커스가 이어지는 동안 같은 값이다 — 연속 타이핑이 한 undo가 되고,
    /// 필드를 떠났다 돌아온 편집은 새 단계가 되는 경계가 이 키다.
    /// </summary>
    [[nodiscard]] std::uint64_t CurrentMergeKey() const;
    void DrawFloatRow(
        const GameEngine::UI::UIRect& content, float& y, const char* label,
        GameEngine::UI::WidgetId id, float value, const std::function<void(float)>& apply);
    void DrawVector2Row(
        const GameEngine::UI::UIRect& content, float& y, const char* label,
        GameEngine::UI::WidgetId id, const GameEngine::Math::Vector2& value,
        const std::function<void(const GameEngine::Math::Vector2&)>& apply);
    void DrawVector3Row(
        const GameEngine::UI::UIRect& content, float& y, const char* label,
        GameEngine::UI::WidgetId id, const GameEngine::Math::Vector3& value,
        const std::function<void(const GameEngine::Math::Vector3&)>& apply);
    void DrawColorRow(
        const GameEngine::UI::UIRect& content, float& y, const char* label,
        GameEngine::UI::WidgetId id, const GameEngine::Math::Color& value,
        const std::function<void(const GameEngine::Math::Color&)>& apply);
    void DrawStringRow(
        const GameEngine::UI::UIRect& content, float& y, const char* label,
        GameEngine::UI::WidgetId id, const std::string& value,
        const std::function<void(const std::string&)>& apply);
    void DrawToggleRow(
        const GameEngine::UI::UIRect& content, float& y, const char* label,
        GameEngine::UI::WidgetId id, bool value, const char* onLabel, const char* offLabel,
        const std::function<void(bool)>& apply);

    IEditorScale& mScale;
    IPropertyEditHost& mPropertyEdit;
    IAssetDragHost& mAssetDrag;
    EditorContext& mContext;
    GameEngine::UI::UIContext& mUI;

    // 인스펙터의 편집 상태: 어떤 객체가 보이고, 각 칸에 무엇이 보이는지. 포커스가 없는 칸은
    // 매 프레임 값으로 다시 채워지고, 포커스가 있는 칸만 사람의 텍스트를 지킨다.
    unsigned int mInspectedInstanceId = 0;
    /// <summary>지금 인스펙터가 보이는 에셋이다. 고른 것이 없으면 비어 있다.</summary>
    std::filesystem::path mInspectedAssetPath;
    /// <summary>
    /// 마지막으로 디스크에 쓴 시트다. 텍스트 칸의 apply는 온전한 숫자마다 불리므로, 이것과
    /// 같은 값을 다시 쓰지 않아야 한 자리 입력할 때마다 사이드카를 다시 쓰는 일이 없다.
    /// </summary>
    std::optional<GameEngine::Assets::Sprite::Sheet> mLastWrittenSpriteSheet;
    /// <summary>마지막으로 디스크에 쓴 머티리얼 값이다. 위 시트와 같은 이유로 필요하다.</summary>
    std::optional<GameEngine::Assets::MaterialData> mLastWrittenMaterial;
    /// <summary>인스펙터 아래의 컴포넌트 목록이 펼쳐져 있는지다. 객체가 바뀌면 접힌다.</summary>
    bool mAddComponentListOpen = false;
    /// <summary>지금 목록이 펼쳐진 에셋 참조 칸이다. 펼쳐진 것이 없으면 0이다.</summary>
    GameEngine::UI::WidgetId mOpenAssetChooser = 0;
    /// <summary>펼쳐진 에셋 목록의 상태다. 유지 모드 Dropdown과 같은 규칙을 쓴다.</summary>
    GameEngine::Core::ChoiceModel mAssetChoice;
    /// <summary>
    /// 이번 프레임에 헤더의 Remove가 눌린 컴포넌트다. 컴포넌트를 걷는 도중에 지우면 걷고 있는
    /// 목록이 무너지므로, 걷기가 끝난 뒤 인스펙터가 지운다.
    /// </summary>
    GameEngine::Runtime::Component* mComponentToRemove = nullptr;
    std::unordered_map<GameEngine::UI::WidgetId, std::string> mFieldTexts;
    /// <summary>지난 프레임 인스펙터 내용의 높이다. 스크롤 범위가 이것으로 정해진다.</summary>
    float mInspectorContentHeight = 0.0f;
    /// <summary>지금 apply를 부르고 있는 텍스트 필드다. 필드 밖의 편집 중에는 0이다.</summary>
    GameEngine::UI::WidgetId mMergeSourceField = 0;
};

}
