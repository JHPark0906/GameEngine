#include "Views/EditorInspectorPanel.h"

#include <cmath>
#include <iterator>
#include <optional>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "Document/EditorCommands.h"
#include "Document/EditorContext.h"
#include "Rules/EditorComponentChoices.h"
#include "Rules/EditorMaterialEditing.h"
#include "Rules/EditorPanelCommon.h"
#include "Rules/EditorSpriteSheetEditing.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetReference.h"
#include "Diagnostics/Debug.h"
#include "Runtime/ComponentType.h"
#include "Runtime/GameObject.h"
#include "Runtime/Transform.h"
#include "Serialization/PreservedComponent.h"

namespace GameEditor
{

namespace
{
    using GameEngine::UI::UIRect;


    /// <summary>
    /// 그 종류의 "아무것도 아닌" 값이다. 스키마에 기본값조차 없는 속성의 행이 무엇을 보일지
    /// 정한다 — 값이 없다고 행을 빼면 사람은 그 속성이 있다는 것조차 알 수 없다.
    /// </summary>
    [[nodiscard]] GameEngine::Runtime::PropertyValue EmptyPropertyValue(
        const GameEngine::Runtime::PropertyKind kind)
    {
        using GameEngine::Runtime::PropertyKind;
        using GameEngine::Runtime::PropertyValue;
        switch (kind)
        {
        case PropertyKind::Bool: return PropertyValue{ false };
        case PropertyKind::Int:
        case PropertyKind::Enum: return PropertyValue{ 0 };
        case PropertyKind::Float: return PropertyValue{ 0.0f };
        case PropertyKind::String: return PropertyValue{ std::string{} };
        case PropertyKind::Vector2: return PropertyValue{ GameEngine::Math::Vector2{} };
        case PropertyKind::Vector3: return PropertyValue{ GameEngine::Math::Vector3{} };
        case PropertyKind::Color: return PropertyValue{ GameEngine::Math::Color{} };
        case PropertyKind::AssetReference:
            return PropertyValue{ GameEngine::Assets::AssetReference{} };
        }
        return PropertyValue{ 0.0f };
    }

    /// <summary>
    /// 타입 이름의 표시형이다. ComponentType 선언의 이름에서 대문자 앞에 공백을 넣는다.
    /// 예를 들어 "MeshRenderer"는 "Mesh Renderer"로 표시한다.
    /// </summary>
    [[nodiscard]] std::string SpaceCamelCase(const std::string_view name)
    {
        std::string display;
        display.reserve(name.size() + 2);
        for (const char character : name)
        {
            if (character >= 'A' && character <= 'Z' && !display.empty())
            {
                display += ' ';
            }
            display += character;
        }
        return display;
    }

    /// <summary>enum 버튼의 레이블이다: 파일 형식의 소문자 이름을 첫 글자만 올린다.</summary>
    [[nodiscard]] std::string CapitalizeFirst(const std::string_view name)
    {
        std::string label(name);
        if (!label.empty() && label[0] >= 'a' && label[0] <= 'z')
        {
            label[0] = static_cast<char>(label[0] - 'a' + 'A');
        }
        return label;
    }

    /// <summary>필드에 표시할 형태의 실수이다. 소수부가 없으면 정수처럼 짧게 쓴다.</summary>
    [[nodiscard]] std::string FormatFieldValue(const float value)
    {
        char buffer[48] = {};
        // 0을 더해 -0을 0으로 만든다. 행렬 분해가 남긴 -0이 칸에 "-0"으로 보일 이유는 없다.
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value + 0.0f));
        return buffer;
    }

    /// <summary>선택 상태에서 인스펙터가 보일 GameObject이다. 컴포넌트 선택이면 그 주인이다.</summary>
    [[nodiscard]] GameEngine::Runtime::GameObject* GetInspectedGameObject(
        GameEngine::Runtime::Object* const selectedObject)
    {
        if (auto* gameObject = dynamic_cast<GameEngine::Runtime::GameObject*>(selectedObject))
        {
            return gameObject;
        }
        if (const auto* component =
                dynamic_cast<const GameEngine::Runtime::Component*>(selectedObject))
        {
            return component->GetGameObject();
        }
        return nullptr;
    }
}

EditorInspectorPanel::EditorInspectorPanel(
    IEditorScale& scale, IPropertyEditHost& propertyEdit, IAssetDragHost& assetDrag, EditorContext& context, GameEngine::UI::UIContext& ui)
    : mScale(scale), mPropertyEdit(propertyEdit), mAssetDrag(assetDrag), mContext(context), mUI(ui)
{
}

float EditorInspectorPanel::S(const float logical) const
{
    return mScale.S(logical);
}

bool EditorInspectorPanel::IsRowVisible(const GameEngine::UI::UIRect& content, const float y) const
{
    return y >= content.y && y + S(RowHeight) <= content.GetBottom();
}

void EditorInspectorPanel::LayoutPropertyRow(
    const GameEngine::UI::UIRect& content, const float y, const char* const label, const int count,
    std::vector<GameEngine::UI::UIRect>& cells)
{
    const float labelWidth = S(InspectorLabelWidth);
    if (IsRowVisible(content, y))
    {
        mUI.DrawLabel({ content.x + S(Padding), y, labelWidth, S(RowHeight) }, label, DimTextColor, SecondaryFontSize);
    }
    const float cellsWidth = (std::max)(content.width - labelWidth - 2.0f * S(Padding), S(60.0f));
    const float cellWidth = cellsWidth / static_cast<float>((std::max)(count, 1));
    cells.clear();
    for (int index = 0; index < count; ++index)
    {
        cells.push_back({
            content.x + S(Padding) + labelWidth + static_cast<float>(index) * cellWidth, y,
            cellWidth - S(2.0f), S(RowHeight) });
    }
}

