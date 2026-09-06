#pragma once

/// <summary>앵커·오프셋 선언 하나가 부모 사각형 안에서 어떤 사각형이 되는지를 검증한다.</summary>
bool RunRectResolutionTests();

/// <summary>UI 계층 전체가 화면 크기에 맞춰 위에서 아래로 배치되는 것을 검증한다.</summary>
bool RunUILayoutHierarchyTests();

/// <summary>배치된 사각형이 프레임의 오버레이 draw로 그대로 실리는 것을 검증한다.</summary>
bool RunUISurfaceSubmissionTests();

/// <summary>마스크가 자기 아래의 UI를 요소 단위로 잘라 내는 것을 검증한다.</summary>
bool RunRectMaskTests();

/// <summary>요소가 자기 내용에 맞는 크기를 요구하고 배치가 그것을 존중하는지 검증한다.</summary>
bool RunLayoutElementTests();

/// <summary>재는 쪽과 그리는 쪽이 같은 글자 캐시를 나눠 쥐는지 검증한다.</summary>
bool RunSharedTextCacheTests();

/// <summary>컨테이너가 자식의 수가 아니라 자식들이 요구하는 크기를 더하는지 검증한다.</summary>
bool RunContentDemandTests();

/// <summary>접히는 요소가 폭을 받아들이고 그 폭에서 필요한 높이를 요구하는지 검증한다.</summary>
bool RunTextWrappingTests();

/// <summary>재기와 그리기가 같은 문자열에 대해 캐시 항목을 하나만 쓰는지 세어서 검증한다.</summary>
bool RunTextCacheFootprintTests();

/// <summary>에디터 자신의 UI가 편집 중인 프로젝트로 새지 않는지 검증한다.</summary>
bool RunEditorSurfaceIsolationTests();

/// <summary>그림 없는 UI 요소가 단색 사각형으로 그려지는지 검증한다.</summary>
bool RunSolidRectTests();

/// <summary>ContentFit이 자식을 순서대로 쌓고, 꺼진 자식은 자리를 차지하지 않는지 확인한다.</summary>
bool RunLayoutStackingTests();
