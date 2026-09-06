#include "InputFieldCaretTests.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BackendPixelSupport.h"
#include "Platform/IAudioOutput.h"
#include "Platform/ITextMeasure.h"
#include "Rendering/CachedTextMeasure.h"
#include "Rendering/GraphicsBackend.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Runtime/Camera.h"
#include "Runtime/Canvas.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Input.h"
#include "Runtime/InputField.h"
#include "Runtime/RectMask.h"
#include "Runtime/RectTransform.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/TextRenderer.h"
#include "Runtime/Transform.h"
#include "SceneRendering/SceneRenderPass.h"
#include "TestSupport.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    using namespace GameEngine;

    bool SaveCapturedImage(const Rendering::CapturedImage& image, const std::filesystem::path& path)
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf()))) ||
            FAILED(factory->CreateStream(stream.GetAddressOf())) ||
            FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) ||
            FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(image.width, image.height))) return false;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) return false;
        // WIC expects BGRA; only the channel order changes, never the GPU-generated pixels.
        std::vector<BYTE> bgra(image.pixels.size());
        for (std::size_t offset = 0; offset < bgra.size(); offset += 4)
        {
            bgra[offset] = std::to_integer<BYTE>(image.pixels[offset + 2]);
            bgra[offset + 1] = std::to_integer<BYTE>(image.pixels[offset + 1]);
            bgra[offset + 2] = std::to_integer<BYTE>(image.pixels[offset]);
            bgra[offset + 3] = std::to_integer<BYTE>(image.pixels[offset + 3]);
        }
        const bool saved = SUCCEEDED(frame->WritePixels(image.height, image.width * 4,
            static_cast<UINT>(bgra.size()), bgra.data())) && SUCCEEDED(frame->Commit()) &&
            SUCCEEDED(encoder->Commit());
        if (saved) std::cout << "  input caret capture: " << path.string() << '\n';
        return saved;
    }

    struct Fixture
    {
        std::shared_ptr<Rendering::TextRasterizationCache> cache =
            std::make_shared<Rendering::TextRasterizationCache>(TestSupport::CreateTestTextRasterizer("Caret font"));
        Runtime::Game game{ nullptr, std::make_unique<Rendering::CachedTextMeasure>(cache) };
        Runtime::InputField* field = nullptr;
        Runtime::TextRenderer* text = nullptr;
        Runtime::RectTransform* rect = nullptr;
        unsigned int scale;

        explicit Fixture(const unsigned int factor) : scale(factor)
        {
            auto scene = std::make_unique<Runtime::Scene>(game.GetRuntimeContext(), "Caret regression");
            scene->CreateGameObject("Camera")->AddComponent<Runtime::Camera>()->SetClearColor({ 0, 0, 0, 1 });
            auto* canvas = scene->CreateGameObject("Canvas");
            canvas->AddComponent<Runtime::Canvas>()->SetScaleFactor(static_cast<float>(scale));
            auto* mask = scene->CreateGameObject("Mask");
            static_cast<void>(mask->GetTransform().SetParent(&canvas->GetTransform()));
            auto* maskRect = mask->AddComponent<Runtime::RectTransform>();
            maskRect->SetOffsetMin({ 20, 35 });
            maskRect->SetOffsetMax({ 260, 90 });
            static_cast<void>(mask->AddComponent<Runtime::RectMask>());
            auto* object = scene->CreateGameObject("Input");
            static_cast<void>(object->GetTransform().SetParent(&mask->GetTransform()));
            rect = object->AddComponent<Runtime::RectTransform>();
            rect->SetOffsetMin({ 0, -20 });
            rect->SetOffsetMax({ 220, 40 });
            text = object->AddComponent<Runtime::TextRenderer>();
            text->SetFontFamily("Caret font");
            text->SetFontSize(24);
            text->SetVerticalAlignment(Runtime::TextRenderer::VerticalAlignment::Middle);
            field = object->AddComponent<Runtime::InputField>();
            static_cast<void>(game.GetSceneManager().AddScene(std::move(scene)));
            game.SetRenderSurfaceSize(320.0f * scale, 120.0f * scale);
        }

        void Step(const Platform::InputState& state = {}, const float delta = 0.0f)
        {
            game.GetInput().BeginFrameWithState(state);
            game.Update(delta);
        }

        void Press(const Platform::Key key, const bool shift = false)
        {
            Platform::InputState state;
            state.SetKey(key, true);
            state.SetKey(Platform::Key::Shift, shift);
            Step(state);
            Step();
        }

        Rendering::RenderFrame Collect()
        {
            Rendering::RenderFrameBuilder builder;
            builder.SetRenderTargetSize({ 320 * scale, 120 * scale });
            SceneRendering::SceneRenderPass frontend(cache);
            frontend.Collect(game, builder);
            return std::move(builder).Build();
        }
    };
}