void EditorInspectorPanel::DrawNumberCell(
    const GameEngine::UI::WidgetId id, const GameEngine::UI::UIRect& rect, const float value,
    const std::function<void(float)>& apply)
{
    std::string& text = mFieldTexts[id];
    if (!mUI.IsFieldFocused(id))
    {
        text = FormatFieldValue(value);
    }
    if (!mUI.DrawTextField(id, rect, text))
    {
        return;
    }
    // 온전한 숫자만 적용한다. "-"나 "1e"처럼 입력 중간의 텍스트는 아직 값이 아니므로, 마저
    // 입력될 때까지 장면을 건드리지 않는다.
    char* parseEnd = nullptr;
    const float parsed = std::strtof(text.c_str(), &parseEnd);
    if (parseEnd != text.c_str() && *parseEnd == '\0' && std::isfinite(parsed))
    {
        // 이 적용이 어느 필드에서 왔는지를 undo 병합이 알도록 남긴다. 키 입력마다 오는 적용들이
        // 포커스가 이어지는 동안 한 undo 단계로 합쳐지는 근거가 이 표시다.
        mMergeSourceField = id;
        apply(parsed);
        mMergeSourceField = 0;
    }
}

void EditorInspectorPanel::DrawStringCell(
    const GameEngine::UI::WidgetId id, const GameEngine::UI::UIRect& rect, const std::string& value,
    const std::function<void(const std::string&)>& apply)
{
    std::string& text = mFieldTexts[id];
    if (!mUI.IsFieldFocused(id))
    {
        text = value;
    }
    if (mUI.DrawTextField(id, rect, text))
    {
        mMergeSourceField = id;
        apply(text);
        mMergeSourceField = 0;
    }
}

void EditorInspectorPanel::DrawToggleCell(
    const GameEngine::UI::WidgetId id, const GameEngine::UI::UIRect& rect, const bool value,
    const char* const onLabel, const char* const offLabel, const std::function<void(bool)>& apply)
{
    if (mUI.DrawButton(id, rect, value ? onLabel : offLabel))
    {
        apply(!value);
    }
}

void EditorInspectorPanel::DrawFloatRow(
    const GameEngine::UI::UIRect& content, float& y, const char* const label,
    const GameEngine::UI::WidgetId id, const float value, const std::function<void(float)>& apply)
{
    std::vector<UIRect> cells;
    LayoutPropertyRow(content, y, label, 1, cells);
    if (IsRowVisible(content, y))
    {
        DrawNumberCell(id, cells[0], value, apply);
    }
    y += S(RowHeight) + S(2.0f);
}

void EditorInspectorPanel::DrawVector2Row(
    const GameEngine::UI::UIRect& content, float& y, const char* const label,
    const GameEngine::UI::WidgetId id, const GameEngine::Math::Vector2& value,
    const std::function<void(const GameEngine::Math::Vector2&)>& apply)
{
    std::vector<UIRect> cells;
    LayoutPropertyRow(content, y, label, 2, cells);
    if (IsRowVisible(content, y))
    {
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 1), cells[0], value.GetX(), [&](const float v) { apply({ v, value.GetY() }); });
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 2), cells[1], value.GetY(), [&](const float v) { apply({ value.GetX(), v }); });
    }
    y += S(RowHeight) + S(2.0f);
}

void EditorInspectorPanel::DrawVector3Row(
    const GameEngine::UI::UIRect& content, float& y, const char* const label,
    const GameEngine::UI::WidgetId id, const GameEngine::Math::Vector3& value,
    const std::function<void(const GameEngine::Math::Vector3&)>& apply)
{
    std::vector<UIRect> cells;
    LayoutPropertyRow(content, y, label, 3, cells);
    if (IsRowVisible(content, y))
    {
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 1), cells[0], value.GetX(), [&](const float v) { apply({ v, value.GetY(), value.GetZ() }); });
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 2), cells[1], value.GetY(), [&](const float v) { apply({ value.GetX(), v, value.GetZ() }); });
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 3), cells[2], value.GetZ(), [&](const float v) { apply({ value.GetX(), value.GetY(), v }); });
    }
    y += S(RowHeight) + S(2.0f);
}

void EditorInspectorPanel::DrawColorRow(
    const GameEngine::UI::UIRect& content, float& y, const char* const label,
    const GameEngine::UI::WidgetId id, const GameEngine::Math::Color& value,
    const std::function<void(const GameEngine::Math::Color&)>& apply)
{
    std::vector<UIRect> cells;
    LayoutPropertyRow(content, y, label, 4, cells);
    if (IsRowVisible(content, y))
    {
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 1), cells[0], value.r, [&](const float v) { apply({ v, value.g, value.b, value.a }); });
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 2), cells[1], value.g, [&](const float v) { apply({ value.r, v, value.b, value.a }); });
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 3), cells[2], value.b, [&](const float v) { apply({ value.r, value.g, v, value.a }); });
        DrawNumberCell(GameEngine::UI::MakeChildWidgetId(id, 4), cells[3], value.a, [&](const float v) { apply({ value.r, value.g, value.b, v }); });
    }
    y += S(RowHeight) + S(2.0f);
}

void EditorInspectorPanel::DrawStringRow(
    const GameEngine::UI::UIRect& content, float& y, const char* const label,
    const GameEngine::UI::WidgetId id, const std::string& value,
    const std::function<void(const std::string&)>& apply)
{
    std::vector<UIRect> cells;
    LayoutPropertyRow(content, y, label, 1, cells);
    if (IsRowVisible(content, y))
    {
        DrawStringCell(id, cells[0], value, apply);
    }
    y += S(RowHeight) + S(2.0f);
}

