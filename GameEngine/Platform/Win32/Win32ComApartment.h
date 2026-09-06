#pragma once

namespace GameEngine::Platform::Win32
{

/// <summary>
/// COM을 쓰는 컴포넌트(WIC 이미지 디코드, XAudio2 등)를 위한 멀티스레드 아파트먼트 참여를
/// 소유한다. 다른 컴포넌트가 이미 다른 아파트먼트 모델에 넣어 둔 스레드는 준비된 것으로
/// 받아들이고, 이 객체가 실제로 들어간 아파트먼트만 해제한다.
/// </summary>
class ComApartment final
{
public:
    ComApartment() = default;
    ~ComApartment();

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    ComApartment(ComApartment&&) = delete;
    ComApartment& operator=(ComApartment&&) = delete;

    /// <summary>필요하면 아파트먼트에 들어간다. 성공 뒤의 반복 호출은 아무 일도 하지 않는다.</summary>
    /// <returns>호출 스레드가 COM 객체를 만들 수 있으면 true이다.</returns>
    [[nodiscard]] bool Initialize();

    [[nodiscard]] bool IsReady() const { return mReady; }

private:
    bool mReady = false;
    bool mUninitializeRequired = false;
};

}