bool RunInputFieldCaretTests()
{
    using TestSupport::Expect;
    bool passed = true;
    for (const unsigned int scale : { 1u, 2u })
    {
        Fixture f(scale);
        f.field->RequestFocus();
        f.Step();
        passed &= Expect(f.field->IsCaretVisible() && !f.field->GetCaretRect().IsEmpty() &&
            std::abs(f.field->GetCaretRect().width - scale) < .01f,
            "an empty focused field must show a one-logical-pixel caret using the registered font height");
        f.field->SetText("가나 ");
        f.Step();
        const float afterSpace = f.field->GetCaretRect().x;
        f.Press(Platform::Key::Left);
        const float afterKorean = f.field->GetCaretRect().x;
        passed &= Expect(f.field->GetCaret() == 6 && afterSpace > afterKorean,
            "the caret must include a trailing space advance and move across UTF-8 character boundaries");
        f.Press(Platform::Key::Left, true);
        passed &= Expect(f.field->GetCaret() == 3 && f.field->GetSelection().begin == 3 &&
            f.field->GetSelection().end == 6 && !f.field->GetSelectionRects().empty(),
            "Shift movement must retain the selection anchor and produce a visible Korean selection");
        // Preedit replaces the selection visually; cancellation restores the uncommitted selection.
        Platform::InputState compose;
        compose.compositionText = "다라";
        compose.compositionCaret = 3;
        f.Step(compose);
        const float compositionMiddle = f.field->GetCaretRect().x;
        passed &= Expect(f.field->GetText() == "가나 " && f.text->GetText() == "가다라 " &&
            f.field->GetDisplayCaret() == 6 && !f.field->GetCompositionRects().empty(),
            "preedit must replace the selected display range without editing committed text");
        compose.compositionCaret = 6;
        f.Step(compose);
        passed &= Expect(f.field->GetCaretRect().x > compositionMiddle,
            "the visual caret must follow the IME cursor within the composition");
        f.Step();
        passed &= Expect(f.text->GetText() == "가나 " && f.field->GetSelection().HasSelection(),
            "canceling composition must restore the selected text without losing its final character");
        f.field->SetText("");
        f.Step();
        compose.compositionText = "한";
        compose.compositionCaret = 3;
        f.Step(compose);
        Platform::InputState nextSyllable;
        nextSyllable.typedText = "한";
        nextSyllable.compositionText = "글";
        nextSyllable.compositionCaret = 3;
        f.Step(nextSyllable);
        passed &= Expect(f.field->GetText() == "한" && f.text->GetText() == "한글" && f.field->GetDisplayCaret() == 6,
            "a committed syllable and the next preedit in one frame must both remain visible");
        Platform::InputState commit;
        commit.typedText = "글";
        commit.SetKey(Platform::Key::Enter, true);
        commit.MarkKeyHandledByIme(Platform::Key::Enter);
        f.Step(commit);
        passed &= Expect(f.field->GetText() == "한글" && f.text->GetText() == "한글" &&
            f.field->IsFocused() && !f.field->WasSubmittedThisFrame(),
            "the final IME result must persist once and its Enter must not submit the field");
        f.Step({}, .6f);
        passed &= Expect(!f.field->IsCaretVisible(), "an idle focused caret must blink off after half a second");
        f.Press(Platform::Key::Home);
        passed &= Expect(f.field->IsCaretVisible() && f.field->GetCaret() == 0,
            "editing navigation must reset the blink and move to the beginning");
        const auto begin = f.field->GetCaretRect();
        f.Press(Platform::Key::End);
        const auto end = f.field->GetCaretRect();
        Platform::InputState click;
        click.cursor = { static_cast<int>(begin.x + .25f * scale), static_cast<int>(begin.GetCenterY()) };
        click.SetMouseButton(Platform::MouseButton::Left, true);
        f.Step(click);
        passed &= Expect(f.field->GetCaret() == 0, "a click must place the caret at the nearest measured text boundary");
        Platform::InputState drag = click;
        drag.mousePresses.fill(0);
        drag.cursor.x = static_cast<int>(end.x);
        f.Step(drag);
        passed &= Expect(f.field->GetSelection().begin == 0 && f.field->GetSelection().end == 6,
            "dragging a captured field must extend selection using measured character positions");
        drag.SetMouseButton(Platform::MouseButton::Left, false);
        f.Step(drag);
        f.field->SetText(std::string(60, 'W') + "한글");
        f.Step();
        passed &= Expect(f.field->GetTextOffsetX() < 0 && !f.field->GetCaretRect().IsEmpty() &&
            f.field->GetCaretRect().GetRight() <= f.rect->GetVisibleRect().GetRight() + .01f,
            "a long single-line input must scroll its text to keep the caret inside the field");
        f.Press(Platform::Key::Home);
        passed &= Expect(std::abs(f.field->GetCaretRect().x - f.rect->GetResolvedRect().x) < .01f,
            "Home must bring the beginning back into view after horizontal scrolling");
        f.field->SetText("가");
        f.Step();
        const float leftAlignedCaret = f.field->GetCaretRect().x;
        f.text->SetAlignment(Runtime::TextRenderer::Alignment::Center);
        f.Step();
        const float centeredCaret = f.field->GetCaretRect().x;
        f.text->SetAlignment(Runtime::TextRenderer::Alignment::Right);
        f.Step();
        const float rightAlignedCaret = f.field->GetCaretRect().x;
        passed &= Expect(centeredCaret > leftAlignedCaret && rightAlignedCaret > centeredCaret &&
            std::abs((centeredCaret - leftAlignedCaret) - (rightAlignedCaret - centeredCaret)) < 2.1f * scale,
            "center and right aligned fields must move the caret with the rendered text block");
        f.Press(Platform::Key::Enter);
        passed &= Expect(!f.field->IsFocused() && !f.field->IsCaretVisible(), "submit must remove the focused caret");

        for (const bool ime : { false, true })
        {
            f.field->RequestFocus();
            f.text->SetAlignment(Runtime::TextRenderer::Alignment::Left);
            f.field->SetText("가나다");
            f.Step();
            f.Press(Platform::Key::Home);
            f.Press(Platform::Key::Right);
            f.Press(Platform::Key::Right, true);
            Platform::InputState preedit;
            if (ime) { preedit.compositionText = "라"; preedit.compositionCaret = 3; }
            f.Step(preedit);
            Platform::InputState clickCommit;
            clickCommit.typedText = ime ? "라" : "x";
            clickCommit.cursor = { static_cast<int>(f.rect->GetVisibleRect().x),
                static_cast<int>(f.rect->GetVisibleRect().GetCenterY()) };
            clickCommit.SetMouseButton(Platform::MouseButton::Left, true);
            f.Step(clickCommit);
            const std::string replaced = ime ? "가라다" : "가x다";
            passed &= Expect(f.field->GetText() == replaced && f.text->GetText() == replaced &&
                f.field->GetCaret() == 0 && !f.field->GetSelection().HasSelection() && f.field->WasEditedThisFrame(),
                "same-field clicking must commit queued text into the old selection before placing the new caret");
            Platform::InputState release;
            release.cursor = clickCommit.cursor;
            f.Step(release);
            passed &= Expect(f.field->GetText() == replaced && !f.field->WasEditedThisFrame(),
                "pointer release must not replay text already committed before the click");

            f.field->SetText("가나다");
            f.Step();
            f.Press(Platform::Key::Home);
            f.Press(Platform::Key::Right);
            Platform::InputState press;
            press.cursor = { static_cast<int>(std::round(f.field->GetCaretRect().x)),
                static_cast<int>(f.field->GetCaretRect().GetCenterY()) };
            press.SetMouseButton(Platform::MouseButton::Left, true);
            f.Step(press);
            Platform::InputState dragCommit = press;
            dragCommit.mousePresses.fill(0);
            dragCommit.typedText = ime ? "라" : "x";
            dragCommit.cursor.x = static_cast<int>(f.rect->GetVisibleRect().GetRight() - 2.0f);
            f.Step(dragCommit);
            const std::string inserted = ime ? "가라나다" : "가x나다";
            passed &= Expect(f.field->GetText() == inserted && f.field->GetCaret() == inserted.size() &&
                f.field->GetSelection().begin == (ime ? 6u : 4u) && f.field->GetSelection().end == inserted.size() &&
                f.field->WasEditedThisFrame(),
                "drag movement must insert queued text at the old caret once before extending the selection");
            release.cursor = dragCommit.cursor;
            f.Step(release);
        }
    }
    return passed;
}