void EditorInspectorPanel::DrawToggleRow(
    const GameEngine::UI::UIRect& content, float& y, const char* const label,
    const GameEngine::UI::WidgetId id, const bool value, const char* const onLabel,
    const char* const offLabel, const std::function<void(bool)>& apply)
{
    std::vector<UIRect> cells;
    LayoutPropertyRow(content, y, label, 1, cells);
    if (IsRowVisible(content, y))
    {
        DrawToggleCell(id, cells[0], value, onLabel, offLabel, apply);
    }
    y += S(RowHeight) + S(2.0f);
}

void EditorInspectorPanel::DrawComponentInspector(
    GameEngine::Runtime::Component& component, const GameEngine::UI::UIRect& content, float& y)
{
    using namespace GameEngine::Runtime;
    const unsigned int componentId = component.GetInstanceId();
    const auto id = [componentId](const std::string_view name)
    {
        return GameEngine::UI::MakeWidgetId(name, componentId);
    };
    const ComponentType& type = component.GetComponentType();

    // 컴포넌트 제목. 보존된 컴포넌트는 원래 타입 이름으로 보여준다 — 이 프로세스가 만들 수
    // 없을 뿐, 사람이 보기에 그 객체에 붙어 있는 것은 여전히 그 스크립트다. 스키마가 있는
    // 게임 컴포넌트는 편집할 수 있으므로 "게임의 것"이라고만 말하고, 스키마가 없어 데이터로만
    // 실려 있는 것은 그 사실을 이름 옆에 적는다.
    auto* const preserved =
        dynamic_cast<GameEngine::Serialization::PreservedComponent*>(&component);
    const GameEngine::Serialization::ComponentSchema* const schema =
        preserved ? mContext.FindGameComponentSchema(preserved->GetPreservedTypeName()) : nullptr;
    std::string title;
    if (preserved)
    {
        title = "[" + SpaceCamelCase(preserved->GetPreservedTypeName()) +
            (schema ? " (game)]" : " (preserved)]");
    }
    else
    {
        title = "[" + SpaceCamelCase(type.GetName()) + "]";
    }
    y += S(Padding);
    if (IsRowVisible(content, y))
    {
        mUI.DrawPanel({ content.x, y, content.width, S(RowHeight) }, HeaderColor);
        mUI.DrawLabel({ content.x + S(Padding), y, content.width - S(140.0f), S(RowHeight) }, title, TextColor, SecondaryFontSize);
        // enabled는 본문 행이 아니라 헤더의 토글이다: 켜짐은 속성 값이지만, 자리는 제목줄이다.
        if (const PropertyDescriptor* const enabled = FindProperty(type, "enabled");
            enabled && enabled->GetKind() == PropertyKind::Bool)
        {
            DrawToggleCell(id("enabled"), { content.GetRight() - S(72.0f), y, S(68.0f), S(RowHeight) },
                std::get<bool>(enabled->Get(component)), "Enabled", "Disabled",
                [this, &component, enabled](const bool v)
                { ApplyProperty(component, *enabled, PropertyValue{ v }); });
        }
        // Transform은 객체의 일부라 지울 수 없다. 나머지는 헤더에서 지운다.
        if (&type != &Transform::StaticType() &&
            mUI.DrawButton(id("remove"), { content.GetRight() - S(72.0f) - S(4.0f) - S(56.0f), y, S(56.0f), S(RowHeight) }, "Remove"))
        {
            mComponentToRemove = &component;
        }
    }
    y += S(RowHeight) + S(2.0f);

    // 이 프로세스에 등록되지 않은 컴포넌트는 게임 빌드의 스키마로 행을 구성하고,
    // 보존 JSON에서 값을 읽는다.
    if (schema)
    {
        for (const GameEngine::Serialization::ComponentSchemaProperty& property :
             schema->properties)
        {
            DrawSchemaPropertyRow(*preserved, property, content, y);
        }
        return;
    }

    // 타입 사슬의 속성 서술이 곧 인스펙터의 행이다.
    // 새 컴포넌트는 속성을 선언하면 여기에도 나타난다.
    for (const PropertyDescriptor* const descriptor : CollectProperties(type))
    {
        if (descriptor->GetName() == "enabled")
        {
            continue;
        }
        DrawPropertyRow(component, *descriptor, content, y);
    }
}

void EditorInspectorPanel::DrawPropertyRow(
    GameEngine::Runtime::Component& component,
    const GameEngine::Runtime::PropertyDescriptor& descriptor,
    const GameEngine::UI::UIRect& content, float& y)
{
    const GameEngine::UI::WidgetId id =
        GameEngine::UI::MakeWidgetId(descriptor.GetName(), component.GetInstanceId());
    DrawValueRow(
        content, y, std::string(descriptor.GetDisplayName()), id, descriptor.GetKind(),
        descriptor.GetEnumNames(), descriptor.GetAssetType(), descriptor.Get(component),
        [this, &component, &descriptor](const GameEngine::Runtime::PropertyValue& newValue)
        { ApplyProperty(component, descriptor, newValue); });
}

