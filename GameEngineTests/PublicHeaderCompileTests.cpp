// Deliberately first: windows.h defines A/W macros such as DrawText, GetObject, CopyFile, and
// LoadImage. An engine function named after one of them is silently renamed here, and left alone in
// translation units that do not see windows.h, so the two disagree at link time rather than at the
// declaration. Including it before every engine header, and referencing the at-risk functions below,
// turns that into a link error in this test instead of in whichever project trips over it later.
#include <windows.h>

#include "App/Application.h"
#include "App/EditorSettings.h"
#include "App/EngineLoop.h"
#include "App/GameTimer.h"
#include "App/GraphicsBackendChoice.h"
#include "App/IGameBootstrap.h"
#include "App/ProjectSettings.h"
#include "App/ProjectFile.h"
#include "App/ProjectSettingsLoader.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AudioData.h"
#include "Build/ProjectBuilder.h"
#include "Build/ProjectBuildSource.h"
#include "Rules/EditorComponentChoices.h"
#include "Runtime/UIStackOrder.h"
#include "UI/DynamicTextureSnapshotPool.h"
#include "Diagnostics/ApiFailure.h"
#include "Diagnostics/Debug.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Math/Vector.h"
#include "Platform/Win32/Win32Utilities.h"
#include "Platform/Win32/Win32Window.h"
#include "Rendering/D3D11/D3D11GraphicsDevice.h"
#include "Rendering/D3D11/D3D11ResolvedResources.h"
#include "Rendering/D3D11/D3D11ResourceResolver.h"
#include "Rendering/D3D11/ID3D11GraphicsDevice.h"
#include "Rendering/D3D11/D3D11CommandList.h"
#include "Rendering/D3D12/D3D12DescriptorHeapAllocator.h"
#include "Rendering/D3D12/D3D12FrameResources.h"
#include "Rendering/D3D12/D3D12GraphicsDevice.h"
#include "Rendering/D3D12/D3D12PipelineDescriptions.h"
#include "Rendering/D3D12/D3D12Renderer.h"
#include "Rendering/D3D12/D3D12ResolvedResources.h"
#include "Rendering/D3D12/D3D12ResourceDescriptions.h"
#include "Rendering/D3D12/D3D12ResourceResolver.h"
#include "Rendering/D3D12/D3D12TextureUploader.h"
#include "Rendering/D3D12/D3D12UploadBufferPool.h"
#include "Rendering/D3D12/ID3D12GraphicsDevice.h"
#include "Rendering/D3D12/D3D12CommandList.h"
#include "Assets/FbxImporter.h"
#include "Rendering/FrameBoundCache.h"
#include "Rendering/GraphicsDeviceCapabilities.h"
#include "Rendering/IGraphicsDevice.h"
#include "Rendering/IRenderFrontend.h"
#include "Rendering/RenderFrame.h"
#include "Rendering/RenderFrameBuilder.h"
#include "Rendering/RenderBackendInterfaces.h"
#include "Rendering/RenderCommandList.h"
#include "Rendering/RenderSamplerPolicy.h"
#include "Rendering/RenderSubmissionPolicy.h"
#include "Rendering/RenderPacketEncoder.h"
#include "Rendering/MeshDrawGeometry.h"
#include "Rendering/MeshRenderPass.h"
#include "Rendering/SpriteRenderPass.h"
#include "SceneRendering/RenderFrontendFactory.h"
#include "Platform/IAudioOutput.h"
#include "Platform/IAudioDecoder.h"
#include "Platform/IImageDecoder.h"
#include "Platform/ITextRasterizer.h"
#include "Platform/PlatformServices.h"
#include "Assets/ImageLimits.h"
#include "Rendering/Direct3D/MatrixConversion.h"
#include "Rendering/Direct3D/ShaderCompiler.h"
#include "Rendering/ShaderInterop.h"
#include "Rendering/ShaderProgram.h"
#include "Rendering/Direct3D/ShaderPaths.h"
#include "Rendering/TextRasterizationKey.h"
#include "SceneRendering/SceneRenderPass.h"
#include "Rendering/D3D11/D3D11Renderer.h"
#include "Runtime/AudioSource.h"
#include "Runtime/AudioSystem.h"
#include "Runtime/Behaviour.h"
#include "Runtime/BoxCollider3D.h"
#include "Runtime/Camera.h"
#include "Runtime/Collider3D.h"
#include "Runtime/Component.h"
#include "Runtime/ComponentType.h"
#include "Runtime/Game.h"
#include "Runtime/GameObject.h"
#include "Runtime/Light.h"
#include "Runtime/MeshRenderer.h"
#include "Runtime/MonoBehaviour.h"
#include "Runtime/Object.h"
#include "Runtime/ObjectRegistry.h"
#include "Runtime/PropertyDescriptor.h"
#include "Runtime/Physics2DSystem.h"
#include "Runtime/Physics3DSystem.h"
#include "Runtime/Rigidbody2D.h"
#include "Runtime/Rigidbody3D.h"
#include "Runtime/Renderer.h"
#include "Runtime/Scene.h"
#include "Runtime/SceneManager.h"
#include "Runtime/SpriteRenderer.h"
#include "Runtime/Transform.h"
#include "Serialization/ComponentFactory.h"
#include "Serialization/ComponentSchema.h"
#include "Math/Aabb2D.h"
#include "Math/Aabb3D.h"
#include "Core/Json.h"
#include "Math/Float.h"
#include "Assets/VertexLayout.h"
#include "Assets/ResourceId.h"
#include "Rendering/SpriteVertex.h"
#include "Platform/TextFile.h"
#include "Platform/RelativePath.h"
#include "UIModel/ChoiceModel.h"
#include "UIModel/TextEditModel.h"
#include "UI/TextFit.h"
#include "Rules/DragGesture.h"
#include "Math/ViewportProjection.h"
#include "Document/UndoStack.h"
#include "Serialization/RuntimeComponentFactories.h"
#include "Serialization/SceneSerializer.h"

