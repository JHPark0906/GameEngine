#pragma once

// 엔진의 미리 컴파일된 헤더다. 컴파일 시간을 줄이는 캐시일 뿐이며, 그 외의 어떤 역할도 갖지
// 않는다. 그래서 여기에는 표준 라이브러리 — 크고, 안정적이고, 이 저장소가 바꾸지 않는 것 — 만
// 있다. 엔진 자신의 헤더는 하나도 없다: 그것을 넣는 순간 모든 번역 단위가 선언 없이 그것에
// 의존하고, 이 파일을 비우면 빌드가 깨진다. 모든 파일은 자기가 쓰는 것을 직접 포함해야 하며,
// 이 파일은 비워도 빌드가 되어야 한다.
//
// Windows, DirectX, 그래픽스 API 헤더는 절대 넣지 않는다. 그것은 플랫폼과 백엔드 디렉터리의
// 번역 단위가 각자 포함한다.

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>