void EditorInspectorPanel::DrawSchemaPropertyRow(
    GameEngine::Serialization::PreservedComponent& component,
    const GameEngine::Serialization::ComponentSchemaProperty& property,
    const GameEngine::UI::UIRect& content, float& y)
{
    using GameEngine::Runtime::PropertyValue;

    // 저장되지 않는 속성은 파일에 없고, 이 컴포넌트는 파일이 전부다.
    if (property.notSerialized)
    {
        return;
    }

    // 값은 실려 온 JSON에서 읽되, 없거나 형태가 다르면 스키마의 기본값으로 떨어진다 — 로더가
    // 없는 멤버에 기본값을 남기는 것과 같은 규칙이라, 인스펙터가 보이는 것이 곧 게임이 읽을
    // 값이다. 기본값마저 없으면 그 종류의 빈 값을 보인다.
    const GameEngine::Core::Json& current = component.GetData().Find(property.name)
        ? component.GetData().At(property.name)
        : property.defaultValue;
    const PropertyValue value = GameEngine::Serialization::PropertyValueFromJson(
        property.kind, current, property.enumNames, EmptyPropertyValue(property.kind));

    const GameEngine::UI::WidgetId id =
        GameEngine::UI::MakeWidgetId(property.name, component.GetInstanceId());
    DrawValueRow(
        content, y, property.displayName, id, property.kind, property.enumNames,
        property.assetType, value,
        [this, &component, &property, before = current](const PropertyValue& newValue)
        {
            auto command = std::make_unique<PreservedPropertyEditCommand>(
                mContext, component.GetInstanceId(), property.name, before,
                GameEngine::Serialization::PropertyValueToJson(
                    property.kind, newValue, property.enumNames),
                CurrentMergeKey());
            if (command->Apply())
            {
                mContext.RecordEdit(std::move(command));
            }
        });
}

namespace
{
    /// <summary>
    /// 에셋 참조 칸의 목록에 놓일 선택지들이다. 종류가 정해진 속성은 그 종류만, 정해지지 않은
    /// 속성은 사람이 가리킬 만한 모든 종류를 보인다.
    /// </summary>
    std::vector<GameEngine::Assets::AssetChoice> CollectChoicesFor(
        const GameEngine::Assets::AssetDatabase& database,
        const std::optional<GameEngine::Assets::AssetType> assetType,
        const GameEngine::Assets::AssetReferenceForm form)
    {
        namespace Assets = GameEngine::Assets;
        if (assetType)
        {
            return Assets::CollectAssetChoices(database, *assetType, form);
        }
        std::vector<Assets::AssetChoice> choices;
        for (const Assets::AssetType type :
             { Assets::AssetType::Sprite, Assets::AssetType::Mesh, Assets::AssetType::AudioClip,
               Assets::AssetType::Font, Assets::AssetType::Scene })
        {
            std::vector<Assets::AssetChoice> ofType =
                Assets::CollectAssetChoices(database, type, form);
            choices.insert(
                choices.end(), std::make_move_iterator(ofType.begin()),
                std::make_move_iterator(ofType.end()));
        }
        return choices;
    }
}

bool EditorInspectorPanel::AcceptsDraggedAsset(
    const std::optional<GameEngine::Assets::AssetType> assetType) const
{
    const GameEngine::Assets::AssetDatabase* const database = mContext.GetProjectAssetDatabase();
    return database &&
        GameEngine::Assets::AssetReferenceMatchesType(
            *database, mAssetDrag.GetDraggedAsset(), assetType);
}

