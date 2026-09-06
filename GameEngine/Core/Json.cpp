#include "pch.h"
#include "Json.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace GameEngine::Core
{

namespace
{
    class Parser final
    {
    public:
        explicit Parser(std::string_view text) : mText(text) {}

        Json ParseDocument()
        {
            SkipByteOrderMark();
            SkipWhitespace();
            Json result = ParseValue(0);
            SkipWhitespace();
            if (mPosition != mText.size()) Fail("unexpected trailing data");
            return result;
        }

    private:
        static constexpr std::size_t MaxDepth = 256;

        [[noreturn]] void Fail(const char* message) const
        {
            std::size_t line = 1;
            std::size_t column = 1;
            for (std::size_t index = 0; index < mPosition && index < mText.size(); ++index)
            {
                if (mText[index] == '\n') { ++line; column = 1; }
                else { ++column; }
            }
            throw JsonError(
                std::string(message) + " at line " + std::to_string(line) +
                ", column " + std::to_string(column));
        }

        /// <summary>
        /// 문서 맨 앞의 UTF-8 BOM을 지나친다. 값의 일부가 아니므로 문서의 시작에서만 본다.
        ///
        /// 메모장을 비롯한 편집기들이 UTF-8로 저장하며 이 세 바이트를 앞에 붙인다. 사람이 손으로
        /// 쓰는 파일 — 에셋 사이드카가 그렇다 — 이 그렇게 저장되면, 그것을 거절하는 파서는
        /// 사람이 보기에 멀쩡한 파일을 읽지 못한다.
        /// </summary>
        void SkipByteOrderMark()
        {
            // 바이트를 값으로 적는다. 표시로는 보이지 않는 세 바이트라, 소스에 그대로 넣으면
            // 이 줄이 무엇을 말하는지 읽어서 알 수 없고 파일의 인코딩에 기대게 된다.
            constexpr unsigned char First = 0xEF;
            constexpr unsigned char Second = 0xBB;
            constexpr unsigned char Third = 0xBF;
            constexpr std::size_t Length = 3;
            if (mPosition == 0 && mText.size() >= Length &&
                static_cast<unsigned char>(mText[0]) == First &&
                static_cast<unsigned char>(mText[1]) == Second &&
                static_cast<unsigned char>(mText[2]) == Third)
            {
                mPosition += Length;
            }
        }

        void SkipWhitespace()
        {
            while (mPosition < mText.size() &&
                (mText[mPosition] == ' ' || mText[mPosition] == '\t' ||
                 mText[mPosition] == '\r' || mText[mPosition] == '\n'))
            {
                ++mPosition;
            }
        }

        bool Consume(char value)
        {
            if (mPosition < mText.size() && mText[mPosition] == value)
            {
                ++mPosition;
                return true;
            }
            return false;
        }

        Json ParseValue(std::size_t depth)
        {
            if (depth > MaxDepth) Fail("maximum nesting depth exceeded");
            if (mPosition >= mText.size()) Fail("expected a value");
            switch (mText[mPosition])
            {
            case 'n': ParseLiteral("null"); return Json(nullptr);
            case 't': ParseLiteral("true"); return Json(true);
            case 'f': ParseLiteral("false"); return Json(false);
            case '"': return Json(ParseString());
            case '[': return ParseArray(depth + 1);
            case '{': return ParseObject(depth + 1);
            default:
                if (mText[mPosition] == '-' ||
                    (mText[mPosition] >= '0' && mText[mPosition] <= '9'))
                {
                    return Json(ParseNumber());
                }
                Fail("invalid value");
            }
        }

        void ParseLiteral(std::string_view literal)
        {
            if (mText.substr(mPosition, literal.size()) != literal) Fail("invalid literal");
            mPosition += literal.size();
        }

        static void AppendUtf8(std::string& output, std::uint32_t codePoint)
        {
            if (codePoint <= 0x7f) output.push_back(static_cast<char>(codePoint));
            else if (codePoint <= 0x7ff)
            {
                output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
            else if (codePoint <= 0xffff)
            {
                output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
            else
            {
                output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
        }

        std::uint32_t ParseHex4()
        {
            if (mPosition + 4 > mText.size()) Fail("incomplete unicode escape");
            std::uint32_t value = 0;
            for (int index = 0; index < 4; ++index)
            {
                const char character = mText[mPosition++];
                value <<= 4;
                if (character >= '0' && character <= '9') value |= character - '0';
                else if (character >= 'a' && character <= 'f') value |= character - 'a' + 10;
                else if (character >= 'A' && character <= 'F') value |= character - 'A' + 10;
                else Fail("invalid unicode escape");
            }
            return value;
        }

        std::string ParseString()
        {
            if (!Consume('"')) Fail("expected a string");
            std::string result;
            while (mPosition < mText.size())
            {
                const unsigned char character = static_cast<unsigned char>(mText[mPosition++]);
                if (character == '"') return result;
                if (character < 0x20) Fail("unescaped control character");
                if (character != '\\') { result.push_back(static_cast<char>(character)); continue; }
                if (mPosition >= mText.size()) Fail("incomplete escape sequence");
                switch (mText[mPosition++])
                {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u':
                {
                    std::uint32_t codePoint = ParseHex4();
                    if (codePoint >= 0xd800 && codePoint <= 0xdbff)
                    {
                        if (mPosition + 2 > mText.size() || mText[mPosition] != '\\' ||
                            mText[mPosition + 1] != 'u') Fail("missing low surrogate");
                        mPosition += 2;
                        const std::uint32_t low = ParseHex4();
                        if (low < 0xdc00 || low > 0xdfff) Fail("invalid low surrogate");
                        codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                    }
                    else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) Fail("unexpected low surrogate");
                    AppendUtf8(result, codePoint);
                    break;
                }
                default: Fail("invalid escape sequence");
                }
            }
            Fail("unterminated string");
        }

        double ParseNumber()
        {
            const std::size_t start = mPosition;
            Consume('-');
            if (Consume('0'))
            {
                if (mPosition < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPosition])))
                    Fail("leading zero in number");
            }
            else
            {
                if (mPosition >= mText.size() || mText[mPosition] < '1' || mText[mPosition] > '9')
                    Fail("invalid number");
                while (mPosition < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPosition]))) ++mPosition;
            }
            if (Consume('.'))
            {
                if (mPosition >= mText.size() || !std::isdigit(static_cast<unsigned char>(mText[mPosition])))
                    Fail("invalid fraction");
                while (mPosition < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPosition]))) ++mPosition;
            }
            if (mPosition < mText.size() && (mText[mPosition] == 'e' || mText[mPosition] == 'E'))
            {
                ++mPosition;
                if (mPosition < mText.size() && (mText[mPosition] == '+' || mText[mPosition] == '-')) ++mPosition;
                if (mPosition >= mText.size() || !std::isdigit(static_cast<unsigned char>(mText[mPosition])))
                    Fail("invalid exponent");
                while (mPosition < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPosition]))) ++mPosition;
            }
            double result = 0.0;
            const auto conversion = std::from_chars(
                mText.data() + start, mText.data() + mPosition, result, std::chars_format::general);
            if (conversion.ec != std::errc{} || !std::isfinite(result)) Fail("number is out of range");
            return result;
        }

        Json ParseArray(std::size_t depth)
        {
            Consume('['); SkipWhitespace();
            Json::Array result;
            if (Consume(']')) return Json(std::move(result));
            while (true)
            {
                result.push_back(ParseValue(depth)); SkipWhitespace();
                if (Consume(']')) return Json(std::move(result));
                if (!Consume(',')) Fail("expected ',' or ']'");
                SkipWhitespace();
            }
        }

        Json ParseObject(std::size_t depth)
        {
            Consume('{'); SkipWhitespace();
            Json::Object result;
            if (Consume('}')) return Json(std::move(result));
            while (true)
            {
                if (mPosition >= mText.size() || mText[mPosition] != '"') Fail("expected an object key");
                std::string key = ParseString(); SkipWhitespace();
                if (!Consume(':')) Fail("expected ':'");
                SkipWhitespace();
                Json value = ParseValue(depth);
                if (!result.emplace(std::move(key), std::move(value)).second) Fail("duplicate object key");
                SkipWhitespace();
                if (Consume('}')) return Json(std::move(result));
                if (!Consume(',')) Fail("expected ',' or '}'");
                SkipWhitespace();
            }
        }

        std::string_view mText;
        std::size_t mPosition = 0;
    };
}

