#include "Rules/EditorScriptTemplate.h"

#include <system_error>

#include "App/ProjectFile.h"
#include "Core/TextFile.h"
#include "Diagnostics/Debug.h"
#include "Rules/EditorPanelCommon.h"

namespace GameEditor
{

namespace
{
    /// <summary>파일 하나를 통째로 쓴다. 이미 있으면 쓰지 않는다.</summary>
    [[nodiscard]] bool WriteNewFile(
        const std::filesystem::path& filePath, const std::string& contents)
    {
        const GameEngine::Core::FileWriteResult written =
            GameEngine::Core::WriteTextFile(filePath, contents);
        if (!written)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Failed to write a new script file. path=", filePath.string(),
                ", reason=", written.Describe());
            return false;
        }
        return true;
    }

    /// <summary>컴포넌트 선언이다. 속성 하나를 들고 있어 인스펙터에 무엇이 나오는지 보인다.</summary>
    [[nodiscard]] std::string MakeHeader(
        const std::string& projectName, const std::string& scriptName)
    {
        return
            "#pragma once\n"
            "\n"
            "#include \"Runtime/MonoBehaviour.h\"\n"
            "\n"
            "namespace " + projectName + "\n"
            "{\n"
            "\n"
            "/// <summary>" + scriptName + " 컴포넌트다.</summary>\n"
            "class " + scriptName + " final : public GameEngine::Runtime::MonoBehaviour\n"
            "{\n"
            "public:\n"
            "    /// <summary>이 컴포넌트 클래스의 정체성이다. 쿼리, 도구, 진단이 공유한다.</summary>\n"
            "    [[nodiscard]] static const GameEngine::Runtime::ComponentType& StaticType();\n"
            "    [[nodiscard]] const GameEngine::Runtime::ComponentType& GetComponentType() const"
            " override\n"
            "    {\n"
            "        return StaticType();\n"
            "    }\n"
            "\n"
            "    /// <summary>인스펙터에 나오는 값이다. 속성 선언이 곧 장면 파일의 형식이다.</summary>\n"
            "    [[nodiscard]] float GetSpeed() const { return mSpeed; }\n"
            "    void SetSpeed(const float speed) { mSpeed = speed; }\n"
            "\n"
            "protected:\n"
            "    void Update(float deltaTime) override;\n"
            "\n"
            "private:\n"
            "    float mSpeed = 1.0f;\n"
            "};\n"
            "\n"
            "}\n";
    }

    /// <summary>정의와 등록이다. 등록이 이 파일 끝에 있는 이유는 그 자리 주석이 말한다.</summary>
    [[nodiscard]] std::string MakeSource(
        const std::string& projectName, const std::string& scriptName)
    {
        return
            "#include \"" + scriptName + ".h\"\n"
            "\n"
            "#include \"Runtime/ComponentType.h\"\n"
            "#include \"Runtime/PropertyDescriptor.h\"\n"
            "#include \"Serialization/RuntimeComponentFactories.h\"\n"
            "\n"
            "#include <span>\n"
            "\n"
            "namespace " + projectName + "\n"
            "{\n"
            "\n"
            "namespace\n"
            "{\n"
            "    /// <summary>이 컴포넌트가 인스펙터와 장면 파일에 내놓는 값들이다.</summary>\n"
            "    std::span<const GameEngine::Runtime::PropertyDescriptor> " + scriptName +
            "Properties()\n"
            "    {\n"
            "        static const GameEngine::Runtime::PropertyDescriptor descriptors[] = {\n"
            "            GameEngine::Runtime::MakeProperty<" + scriptName + ">(\n"
            "                \"speed\", \"Speed\", &" + scriptName + "::GetSpeed,\n"
            "                &" + scriptName + "::SetSpeed) };\n"
            "        return descriptors;\n"
            "    }\n"
            "}\n"
            "\n"
            "const GameEngine::Runtime::ComponentType& " + scriptName + "::StaticType()\n"
            "{\n"
            "    static const GameEngine::Runtime::ComponentType type{\n"
            "        \"" + scriptName + "\", &GameEngine::Runtime::MonoBehaviour::StaticType(),\n"
            "        &" + scriptName + "Properties,\n"
            "        &GameEngine::Runtime::MakeComponentInstance<" + scriptName + "> };\n"
            "    return type;\n"
            "}\n"
            "\n"
            "void " + scriptName + "::Update(const float deltaTime)\n"
            "{\n"
            "    static_cast<void>(deltaTime);\n"
            "}\n"
            "\n"
            "namespace\n"
            "{\n"
            "    // 프로세스가 시작할 때 이 컴포넌트를 등록한다. 등록이 이 번역 단위에 살아도 되는\n"
            "    // 이유는 프로젝트의 소스가 OBJECT 라이브러리로 묶여 실행 파일에 <b>명시적으로</b>\n"
            "    // 링크되기 때문이다 — 오브젝트가 링크 줄에 직접 얹히므로 링커가 버릴 수 없다.\n"
            "    // 정적 라이브러리였다면 아무 심볼도 이 초기화자를 부르지 않으므로 조용히 사라지고,\n"
            "    // 빌드는 통과하는데 Play 모드에만 이 컴포넌트가 없다.\n"
            "    const bool g" + scriptName + "Registered =\n"
            "        GameEngine::Serialization::RegisterComponentType(" + scriptName +
            "::StaticType());\n"
            "}\n"
            "\n"
            "}\n";
    }
}

