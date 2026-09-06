#include "pch.h"
#include "ShaderPaths.h"

#include <filesystem>
#include <vector>


namespace GameEngine::Rendering::Direct3D
{

std::filesystem::path GetShaderRelativePath(const ShaderProgram program)
{
    // The HLSL sources are shared within the Direct3D family, so they live beside this mapping
    // rather than in either backend directory, and both Direct3D backends compile the same files.
    switch (program)
    {
    case ShaderProgram::Mesh: return L"Rendering/Direct3D/Shaders/Mesh.hlsl";
    case ShaderProgram::Sprite: return L"Rendering/Direct3D/Shaders/Sprite.hlsl";
    case ShaderProgram::Text: return L"Rendering/Direct3D/Shaders/Text.hlsl";
    // 별도 파일인 이유는 파일이 아니라 컴파일러다: 이 소스 컴파일 경로는 include 핸들러를
    // 넘기지 않으므로(ShaderCompiler.cpp 주석 참고) Mesh.hlsl을 #include할 수 없고, 조명 계산을
    // 두 파일에 나란히 적는다.
    case ShaderProgram::SkinnedMesh: return L"Rendering/Direct3D/Shaders/SkinnedMesh.hlsl";
    }
    return {};
}

std::filesystem::path GetCompiledShaderRelativePath(
    const ShaderProgram program, const char* const entryPoint)
{
    std::filesystem::path path = GetShaderRelativePath(program);
    path.replace_extension();
    path += ".";
    path += entryPoint;
    path += ".cso";
    return path;
}

std::vector<std::filesystem::path> GetShaderRelativePaths()
{
    std::vector<std::filesystem::path> paths;
    paths.reserve(AllShaderPrograms.size());
    for (const ShaderProgram program : AllShaderPrograms)
    {
        paths.push_back(GetShaderRelativePath(program));
    }
    return paths;
}

}
