#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace GameEngine::Core
{

/// <summary>JSON 파싱이나 조회가 실패했을 때 던져지는 오류이다.</summary>
class JsonError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// <summary>파싱된 JSON 값 하나이다. 객체, 배열, 문자열, 숫자, 불리언, null 중 하나를 담는다.</summary>
class Json final
{
public:
    using Array = std::vector<Json>;
    using Object = std::unordered_map<std::string, Json>;

    Json() = default;
    explicit Json(std::nullptr_t);
    explicit Json(bool value);
    explicit Json(double value);
    explicit Json(std::string value);
    explicit Json(Array value);
    explicit Json(Object value);

    [[nodiscard]] bool IsNull() const noexcept;
    [[nodiscard]] bool IsBoolean() const noexcept;
    [[nodiscard]] bool IsNumber() const noexcept;
    [[nodiscard]] bool IsString() const noexcept;
    [[nodiscard]] bool IsArray() const noexcept;
    [[nodiscard]] bool IsObject() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;

    [[nodiscard]] const Json* Find(const std::string& key) const noexcept;
    [[nodiscard]] const Json& At(const std::string& key) const;
    [[nodiscard]] const Json& At(std::size_t index) const;
    [[nodiscard]] const Array& AsArray() const;
    [[nodiscard]] const Object& AsObject() const;
    [[nodiscard]] const std::string& AsString() const;
    [[nodiscard]] bool AsBoolean() const;
    [[nodiscard]] double AsNumber() const;

    template<typename T>
    [[nodiscard]] T Get() const;

    template<typename T>
    [[nodiscard]] T Value(const std::string& key, T fallback) const
    {
        const Json* value = Find(key);
        if (!value)
        {
            return fallback;
        }
        try
        {
            return value->Get<T>();
        }
        catch (const JsonError&)
        {
            return fallback;
        }
    }

    /// <summary>
    /// 이 값을 다시 JSON 텍스트로 쓴다.
    ///
    /// 객체 키는 정렬해서 쓴다. 저장 컨테이너가 순서를 약속하지 않으므로, 정렬 없이는 같은 값이
    /// 실행마다 다른 텍스트가 됐을 것이다. 숫자는 왕복이 보장되는 최단 표기로 쓴다. 원래 텍스트를
    /// 재현하는 것이 아니라 같은 값을 결정적으로 쓰는 것이 약속이다.
    /// </summary>
    [[nodiscard]] std::string Dump() const;

    [[nodiscard]] static Json Parse(std::string_view text);

    /// <summary>
    /// 콘텐츠 소스가 반환한 바이트를 파싱한다. 엔진이 읽는 것은 바이트로 도착하므로, 캐스팅을
    /// 호출 지점마다 손으로 적지 않고 여기서 한 번 한다.
    /// </summary>
    [[nodiscard]] static Json ParseBytes(std::span<const std::byte> bytes);

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage mValue = nullptr;
};

template<> [[nodiscard]] bool Json::Get<bool>() const;
template<> [[nodiscard]] int Json::Get<int>() const;
template<> [[nodiscard]] unsigned int Json::Get<unsigned int>() const;
template<> [[nodiscard]] float Json::Get<float>() const;
template<> [[nodiscard]] double Json::Get<double>() const;
template<> [[nodiscard]] std::string Json::Get<std::string>() const;

}