Json::Json(std::nullptr_t) : mValue(nullptr) {}
Json::Json(bool value) : mValue(value) {}
Json::Json(double value) : mValue(value) {}
Json::Json(std::string value) : mValue(std::move(value)) {}
Json::Json(Array value) : mValue(std::move(value)) {}
Json::Json(Object value) : mValue(std::move(value)) {}
bool Json::IsNull() const noexcept { return std::holds_alternative<std::nullptr_t>(mValue); }
bool Json::IsBoolean() const noexcept { return std::holds_alternative<bool>(mValue); }
bool Json::IsNumber() const noexcept { return std::holds_alternative<double>(mValue); }
bool Json::IsString() const noexcept { return std::holds_alternative<std::string>(mValue); }
bool Json::IsArray() const noexcept { return std::holds_alternative<Array>(mValue); }
bool Json::IsObject() const noexcept { return std::holds_alternative<Object>(mValue); }
std::size_t Json::Size() const noexcept { return IsArray() ? std::get<Array>(mValue).size() : IsObject() ? std::get<Object>(mValue).size() : 0; }
const Json* Json::Find(const std::string& key) const noexcept
{
    if (!IsObject()) return nullptr;
    const Object& object = std::get<Object>(mValue);
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->second;
}
const Json& Json::At(const std::string& key) const
{
    const Json* value = Find(key);
    if (!value) throw JsonError("missing JSON member: " + key);
    return *value;
}
const Json& Json::At(std::size_t index) const
{
    if (!IsArray() || index >= std::get<Array>(mValue).size()) throw JsonError("JSON array index out of range");
    return std::get<Array>(mValue)[index];
}
const Json::Array& Json::AsArray() const { if (!IsArray()) throw JsonError("expected JSON array"); return std::get<Array>(mValue); }
const Json::Object& Json::AsObject() const { if (!IsObject()) throw JsonError("expected JSON object"); return std::get<Object>(mValue); }
const std::string& Json::AsString() const { if (!IsString()) throw JsonError("expected JSON string"); return std::get<std::string>(mValue); }
bool Json::AsBoolean() const { if (!IsBoolean()) throw JsonError("expected JSON boolean"); return std::get<bool>(mValue); }
double Json::AsNumber() const { if (!IsNumber()) throw JsonError("expected JSON number"); return std::get<double>(mValue); }

