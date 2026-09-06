#include "TestSupport.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ios>
#include <string>
#include <string_view>
#include <functional>
#include <iostream>
#include <system_error>
#include <vector>

#include <chrono>
#include <optional>
#include <thread>

#include "App/GameBootstrapRegistry.h"
#include "Core/TextFile.h"
#include "Platform/PlatformServices.h"
#include "Platform/ProcessRun.h"

namespace TestSupport
{

CommandResult RunCommand(
    const std::filesystem::path& executable, const std::vector<std::string>& arguments,
    const std::filesystem::path& workingDirectory)
{
    GameEngine::Platform::ProcessRequest request;
    request.executable = executable;
    request.arguments = arguments;
    request.workingDirectory = workingDirectory;

    CommandResult result;
    result.exitCode = GameEngine::Platform::RunProcessToCompletion(
        request, [&result](const std::string_view line)
        {
            result.output.append(line);
            result.output.push_back('\n');
        });
    return result;
}

TemporaryDirectory::TemporaryDirectory(const std::string_view name)
    : mPath(
          std::filesystem::temp_directory_path() /
          ("GameEngineTests-" + std::to_string(GetCurrentProcessId()) +
              (name.empty() ? std::string{} : "-" + std::string(name))))
{
    std::error_code error;
    std::filesystem::remove_all(mPath, error);
    std::filesystem::create_directories(mPath);
}

TemporaryDirectory::~TemporaryDirectory()
{
    std::error_code error;
    std::filesystem::remove_all(mPath, error);
}

bool WriteFile(const std::filesystem::path& path, const std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(stream);
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

bool Expect(const bool condition, const char* const message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << "\n";
    }
    return condition;
}

bool ForEachUiScale(const std::function<bool(float scale)>& check)
{
    bool passed = true;
    for (const float scale : { 1.0f, 2.0f })
    {
        if (!check(scale))
        {
            std::cerr << "  the check above failed at scale " << scale << "\n";
            passed = false;
        }
    }
    return passed;
}

}

namespace TestSupport
{

RegistryScope::RegistryScope()
    : mImporters(GameEngine::Assets::AssetImporterRegistry::Snapshot())
    , mComponentFactories(GameEngine::Serialization::ComponentFactory::Snapshot())
{
}

RegistryScope::~RegistryScope()
{
    GameEngine::Assets::AssetImporterRegistry::Restore(mImporters);
    GameEngine::Serialization::ComponentFactory::Restore(mComponentFactories);
    GameEngine::App::GameBootstrapRegistry::Reset();
}

std::optional<std::string> ReadFileWhenSettled(const std::filesystem::path& path)
{
    // 50밀리초씩 스무 번, 합쳐서 1초다. 이름 바꾸기가 열리지 않게 만드는 창은 그보다 훨씬
    // 짧으므로 넉넉하고, 정말 못 읽는 파일 앞에서 시험이 멈춰 있는 시간으로는 짧다.
    constexpr int Attempts = 20;
    for (int attempt = 0; attempt < Attempts; ++attempt)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            return std::nullopt;
        }
        if (std::optional<std::string> contents = GameEngine::Core::ReadTextFile(path))
        {
            return contents;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return std::nullopt;
}

std::unique_ptr<GameEngine::Platform::ITextRasterizer> CreateTestTextRasterizer(
    const std::string_view familyAlias)
{
    // 이 소스 파일과 나란한 GameEditor/Content/Fonts에서 시험용 폰트를 찾는다. 저장소 루트를
    // __FILE__에서 구하면, 시험을 어느 디렉터리에서 돌리든 같은 폰트를 찾는다.
    const std::filesystem::path fontPath = std::filesystem::path(__FILE__).parent_path()
        .parent_path() / "GameEditor" / "Content" / "Fonts" / "D2Coding-Ver1.3.3-20260725.ttf";

    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return nullptr;
    }
    const std::streamoff size = file.tellg();
    if (size <= 0)
    {
        return nullptr;
    }
    std::vector<std::byte> fontBytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(fontBytes.data()), size);
    if (!file)
    {
        return nullptr;
    }

    std::unique_ptr<GameEngine::Platform::ITextRasterizer> rasterizer =
        GameEngine::Platform::PlatformServices::CreateTextRasterizer();
    if (!rasterizer || !rasterizer->Initialize() ||
        !rasterizer->RegisterFont(familyAlias, fontBytes))
    {
        return nullptr;
    }
    return rasterizer;
}

}

namespace
{
    /// <summary>등록부의 유일한 사본이다. 첫 등록이 이것을 만든다.</summary>
    [[nodiscard]] std::vector<TestSupport::RegisteredTest>& MutableRegistry()
    {
        static std::vector<TestSupport::RegisteredTest> registry;
        return registry;
    }
}

namespace TestSupport
{

const std::vector<RegisteredTest>& RegisteredTests()
{
    return MutableRegistry();
}

Registration::Registration(
    const std::string_view suite, const std::string_view description, bool (*const run)())
{
    MutableRegistry().push_back(RegisteredTest{ suite, description, run });
}

}
