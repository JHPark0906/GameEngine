#include "RelativePathTests.h"

#include <filesystem>
#include <optional>
#include <string>

#include "Core/RelativePath.h"
#include "TestSupport.h"

using TestSupport::Expect;

bool RunRelativePathTests()
{
    using GameEngine::Core::EscapesRoot;
    using GameEngine::Core::RelativePathWithin;

    const std::filesystem::path root = "C:/Projects/Game";
    const auto within = [&root](const std::filesystem::path& path)
    {
        const std::optional<std::filesystem::path> relative = RelativePathWithin(root, path);
        return relative ? relative->generic_string() : std::string{ "(none)" };
    };

    // 루트 안의 절대 경로는 그 안에서의 자리로 옮겨진다.
    const bool takesPathsInside = within("C:/Projects/Game/Scenes/Main.scene") ==
        "Scenes/Main.scene";
    // 이미 상대인 것은 루트 기준인 것으로 본다 — 콘텐츠 소스가 말하는 언어가 그것이다.
    const bool acceptsRelative = within("Scenes/Main.scene") == "Scenes/Main.scene";
    // 🔴 구분자는 하나로 모인다. 같은 경로가 두 글자로 갈라지면 그 둘은 같은 것을 가리키면서
    // 서로 다른 조회 키가 된다.
    const bool unifiesSeparators = within("C:\\Projects\\Game\\Scenes\\Main.scene") ==
        "Scenes/Main.scene" && within("Scenes\\Main.scene") == "Scenes/Main.scene";
    // 중복 슬래시와 가운데의 . 도 다듬는다.
    const bool tidiesTheMiddle = within("Scenes//./Main.scene") == "Scenes/Main.scene";

    // 여러 경로 요소 뒤의 상위 디렉터리 이동도 루트 밖으로 나가면 거절해야 한다.
    const bool refusesTraversalInTheMiddle = within("Scenes/../../outside.scene") == "(none)";
    const bool refusesTraversalAtTheFront = within("../outside.scene") == "(none)";
    // 다듬고 나면 루트 안에 남는 .. 는 나가는 것이 아니다. 지나치게 거절하면 멀쩡한 경로가 죽는다.
    const bool keepsHarmlessTraversal = within("Scenes/../Assets/tile.png") == "Assets/tile.png";

    const bool refusesOutside = within("C:/Elsewhere/other.scene") == "(none)";
    const bool refusesTheRootItself = within("C:/Projects/Game") == "(none)";
    const bool refusesEmpty = within("") == "(none)";
    // 루트가 없으면 절대 경로는 옮길 수 없지만, 이미 상대인 것은 그대로 답할 수 있다.
    const bool needsARootForAbsolutePaths =
        !RelativePathWithin("", "C:/Projects/Game/Scenes/Main.scene").has_value() &&
        RelativePathWithin("", "Scenes/Main.scene").has_value();

    // 대소문자는 그대로 둔다. Windows의 파일 시스템이 그것을 무시할 뿐, 경로의 글자는 사람이
    // 적은 것이고 화면에 그대로 보여야 한다 — 조회 키를 만드는 자리가 따로 낮춘다.
    const bool keepsCase = within("C:/Projects/Game/Scenes/MainScene.scene") ==
        "Scenes/MainScene.scene";

    // 🔴 나가는지만 묻는 쪽은 경로를 <b>적힌 그대로</b> 본다. 다듬고 나서 보는 것은 위의
    // RelativePathWithin이고, 이쪽은 밖에서 들어온 상대 경로를 그대로 의심하는 자리들이 쓴다 —
    // 그래서 a/../b 처럼 결국 안에 남는 것도 거절한다. 두 답이 다른 것이 규칙이고, 그것을
    // 여기서 못 박는다.
    const bool answersEscapesDirectly = EscapesRoot("../x") && EscapesRoot("a/../../x") &&
        EscapesRoot("C:/absolute") && EscapesRoot("a/../b") && !EscapesRoot("a/b");

    return Expect(takesPathsInside, "a path inside the root should become its place inside it") &&
        Expect(acceptsRelative, "a path that is already relative should be taken as it is") &&
        Expect(unifiesSeparators, "separators should come out one way, whichever way they went in") &&
        Expect(tidiesTheMiddle, "doubled slashes and a bare dot should be tidied away") &&
        Expect(
            refusesTraversalInTheMiddle,
            "a path that climbs out later in its middle should be refused") &&
        Expect(refusesTraversalAtTheFront, "a path that starts by climbing out should be refused") &&
        Expect(keepsHarmlessTraversal, "climbing back down inside the root is not leaving it") &&
        Expect(refusesOutside, "a path under another root should be refused") &&
        Expect(refusesTheRootItself, "the root is not a place inside itself") &&
        Expect(refusesEmpty, "an empty path names nothing and should be refused") &&
        Expect(needsARootForAbsolutePaths, "an absolute path needs a root to be measured against") &&
        Expect(keepsCase, "the letters a person wrote should come back as they wrote them") &&
        Expect(
            answersEscapesDirectly,
            "asking whether a path leaves should judge it as written, not tidied");
}

static const TestSupport::Registration gRelativePathTests{
    "Core", "relative path tests should pass", RunRelativePathTests };