namespace
{
[[maybe_unused]] constexpr bool PublicHeadersCompileTogether = true;
}

/// <summary>
/// windows.h 매크로 이름 위로 개명되면 조용히 A/W 변형이 되어 버릴 엔진 멤버들의 이름을 적는다.
/// windows.h가 보이는 이 번역 단위에서 그 주소를 취하면, 링커는 이 파일이 보는 바로 그 이름들을
/// 해석하게 된다. 그래서 충돌은 나중에 어긋난 어떤 프로젝트에서가 아니라 여기서 링크 실패로
/// 드러난다.
///
/// 멤버들을 외부 링크의 객체에 담는 것은 의도다: 내부 링크의 안 쓰는 상수는 링커에 닿기 전에
/// 버려져서, 이 가드는 결코 작동하지 않았을 것이다.
/// </summary>
struct EngineApiNameGuard
{
    GameEngine::Runtime::Object* (GameEngine::Runtime::Scene::*SceneFindObject)(unsigned int);
    GameEngine::Runtime::Object* (GameEngine::Runtime::ObjectRegistry::*RegistryFindObject)(
        unsigned int) const;
    GameEngine::Runtime::Object* (GameEngine::Runtime::Game::*GameFindObject)(unsigned int) const;
    // One shared pass draws text for every backend, so one member covers them all.
    void (GameEngine::Rendering::SpriteRenderPass::*DrawTextQuad)(
        GameEngine::Rendering::IRenderCommandList&,
        const GameEngine::Rendering::RenderFrame&,
        const GameEngine::Rendering::TextDraw&,
        const GameEngine::Rendering::IResolvedTexture&) const;
};

EngineApiNameGuard gEngineApiNameGuard =
{
    &GameEngine::Runtime::Scene::FindObject,
    &GameEngine::Runtime::ObjectRegistry::FindObject,
    &GameEngine::Runtime::Game::FindObject,
    &GameEngine::Rendering::SpriteRenderPass::DrawTextQuad,
};