void EditorInspectorPanel::DrawAssetReferenceRow(
    const GameEngine::UI::UIRect& content, float& y, const std::string& label,
    const GameEngine::UI::WidgetId id, const GameEngine::Assets::AssetReference& value,
    const std::optional<GameEngine::Assets::AssetType> assetType,
    const std::function<void(const GameEngine::Assets::AssetReference&)>& apply)
{
    namespace Assets = GameEngine::Assets;
    using GameEngine::Core::ChoiceModel;

    const Assets::AssetDatabase* const database = mContext.GetProjectAssetDatabase();
    const Assets::AssetReferenceStatus status = database
        ? Assets::ClassifyAssetReference(*database, value)
        : Assets::AssetReferenceStatus::Empty;

    // 목록은 펼쳐진 칸에서만 모은다. 0번 항목은 "비우기"이고 그 뒤가 에셋들이다.
    std::vector<Assets::AssetChoice> choices;
    // 이 프로젝트가 이미 정체성으로 가리키고 있으면 새로 고르는 것도 그렇게 적는다. 섞여
    // 쓰면 한 장면에 두 형식이 함께 살게 된다.
    const GameEngine::Assets::AssetReferenceForm form = mContext.AreSceneReferencesMigrated()
        ? GameEngine::Assets::AssetReferenceForm::Identity
        : GameEngine::Assets::AssetReferenceForm::Path;
    const auto collect = [&choices, database, assetType, form]()
    {
        if (database)
        {
            choices = CollectChoicesFor(*database, assetType, form);
        }
    };
    const bool open = mOpenAssetChooser == id && mAssetChoice.IsOpen();
    if (open)
    {
        collect();
    }

    std::vector<UIRect> cells;
    LayoutPropertyRow(content, y, label.c_str(), 1, cells);
    if (IsRowVisible(content, y))
    {
        UIRect fieldRect = cells[0];
        if (status == Assets::AssetReferenceStatus::Missing)
        {
            const float markerWidth = (std::min)(S(76.0f), fieldRect.width * 0.4f);
            fieldRect.width -= markerWidth + S(2.0f);
            mUI.DrawLabel(
                { fieldRect.GetRight() + S(2.0f), y, markerWidth, S(RowHeight) }, "not found",
                ErrorColor, SecondaryFontSize, GameEngine::UI::TextAlign::Center);
        }
        // 끌어 온 에셋이 이 칸에 놓일 수 있으면 그 사실을 보인다. 받을 수 없는 칸은 강조하지
        // 않으므로, 사람이 놓기 전에 어디가 받는지 안다.
        if (mAssetDrag.IsDraggingAsset() && AcceptsDraggedAsset(assetType) &&
            fieldRect.Contains(mUI.GetMouseX(), mUI.GetMouseY()))
        {
            mUI.DrawPanel(fieldRect, DropTargetColor);
            if (mUI.WasMouseReleased())
            {
                apply(mAssetDrag.TakeDraggedAsset());
            }
        }
        // 머리 칸은 지금 값이다. 누르면 이 칸의 목록이 펼쳐지고, 다른 칸의 목록은 접힌다.
        if (mUI.DrawButton(
                id, fieldRect,
                database ? mContext.DescribeAssetReference(value) : "(None)"))
        {
            if (mOpenAssetChooser != id)
            {
                mAssetChoice = ChoiceModel{};
                mOpenAssetChooser = id;
                collect();
            }
            ChoiceModel::Input toggle;
            toggle.toggle = true;
            static_cast<void>(mAssetChoice.Apply(choices.size() + 1, toggle));
            if (!mAssetChoice.IsOpen())
            {
                mOpenAssetChooser = 0;
            }
        }
    }
    y += S(RowHeight) + S(2.0f);
    if (mOpenAssetChooser != id || !mAssetChoice.IsOpen())
    {
        return;
    }

    // 선택은 값이 정하고 모델은 그것을 따른다. 값이 가리키는 것이 목록에 없으면 — 지워졌거나
    // 오타이거나 — 어느 항목도 선택되어 보이지 않는다.
    const std::size_t count = choices.size() + 1;
    std::size_t selected = value.IsValid() ? ChoiceModel::NoSelection : 0;
    for (std::size_t index = 0; index < choices.size(); ++index)
    {
        if (choices[index].reference == value)
        {
            selected = index + 1;
        }
    }
    mAssetChoice.SetSelected(selected, count);

    ChoiceModel::Input input;
    const UIRect rowRect{
        content.x + S(Padding) * 3.0f, 0.0f, content.width - 4.0f * S(Padding), S(RowHeight) };
    for (std::size_t index = 0; index < count; ++index)
    {
        if (IsRowVisible(content, y))
        {
            UIRect rect = rowRect;
            rect.y = y;
            const GameEngine::UI::UIContext::SelectableResult row = mUI.DrawSelectable(
                GameEngine::UI::MakeChildWidgetId(id, static_cast<std::uint64_t>(index) + 1), rect,
                index == 0 ? "(None)" : choices[index - 1].label,
                index == mAssetChoice.GetSelected());
            if (row.clicked)
            {
                input.clicked = index;
            }
        }
        y += S(RowHeight) + S(2.0f);
    }
    if (choices.empty() && IsRowVisible(content, y))
    {
        const std::string typeName = assetType
            ? std::string(Assets::AssetDatabase::GetAssetTypeName(*assetType))
            : std::string("asset");
        mUI.DrawLabel(
            { rowRect.x, y, rowRect.width, S(RowHeight) },
            "No " + typeName + " assets in this project.", DimTextColor, SecondaryFontSize);
        y += S(RowHeight) + S(2.0f);
    }

    if (input.clicked)
    {
        const std::size_t chosen = *input.clicked;
        const ChoiceModel::Result result = mAssetChoice.Apply(count, input);
        if (result.selectionChanged)
        {
            apply(chosen == 0 ? Assets::AssetReference{} : choices[chosen - 1].reference);
        }
        if (!mAssetChoice.IsOpen())
        {
            mOpenAssetChooser = 0;
        }
    }
}

void EditorInspectorPanel::DrawValueRow(
    const GameEngine::UI::UIRect& content, float& y, const std::string& label,
    const GameEngine::UI::WidgetId id, const GameEngine::Runtime::PropertyKind kind,
    const std::span<const std::string> enumNames,
    const std::optional<GameEngine::Assets::AssetType> assetType,
    const GameEngine::Runtime::PropertyValue& value,
    const std::function<void(const GameEngine::Runtime::PropertyValue&)>& apply)
{
    using namespace GameEngine::Runtime;

    switch (kind)
    {
    case PropertyKind::Bool:
        DrawToggleRow(content, y, label.c_str(), id, std::get<bool>(value), "On", "Off",
            [&apply](const bool v) { apply(PropertyValue{ v }); });
        break;
    case PropertyKind::Int:
        // 정수는 숫자 칸에 실수로 표시하고 반올림해 적용한다.
        DrawFloatRow(content, y, label.c_str(), id, static_cast<float>(std::get<int>(value)),
            [&apply](const float v) { apply(PropertyValue{ static_cast<int>(std::lround(v)) }); });
        break;
    case PropertyKind::Float:
        DrawFloatRow(content, y, label.c_str(), id, std::get<float>(value),
            [&apply](const float v) { apply(PropertyValue{ v }); });
        break;
    case PropertyKind::String:
        DrawStringRow(content, y, label.c_str(), id, std::get<std::string>(value),
            [&apply](const std::string& v) { apply(PropertyValue{ v }); });
        break;
    case PropertyKind::Vector2:
        DrawVector2Row(content, y, label.c_str(), id, std::get<GameEngine::Math::Vector2>(value),
            [&apply](const GameEngine::Math::Vector2& v) { apply(PropertyValue{ v }); });
        break;
    case PropertyKind::Vector3:
        DrawVector3Row(content, y, label.c_str(), id, std::get<GameEngine::Math::Vector3>(value),
            [&apply](const GameEngine::Math::Vector3& v) { apply(PropertyValue{ v }); });
        break;
    case PropertyKind::Color:
        DrawColorRow(content, y, label.c_str(), id, std::get<GameEngine::Math::Color>(value),
            [&apply](const GameEngine::Math::Color& v) { apply(PropertyValue{ v }); });
        break;
    case PropertyKind::AssetReference:
        // 드롭다운으로 고르거나 콘텐츠 브라우저에서 끌어 놓는다 — 손으로 경로를 적는 칸은
        // 없다. 종류가 정해진 속성은 그 종류의 서브에셋만 목록과 드롭 대상에 들어오고, 정해지지
        // 않은 속성은 사람이 가리킬 만한 모든 종류를 보인다.
        DrawAssetReferenceRow(
            content, y, label, id, std::get<GameEngine::Assets::AssetReference>(value), assetType,
            [&apply](const GameEngine::Assets::AssetReference& v) { apply(PropertyValue{ v }); });
        break;
    case PropertyKind::Enum:
    {
        // 이름 표를 도는 순환 버튼이다. 누를 때마다 다음 이름으로 넘어간다.
        std::vector<UIRect> cells;
        LayoutPropertyRow(content, y, label.c_str(), 1, cells);
        const int current = std::get<int>(value);
        if (IsRowVisible(content, y) && !enumNames.empty() &&
            current >= 0 && static_cast<std::size_t>(current) < enumNames.size())
        {
            if (mUI.DrawButton(id, cells[0], CapitalizeFirst(enumNames[static_cast<std::size_t>(current)])))
            {
                apply(PropertyValue{ (current + 1) % static_cast<int>(enumNames.size()) });
            }
        }
        y += S(RowHeight) + S(2.0f);
        break;
    }
    }
}