template<> bool Json::Get<bool>() const { return AsBoolean(); }
template<> int Json::Get<int>() const
{
    const double value = AsNumber();
    if (std::trunc(value) != value || value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)())
        throw JsonError("JSON number cannot be represented as int");
    return static_cast<int>(value);
}
template<> unsigned int Json::Get<unsigned int>() const
{
    const double value = AsNumber();
    if (std::trunc(value) != value || value < 0.0 || value > (std::numeric_limits<unsigned int>::max)())
        throw JsonError("JSON number cannot be represented as unsigned int");
    return static_cast<unsigned int>(value);
}
template<> float Json::Get<float>() const
{
    const double value = AsNumber();
    if (value < -(std::numeric_limits<float>::max)() || value > (std::numeric_limits<float>::max)())
        throw JsonError("JSON number cannot be represented as float");
    return static_cast<float>(value);
}
template<> double Json::Get<double>() const { return AsNumber(); }
template<> std::string Json::Get<std::string>() const { return AsString(); }

Json Json::Parse(std::string_view text) { return Parser(text).ParseDocument(); }
Json Json::ParseBytes(const std::span<const std::byte> bytes)
{
    return Parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

namespace
{
    void DumpEscapedString(const std::string& value, std::string& out)
    {
        constexpr char HexDigits[] = "0123456789abcdef";
        out.push_back('"');
        for (const char character : value)
        {
            const auto raw = static_cast<unsigned char>(character);
            switch (raw)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (raw < 0x20)
                {
                    out += "\\u00";
                    out.push_back(HexDigits[raw >> 4]);
                    out.push_back(HexDigits[raw & 0x0f]);
                }
                else
                {
                    out.push_back(character);
                }
                break;
            }
        }
        out.push_back('"');
    }

    void DumpValue(const Json& value, std::string& out)
    {
        if (value.IsNull())
        {
            out += "null";
            return;
        }
        if (value.IsBoolean())
        {
            out += value.AsBoolean() ? "true" : "false";
            return;
        }
        if (value.IsNumber())
        {
            // 최단 왕복 표기이다. 파싱된 숫자는 유한하지만, 만에 하나 아닌 값은 유효한 JSON이
            // 없으므로 0으로 쓴다.
            const double number = value.AsNumber();
            if (!std::isfinite(number))
            {
                out += "0";
                return;
            }
            char buffer[32] = {};
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number);
            out.append(buffer, result.ptr);
            return;
        }
        if (value.IsString())
        {
            DumpEscapedString(value.AsString(), out);
            return;
        }
        if (value.IsArray())
        {
            out.push_back('[');
            bool first = true;
            for (const Json& element : value.AsArray())
            {
                if (!first)
                {
                    out += ", ";
                }
                first = false;
                DumpValue(element, out);
            }
            out.push_back(']');
            return;
        }

        const Json::Object& object = value.AsObject();
        std::vector<const std::pair<const std::string, Json>*> members;
        members.reserve(object.size());
        for (const auto& member : object)
        {
            members.push_back(&member);
        }
        std::ranges::sort(members, {}, [](const auto* member) -> const std::string&
        {
            return member->first;
        });

        out.push_back('{');
        bool first = true;
        for (const auto* member : members)
        {
            if (!first)
            {
                out += ", ";
            }
            first = false;
            DumpEscapedString(member->first, out);
            out += ": ";
            DumpValue(member->second, out);
        }
        out.push_back('}');
    }
}

std::string Json::Dump() const
{
    std::string out;
    DumpValue(*this, out);
    return out;
}
}