bool RunInputFieldCaretImageTests()
{
    using TestSupport::Expect;
    struct Apartment
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    const auto captures = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "build" / "input-caret-captures" / (std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(captures);
    const auto backends = TestSupport::SupportedBackends();
    if (!Expect(!backends.empty(), "caret pixel tests require a graphics backend")) return false;
    bool passed = true;
    for (const unsigned int scale : { 1u, 2u })
    {
        Fixture f(scale);
        f.field->RequestFocus();
        f.Step();
        const auto emptyCaret = f.Collect();
        f.Step({}, .6f);
        const auto emptyBlink = f.Collect();
        f.field->SetText("가나다");
        f.Step();
        const auto visible = f.Collect();
        const auto caret = f.field->GetCaretRect();
        f.Step({}, .6f);
        const auto hidden = f.Collect();
        const auto clip = f.rect->GetVisibleRect();
        // Readable review images complement the intentionally partial mask pixel fixture above.
        f.rect->SetOffsetMin({ 0, 0 });
        f.rect->SetOffsetMax({ 220, 55 });
        f.field->SetText("한글 입력: ");
        Platform::InputState preedit;
        preedit.compositionText = "조합";
        preedit.compositionCaret = 3;
        f.Step(preedit);
        const auto compositionFrame = f.Collect();
        f.Step();
        f.Press(Platform::Key::Home);
        f.Press(Platform::Key::Right, true);
        f.Press(Platform::Key::Right, true);
        const auto selectionFrame = f.Collect();
        for (const auto* backend : backends)
        {
            auto device = backend->CreateDevice();
            Rendering::CapturedImage image, reference, emptyOn, emptyOff;
            if (!Expect(device && device->Initialize(Platform::NativeSurface{}) &&
                device->RenderToImage(visible, image) && device->RenderToImage(hidden, reference) &&
                device->RenderToImage(emptyCaret, emptyOn) && device->RenderToImage(emptyBlink, emptyOff),
                "focused and blinking caret frames must render on every registered backend")) { passed = false; continue; }
            std::size_t changed = 0;
            std::size_t emptyPixels = 0;
            std::size_t textPixels = 0;
            bool outsideClear = true;
            bool changesOnlyAtCaret = true;
            bool emptyOffClear = true;
            for (unsigned int y = 0; y < image.height; ++y)
                for (unsigned int x = 0; x < image.width; ++x)
                {
                    const auto actual = TestSupport::ReadPixel(image, x, y);
                    const auto before = TestSupport::ReadPixel(reference, x, y);
                    const auto blankOn = TestSupport::ReadPixel(emptyOn, x, y);
                    const auto blankOff = TestSupport::ReadPixel(emptyOff, x, y);
                    const bool inside = clip.Contains(x + .5f, y + .5f);
                    if (!inside) outsideClear &= actual.r == 0 && actual.g == 0 && actual.b == 0 &&
                        blankOn.r == 0 && blankOn.g == 0 && blankOn.b == 0;
                    if (actual != before)
                    {
                        ++changed;
                        changesOnlyAtCaret &= x + 1.0f >= caret.x && x <= caret.GetRight() &&
                            y + 1.0f >= caret.y && y <= caret.GetBottom();
                    }
                    if (before.r > 100) ++textPixels;
                    if (blankOn.r > 100) ++emptyPixels;
                    emptyOffClear &= blankOff.r == 0 && blankOff.g == 0 && blankOff.b == 0;
                }
            passed &= Expect(outsideClear && changesOnlyAtCaret && changed > 5 * scale && textPixels > 20,
                "caret blinking must change only its clipped rectangle while preserving real Korean glyph pixels");
            passed &= Expect(emptyOffClear && emptyPixels > 5 * scale,
                "the empty field must draw an actual masked caret and no glyph or placeholder pixels");
            Rendering::CapturedImage compositionImage, selectionImage;
            const std::string name = std::string(backend->id) + "-scale" + std::to_string(scale);
            passed &= Expect(device->RenderToImage(compositionFrame, compositionImage) &&
                device->RenderToImage(selectionFrame, selectionImage) &&
                SaveCapturedImage(compositionImage, captures / (name + "-ime.png")) &&
                SaveCapturedImage(selectionImage, captures / (name + "-selection.png")),
                "real Korean composition/caret and selection PNGs must be available for visual review");
        }
    }
    return passed;
}

static const TestSupport::Registration gInputFieldCaretTests{
    "UIEvent", "input carets must follow UTF-8 editing and IME composition", RunInputFieldCaretTests };
static const TestSupport::Registration gInputFieldCaretImages{
    "BackendImage", "input carets must blink and clip at both UI scales", RunInputFieldCaretImageTests };