void EditorInspectorPanel::ApplyProperty(
    GameEngine::Runtime::Component& component,
    const GameEngine::Runtime::PropertyDescriptor& descriptor,
    const GameEngine::Runtime::PropertyValue& value)
{
    mPropertyEdit.ApplyProperty(component, descriptor, value, CurrentMergeKey());
}

std::uint64_t EditorInspectorPanel::CurrentMergeKey() const
{
    // 필드 밖의 편집 — 토글, enum 버튼 — 은 키 0으로 병합에서 빠진다. 클릭 하나가 undo 하나다.
    return mPropertyEdit.MakeMergeKey(mMergeSourceField);
}

void EditorInspectorPanel::DrawSpriteSheetSettings(
    const GameEngine::UI::UIRect& content, float& y, const GameEngine::Assets::Sprite& sprite)
{
    using GameEngine::Assets::Sprite;
    // 방금 쓴 값이 있으면 그것을 기준으로 삼는다. 파일을 썼다고 런타임의 자산 데이터베이스가
    // 곧바로 갱신되지는 않는다(프로젝트 디렉터리 감시가 조용해진 뒤에야 다시 스캔한다) —
    // sprite.GetSheet()를 그대로 쓰면, 칸에서 포커스를 떼는 순간 방금 입력한 값이 아직 낡은
    // 스캔 결과로 되돌아가 보인다. 아직 아무것도 안 썼으면 지금 실제로 읽힌 값이 기준이다.
    const Sprite::Sheet current = mLastWrittenSpriteSheet.value_or(sprite.GetSheet());

    // 한 자리 고쳐도 나머지 세 자리는 지금 값 그대로 다시 쓴다 — 칸마다 apply가 독립적으로
    // 불리므로, 여기서 합쳐 두지 않으면 마지막으로 고친 칸만 반영된 시트가 나간다.
    const auto applyField =
        [this, &sprite, current](const std::function<void(Sprite::Sheet&)>& mutate)
    {
        Sprite::Sheet next = current;
        mutate(next);
        if (mLastWrittenSpriteSheet == next)
        {
            return;
        }
        const GameEngine::Assets::AssetDatabase* const database = mContext.GetProjectAssetDatabase();
        if (database && WriteSpriteSheet(*database, sprite, next))
        {
            mLastWrittenSpriteSheet = next;
        }
    };
    // 정수 칸은 실수 칸을 재사용한다: 반올림해 담을 뿐 새 위젯을 만들지 않는다. 소수를 칠
    // 이유가 없는 값이라 %g로 찍히는 그대로 정수처럼 보인다.
    const auto applyRounded = [&applyField](
        const float value, void (*const set)(Sprite::Sheet&, int))
    {
        const int rounded = (std::max)(0, static_cast<int>(std::lround(value)));
        applyField([set, rounded](Sprite::Sheet& sheet) { set(sheet, rounded); });
    };

    DrawFloatRow(content, y, "Columns", GameEngine::UI::MakeWidgetId("inspector-sprite-columns"),
        static_cast<float>(current.columns),
        [&applyRounded](const float v)
        { applyRounded(v, [](Sprite::Sheet& sheet, const int value) { sheet.columns = value; }); });
    DrawFloatRow(content, y, "Rows", GameEngine::UI::MakeWidgetId("inspector-sprite-rows"),
        static_cast<float>(current.rows),
        [&applyRounded](const float v)
        { applyRounded(v, [](Sprite::Sheet& sheet, const int value) { sheet.rows = value; }); });
    DrawFloatRow(content, y, "Frame Count", GameEngine::UI::MakeWidgetId("inspector-sprite-frame-count"),
        static_cast<float>(current.frameCount),
        [&applyRounded](const float v)
        { applyRounded(v, [](Sprite::Sheet& sheet, const int value) { sheet.frameCount = value; }); });
    DrawFloatRow(content, y, "Frame Rate", GameEngine::UI::MakeWidgetId("inspector-sprite-frame-rate"),
        current.frameRate,
        [&applyField](const float v) { applyField([v](Sprite::Sheet& sheet) { sheet.frameRate = v; }); });
}