std::optional<CreatedScript> CreateComponentScript(
    const std::filesystem::path& projectRoot, const std::string_view projectName,
    const std::filesystem::path& contentDirectory,
    const std::filesystem::path& chosenHeaderPath,
    const std::filesystem::path& iconPath, const std::string_view iconGuid)
{
    const std::string scriptName = ToUtf8(chosenHeaderPath.stem().wstring());
    if (!GameEngine::App::ProjectFile::IsUsableAsIdentifier(scriptName))
    {
        GameEngine::Diagnostics::Debug::LogError(
            "A component name has to be usable as a C++ class name: letters, digits and "
            "underscores, starting with a letter, and not a reserved word. name=", scriptName);
        return std::nullopt;
    }

    const std::filesystem::path sourceDirectory = (projectRoot / "Source").lexically_normal();
    const std::filesystem::path chosenDirectory =
        chosenHeaderPath.parent_path().lexically_normal();
    if (chosenDirectory != sourceDirectory)
    {
        // 고른 자리를 말없이 옮기지 않는다. 빌드 스크립트가 Source/ 하나만 훑고 그것도 재귀가
        // 아니므로, 다른 자리에 만들어 주면 파일은 생기는데 어떤 빌드에도 들어가지 않는다 —
        // 사람이 보기에는 만들었는데 컴포넌트가 나타나지 않는 상태가 된다.
        GameEngine::Diagnostics::Debug::LogError(
            "A component has to be created directly in the project's Source directory, because "
            "that is the only place the build script looks and it does not look inside "
            "subdirectories. chosen=", chosenDirectory.string(),
            ", expected=", sourceDirectory.string());
        return std::nullopt;
    }

    std::error_code error;
    std::filesystem::create_directories(sourceDirectory, error);
    if (error)
    {
        GameEngine::Diagnostics::Debug::LogError(
            "Failed to create the project's Source directory. path=", sourceDirectory.string(),
            ", error=", error.message());
        return std::nullopt;
    }

    CreatedScript created;
    created.headerPath = sourceDirectory / (scriptName + ".h");
    created.sourcePath = sourceDirectory / (scriptName + ".cpp");

    // 같은 이름이 이미 있으면 덮어쓰지 않는다. 사람이 쓴 컴포넌트를 템플릿으로 지우는 것은
    // 되돌릴 수 없다.
    for (const std::filesystem::path& path : { created.headerPath, created.sourcePath })
    {
        if (std::filesystem::exists(path, error) && !error)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "A component with that name is already in this project. path=", path.string());
            return std::nullopt;
        }
        if (error)
        {
            GameEngine::Diagnostics::Debug::LogError(
                "Failed to inspect a script path. path=", path.string(),
                ", error=", error.message());
            return std::nullopt;
        }
    }

    if (!WriteNewFile(created.headerPath, MakeHeader(std::string(projectName), scriptName)))
    {
        return std::nullopt;
    }
    if (!WriteNewFile(created.sourcePath, MakeSource(std::string(projectName), scriptName)))
    {
        std::filesystem::remove(created.headerPath, error);
        return std::nullopt;
    }

    // 이 프로젝트가 코드를 처음 갖는 경우다. 빌드 스크립트가 없으면 지금 놓는다 — 없으면 이
    // 파일들이 어떤 빌드에도 들어가지 않는다.
    const std::filesystem::path buildScript = projectRoot / "CMakeLists.txt";
    const bool hadBuildScript = std::filesystem::exists(buildScript, error) && !error;
    if (!hadBuildScript)
    {
        created.wroteBuildScript = GameEngine::App::ProjectFile::WriteBuildScript(
            projectRoot, projectName, contentDirectory, iconPath, iconGuid);
    }
    return created;
}

}
