#include "pch.h"
#include "ShaderCompiler.h"


#include <windows.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

#include "ShaderPaths.h"
#include "../ShaderBindings.h"
#include "../../Core/TextFile.h"
#include "../../Platform/ApplicationContent.h"
#include "../../Platform/Win32/Win32Diagnostics.h"

namespace GameEngine::Rendering::Direct3D
{

namespace
{
    /// <summary>
    /// HLSL이 쓰는 표기법의 슬롯이다. 예: "b0", "s1".
    /// </summary>
    [[nodiscard]] std::string ToRegisterName(const char prefix, const unsigned int slot)
    {
        return std::string(1, prefix) + std::to_string(slot);
    }

    /// <summary>
    /// 소스 바이트를 컴파일한다. 실행 시 예비 경로와 빌드 시 아티팩트 생산이 이 하나를 공유하므로,
    /// 둘이 다른 셰이더를 만들 수 없다.
    /// </summary>
    [[nodiscard]] bool CompileFromSource(
        const ShaderProgram program,
        const char* const entryPoint,
        const char* const target,
        const std::span<const std::byte> source,
        Microsoft::WRL::ComPtr<ID3DBlob>& blob)
    {
        UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        // 셰이더는 슬롯을 직접 적는 대신 엔진에서 받는다. 엔진이 정의하지 않은 슬롯을 부르는
        // 셰이더는 컴파일에 실패하며, 그것이 리팩터링을 살아남는 유일한 종류의 검사다.
        const ShaderBindingSlots slots = GetShaderBindings(program);
        const std::string constantSlot = ToRegisterName('b', slots.constantBuffer);
        const std::string textureSlot = ToRegisterName('t', slots.texture);
        const std::string samplerSlot = ToRegisterName('s', slots.sampler);
        // 이 값을 쓰는 프로그램은 SkinnedMesh 하나뿐이지만, 다른 세 슬롯과 같은 자리(여기 한
        // 곳)에서 나와야 HLSL의 register와 어긋날 수 없다는 원칙이 예외 없이 지켜진다.
        const std::string boneConstantSlot = ToRegisterName('b', SkinnedMeshBoneConstantBufferSlot);
        const std::array<D3D_SHADER_MACRO, 5> defines{
            D3D_SHADER_MACRO{ "CONSTANT_SLOT", constantSlot.c_str() },
            D3D_SHADER_MACRO{ "TEXTURE_SLOT", textureSlot.c_str() },
            D3D_SHADER_MACRO{ "SAMPLER_SLOT", samplerSlot.c_str() },
            D3D_SHADER_MACRO{ "BONE_CONSTANT_SLOT", boneConstantSlot.c_str() },
            D3D_SHADER_MACRO{ nullptr, nullptr },
        };

        // 소스 이름은 컴파일러 진단이 인용하는 것이라서, 아무것도 열지 않았어도 배포 경로로
        // 남는다. include 핸들러는 넘기지 않는다: 어느 셰이더도 `#include`를 쓰지 않고, 쓴다면
        // 표준 핸들러 대신 콘텐츠 소스를 통해 읽는 핸들러를 받아야 한다 — 표준 핸들러는 이
        // 소스에 없을 수 있는 디렉터리에 대해 include를 해석한다.
        const std::string sourceName = GetShaderRelativePath(program).generic_string();
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(
            source.data(), source.size(), sourceName.c_str(), defines.data(), nullptr,
            entryPoint, target, flags, 0,
            blob.ReleaseAndGetAddressOf(), errors.ReleaseAndGetAddressOf());
        if (SUCCEEDED(result))
        {
            return true;
        }
        if (errors)
        {
            Diagnostics::Debug::LogError(
                "Shader compiler output: ",
                std::string(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize()));
        }
        Diagnostics::Debug::LogError(
            "Failed to compile shader. path=", sourceName,
            ", program=", GetShaderProgramName(program),
            ", entryPoint=", entryPoint, ", target=", target);
        Platform::Win32::LogHResult("Shader compilation", result);
        return false;
    }
}

bool CompileShader(
    const ShaderProgram program,
    const char* entryPoint,
    const char* target,
    Microsoft::WRL::ComPtr<ID3DBlob>& blob)
{
    // 배포된 아티팩트가 우선한다. 아티팩트는 빌더가 배포마다 새로 만들므로 낡은 것이 남아 있을
    // 자리가 없고, 개발 디렉터리에는 아예 없어서 소스 편집이 그대로 반영된다.
    const std::filesystem::path compiledPath = GetCompiledShaderRelativePath(program, entryPoint);
    std::vector<std::byte> compiledBytes;
    if (Platform::GetApplicationContent().Read(compiledPath, compiledBytes) &&
        !compiledBytes.empty())
    {
        if (SUCCEEDED(D3DCreateBlob(compiledBytes.size(), blob.ReleaseAndGetAddressOf())))
        {
            std::memcpy(blob->GetBufferPointer(), compiledBytes.data(), compiledBytes.size());
            Diagnostics::Debug::Log(
                "Loaded a compiled shader artifact. path=", compiledPath.string());
            return true;
        }
    }

    // 아티팩트가 없으면 소스에서 컴파일한다. 소스는 경로로 여는 대신 애플리케이션의 콘텐츠를
    // 통해 읽으므로, 배포가 실행 파일 옆에 두든 안에 넣든 같은 방식으로 찾아진다.
    const std::filesystem::path relativePath = GetShaderRelativePath(program);
    std::vector<std::byte> source;
    if (!Platform::GetApplicationContent().Read(relativePath, source) || source.empty())
    {
        Diagnostics::Debug::LogError(
            "Failed to read a shader. path=", relativePath.string(),
            ", program=", GetShaderProgramName(program));
        return false;
    }
    return CompileFromSource(program, entryPoint, target, source, blob);
}

bool CompileRuntimeArtifacts(const std::filesystem::path& runtimeRootPath)
{
    // 이 계열의 프로그램마다 정점·픽셀 진입점 하나씩이다. 다른 셰이딩 언어의 계열은 자기
    // 서술자에서 자기만의 아티팩트 생산을 가리킨다.
    constexpr std::pair<const char*, const char*> Entries[] = {
        { "VS", "vs_5_0" },
        { "PS", "ps_5_0" },
    };

    bool allCompiled = true;
    for (const ShaderProgram program : AllShaderPrograms)
    {
        const std::filesystem::path sourcePath =
            runtimeRootPath / GetShaderRelativePath(program);
        const std::string sourceText = Core::ReadTextFile(sourcePath).value_or(std::string{});
        const std::span<const std::byte> source =
            std::as_bytes(std::span(sourceText.data(), sourceText.size()));
        if (source.empty())
        {
            Diagnostics::Debug::LogWarning(
                "A staged shader source could not be read for precompilation. path=",
                sourcePath.string());
            allCompiled = false;
            continue;
        }

        for (const auto& [entryPoint, target] : Entries)
        {
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            if (!CompileFromSource(program, entryPoint, target, source, blob))
            {
                allCompiled = false;
                continue;
            }

            const std::filesystem::path artifactPath =
                runtimeRootPath / GetCompiledShaderRelativePath(program, entryPoint);
            std::ofstream artifactStream(artifactPath, std::ios::binary | std::ios::trunc);
            artifactStream.write(
                static_cast<const char*>(blob->GetBufferPointer()),
                static_cast<std::streamsize>(blob->GetBufferSize()));
            artifactStream.flush();
            if (!artifactStream)
            {
                Diagnostics::Debug::LogWarning(
                    "Failed to write a compiled shader artifact. path=", artifactPath.string());
                allCompiled = false;
            }
        }
    }
    return allCompiled;
}

}