void EditorInspectorPanel::DrawMaterialSettings(
    const GameEngine::UI::UIRect& content, float& y,
    const GameEngine::Assets::AssetDatabase& database, const GameEngine::Assets::Material& material)
{
    namespace Assets = GameEngine::Assets;

    // 시트와 같은 이유로 마지막에 쓴 값을 기준으로 삼는다. 값은 머티리얼 자신이 들고 있지
    // 않고(Sprite와 달리 texture·tint는 스캔이 만드는 페이로드다) 데이터베이스에서 읽는다 —
    // 아직 아무것도 안 썼으면 그 값이 기준이다.
    const std::shared_ptr<const Assets::MaterialData> loaded =
        database.LoadMaterial(Assets::AssetReference(material.GetRelativePath()));
    const Assets::MaterialData current =
        mLastWrittenMaterial.value_or(loaded ? *loaded : Assets::MaterialData{});

    const auto applyField =
        [this, &material, current](const std::function<void(Assets::MaterialData&)>& mutate)
    {
        Assets::MaterialData next = current;
        mutate(next);
        if (mLastWrittenMaterial == next)
        {
            return;
        }
        if (WriteMaterial(material, next))
        {
            mLastWrittenMaterial = next;
        }
    };

    // 텍스처는 참조 칸이다. 이 엔진에서 픽셀을 들고 있는 종류가 Sprite뿐이라 그것만 보인다 —
    // Material::texture의 뜻 그대로다.
    DrawAssetReferenceRow(
        content, y, "Texture", GameEngine::UI::MakeWidgetId("inspector-material-texture"),
        current.texture, Assets::AssetType::Sprite,
        [&applyField](const Assets::AssetReference& v)
        { applyField([&v](Assets::MaterialData& data) { data.texture = v; }); });
    DrawColorRow(
        content, y, "Tint", GameEngine::UI::MakeWidgetId("inspector-material-tint"), current.tint,
        [&applyField](const GameEngine::Math::Color& v)
        { applyField([v](Assets::MaterialData& data) { data.tint = v; }); });
}

void EditorInspectorPanel::DrawAssetInspector(
    const GameEngine::UI::UIRect& content, const std::filesystem::path& assetPath)
{
    if (mInspectedAssetPath != assetPath)
    {
        // 다른 에셋이다: 이전 에셋의 칸 텍스트와 마지막으로 쓴 시트는 이 에셋의 것이 아니다.
        mInspectedAssetPath = assetPath;
        mFieldTexts.clear();
        mUI.ClearFieldFocus();
        mLastWrittenSpriteSheet.reset();
        mLastWrittenMaterial.reset();
    }

    const GameEngine::Assets::AssetDatabase* const database = mContext.GetProjectAssetDatabase();
    const GameEngine::Assets::Asset* const asset = database ? database->FindAsset(assetPath) : nullptr;
    const auto* const sprite = dynamic_cast<const GameEngine::Assets::Sprite*>(asset);
    const auto* const material = dynamic_cast<const GameEngine::Assets::Material*>(asset);

    float y = content.y + S(Padding);
    mUI.DrawLabel(
        { content.x + S(Padding), y, content.width - 2.0f * S(Padding), S(RowHeight) },
        assetPath.generic_string(), TextColor, RowFontSize);
    y += S(RowHeight) + S(Padding);

    if (sprite)
    {
        DrawSpriteSheetSettings(content, y, *sprite);
    }
    else if (material && database)
    {
        DrawMaterialSettings(content, y, *database, *material);
    }
    else
    {
        mUI.DrawLabel(
            { content.x + S(Padding), y, content.width - 2.0f * S(Padding), S(RowHeight) },
            asset ? "This asset has no editable settings." : "This asset could not be found.",
            DimTextColor, RowFontSize);
        y += S(RowHeight);
    }
    mInspectorContentHeight = y - content.y;
}

