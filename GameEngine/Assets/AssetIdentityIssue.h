#pragma once

#include <cstddef>

namespace GameEngine::Platform
{
class IContentSource;
}

namespace GameEngine::Assets
{

class AssetDatabase;

/// <summary>
/// 정체성이 없는 에셋마다 하나를 발급해 사이드카에 적는다. 발급한 수를 돌려준다.
///
/// <b>이미 있는 정체성은 절대 덮지 않는다.</b> 다시 발급하면 그것을 가리키던 모든 참조가
/// 아무것도 가리키지 않게 되므로, 있는 것이 언제나 이긴다. 사이드카가 있는데 정체성만 없는
/// 파일에는 그 파일에 정체성을 더한다 — 새 파일을 만들면 한 에셋에 설정이 둘이 되고, 사람이
/// 적어 둔 값은 새 것에 없다.
///
/// 매니페스트는 정체성 없는 에셋을 거부한다. 편집기와 빌더가 이 함수를 공유하므로,
/// 스크립트나 CI가 만든 프로젝트도 편집기를 열지 않고 정체성을 발급받아 패키징할 수 있다.
///
/// 이것은 프로젝트에 <b>파일을 만든다</b>. 콘텐츠 소스는 읽기만 하므로 쓰기는 경로로 일어나며,
/// 그래서 소스와 데이터베이스를 함께 받는다 — 읽는 것은 소스에게, 어디에 쓸지는 데이터베이스가
/// 아는 프로젝트 루트에게 묻는다.
///
/// 발급한 뒤의 데이터베이스는 <b>아직 그 정체성을 모른다.</b> 사이드카는 디스크에만 있으므로,
/// 부르는 쪽이 다시 스캔해야 방금 발급된 에셋이 자기 정체성으로 조회된다.
/// </summary>
/// <param name="database">발급 대상 에셋들을 담은 데이터베이스다.</param>
/// <param name="source">기존 사이드카를 읽을 콘텐츠 소스다.</param>
/// <returns>새로 정체성을 받은 에셋 수다.</returns>
[[nodiscard]] std::size_t IssueMissingIdentities(
    const AssetDatabase& database, const Platform::IContentSource& source);

}