void EditorInspectorPanel::Draw(const GameEngine::UI::UIRect content)
{
    GameEngine::Runtime::GameObject* const gameObject =
        GetInspectedGameObject(mContext.GetSelectedObject());
    if (!gameObject)
    {
        if (const std::optional<std::filesystem::path> assetPath = mContext.GetSelectedAssetPath())
        {
            mInspectedInstanceId = 0;
            DrawAssetInspector(content, *assetPath);
            return;
        }
        mInspectedAssetPath.clear();
        mLastWrittenSpriteSheet.reset();
        mLastWrittenMaterial.reset();
        mUI.DrawLabel(
            { content.x + S(Padding), content.y, content.width - 2.0f * S(Padding), S(RowHeight) },
            mContext.GetSelectedObject() ? "Select a GameObject to inspect it."
                                         : "No object selected.",
            DimTextColor, RowFontSize);
        mInspectedInstanceId = 0;
        mInspectorContentHeight = 0.0f;
        return;
    }
    mInspectedAssetPath.clear();
    mLastWrittenSpriteSheet.reset();
    mLastWrittenMaterial.reset();
    if (mInspectedInstanceId != gameObject->GetInstanceId())
    {
        // 다른 객체다: 이전 객체의 칸 텍스트는 어느 것도 이 객체의 것이 아니다.
        mInspectedInstanceId = gameObject->GetInstanceId();
        mFieldTexts.clear();
        mUI.ClearFieldFocus();
        mAddComponentListOpen = false;
        mOpenAssetChooser = 0;
    }

    const float offset = mUI.ApplyScroll(
        GameEngine::UI::MakeWidgetId("inspector-scroll"), content, mInspectorContentHeight);
    float y = content.y - offset + S(Padding);
    const float top = y;

    const GameEngine::UI::WidgetId objectId =
        GameEngine::UI::MakeWidgetId("inspector-object", gameObject->GetInstanceId());
    // 이름과 활성은 GameObject의 것이라 ApplyProperty를 지나지 않는다. 기록은 여기서 같은
    // 규칙으로 한다: 이름 타이핑은 속성 편집처럼 병합되고, 활성 토글은 클릭마다 한 단계다.
    DrawStringRow(content, y, "Name", GameEngine::UI::MakeChildWidgetId(objectId, 1), gameObject->GetName(),
        [this, gameObject](const std::string& v)
        {
            const std::string before = gameObject->GetName();
            // 같은 이름은 편집이 아니다 — no-op push가 redo 스택을 지우면 안 된다.
            if (before == v)
            {
                return;
            }
            gameObject->SetName(v);
            mContext.RecordEdit(std::make_unique<GameObjectNameCommand>(
                mContext, gameObject->GetInstanceId(), before, v, CurrentMergeKey()));
        });
    DrawToggleRow(content, y, "Active", GameEngine::UI::MakeChildWidgetId(objectId, 2), gameObject->IsActive(), "Active", "Inactive",
        [this, gameObject](const bool v)
        {
            gameObject->SetActive(v);
            mContext.RecordEdit(
                std::make_unique<GameObjectActiveCommand>(mContext, gameObject->GetInstanceId(), v));
        });

    mComponentToRemove = nullptr;
    for (const std::unique_ptr<GameEngine::Runtime::Component>& component :
         gameObject->GetAllComponents())
    {
        DrawComponentInspector(*component, content, y);
    }
    if (mComponentToRemove)
    {
        // 걷기가 끝났으니 지워도 된다. 커맨드가 제거 전에 속성 스냅숏을 떠 두므로, undo가 타입
        // 이름 + 스냅숏으로 컴포넌트를 되살린다. 지운 컴포넌트의 칸 텍스트는 다음에 그 id를 얻는
        // 위젯의 것이 아니므로 함께 버린다.
        auto command = std::make_unique<RemoveComponentCommand>(mContext, *mComponentToRemove);
        mComponentToRemove = nullptr;
        if (command->Apply())
        {
            mContext.RecordEdit(std::move(command));
            mFieldTexts.clear();
            mUI.ClearFieldFocus();
        }
    }

    // 팔레트는 여기 없다. 인스펙터가 자리를 빌려 주면 팔레트가 자기 높이를 가질 수 없어 타일
    // 하나를 고르는 데 스크롤이 두 겹이 된다. 팔레트는 자기 패널(Tile Palette)에 산다 — 두 곳에
    // 두면 어느 쪽이 진짜인지 알 수 없게 된다.
    DrawAddComponent(*gameObject, content, y);

    mInspectorContentHeight = y - top + S(Padding);
}

void EditorInspectorPanel::DrawAddComponent(
    GameEngine::Runtime::GameObject& gameObject, const GameEngine::UI::UIRect& content, float& y)
{
    using namespace GameEngine::Runtime;
    const GameEngine::UI::WidgetId listId = GameEngine::UI::MakeWidgetId("add-component");

    y += S(Padding);
    if (IsRowVisible(content, y) &&
        mUI.DrawButton(listId, { content.x + S(Padding), y, content.width - 2.0f * S(Padding), S(RowHeight) },
            mAddComponentListOpen ? "Cancel" : "Add Component"))
    {
        mAddComponentListOpen = !mAddComponentListOpen;
    }
    y += S(RowHeight) + S(2.0f);
    if (!mAddComponentListOpen)
    {
        return;
    }

    std::uint64_t index = 0;
    std::size_t gameComponentCount = 0;
    for (const EditorComponentChoice& choice :
         BuildEditorComponentChoices(mContext.GetGameComponentSchemas()))
    {
        ++index;
        if (IsRowVisible(content, y) &&
            mUI.DrawButton(GameEngine::UI::MakeChildWidgetId(listId, index),
                { content.x + S(Padding) * 3.0f, y, content.width - 4.0f * S(Padding), S(RowHeight) },
                SpaceCamelCase(choice.typeName)))
        {
            AddComponentOfType(gameObject, choice.typeName, choice.prototype);
        }
        if (choice.isGameComponent)
        {
            ++gameComponentCount;
        }
        y += S(RowHeight) + S(2.0f);
    }

    // 게임 컴포넌트가 하나도 없으면 그 사실을 목록 안에서 말한다. 콘솔에도 남기지만, 사람이
    // Add Component를 눌러 자기 스크립트를 찾는 순간에 보고 있는 것은 이 목록이다 — 아무 말도
    // 없는 짧은 목록은 "이 에디터는 원래 그런 것"처럼 보인다.
    if (gameComponentCount == 0)
    {
        if (IsRowVisible(content, y))
        {
            mUI.DrawLabel(
                { content.x + S(Padding) * 3.0f, y, content.width - 4.0f * S(Padding),
                  S(RowHeight) },
                mContext.GetGameComponentSchemas().empty()
                    ? "No game components: build the game project."
                    : "This project's components are all listed above.",
                DimTextColor, SecondaryFontSize);
        }
        y += S(RowHeight) + S(2.0f);
    }
}


void EditorInspectorPanel::AddComponentOfType(
    GameEngine::Runtime::GameObject& gameObject, std::string typeName,
    GameEngine::Core::Json prototype)
{
    // 추가는 커맨드의 Apply가 한다 — redo가 같은 길을 다시 밟는다.
    auto command = prototype.IsNull()
        ? std::make_unique<AddComponentCommand>(mContext, gameObject.GetInstanceId(), typeName)
        : std::make_unique<AddComponentCommand>(
              mContext, gameObject.GetInstanceId(), typeName, std::move(prototype));
    if (command->Apply())
    {
        mContext.RecordEdit(std::move(command));
    }
    else
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to add a component. type=", typeName);
    }
    mAddComponentListOpen = false;
    mOpenAssetChooser = 0;
}

}
