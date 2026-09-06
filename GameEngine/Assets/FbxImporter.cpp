#include "pch.h"
#include "FbxImporter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// Node transforms are composed with engine Math, so the importer depends on no graphics API.
#include "../Math/Matrix.h"
#include "../Math/Quaternion.h"
#include "../Core/VertexLayout.h"

namespace GameEngine::Assets
{

namespace
{
    constexpr std::size_t MaximumNodeDepth = 128;
    constexpr std::size_t MaximumNodeCount = 1'000'000;
    constexpr std::size_t MaximumPropertyCount = 1'000'000;
    constexpr std::size_t MaximumDecodedArrayBytes = 512ull * 1024ull * 1024ull;

    class FbxError final : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    struct ArrayValue
    {
        char type = 0;
        std::size_t count = 0;
        std::vector<std::byte> bytes;
    };

    using Property = std::variant<std::int64_t, double, std::string, ArrayValue>;

    struct Node
    {
        std::string name;
        std::vector<Property> properties;
        std::vector<Node> children;
    };

    template<typename T>
    T ReadLittle(std::span<const std::byte> data, std::size_t& position)
    {
        if (position + sizeof(T) > data.size()) throw FbxError("unexpected end of FBX file");
        T value{};
        std::memcpy(&value, data.data() + position, sizeof(T));
        position += sizeof(T);
        return value;
    }

    class BitReader final
    {
    public:
        explicit BitReader(std::span<const std::byte> data) : mData(data) {}

        std::uint32_t Read(unsigned int count)
        {
            while (mBitCount < count)
            {
                if (mPosition >= mData.size()) throw FbxError("truncated DEFLATE stream");
                mBits |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(mData[mPosition++])) << mBitCount;
                mBitCount += 8;
            }
            const std::uint32_t result = static_cast<std::uint32_t>(mBits & ((std::uint64_t{ 1 } << count) - 1));
            mBits >>= count;
            mBitCount -= count;
            return result;
        }

        void Align() { mBits = 0; mBitCount = 0; }
        [[nodiscard]] std::size_t Position() const noexcept { return mPosition; }

    private:
        std::span<const std::byte> mData;
        std::size_t mPosition = 0;
        std::uint64_t mBits = 0;
        unsigned int mBitCount = 0;
    };

    std::uint32_t ReverseBits(std::uint32_t value, unsigned int count)
    {
        std::uint32_t result = 0;
        for (unsigned int index = 0; index < count; ++index)
        {
            result = (result << 1) | (value & 1);
            value >>= 1;
        }
        return result;
    }

    class Huffman final
    {
    public:
        explicit Huffman(const std::vector<unsigned int>& lengths)
        {
            std::array<unsigned int, 16> counts{};
            for (const unsigned int length : lengths)
            {
                if (length > 15) throw FbxError("invalid DEFLATE Huffman code length");
                if (length != 0) ++counts[length];
            }
            std::array<unsigned int, 16> next{};
            unsigned int code = 0;
            for (unsigned int bits = 1; bits <= 15; ++bits)
            {
                code = (code + counts[bits - 1]) << 1;
                next[bits] = code;
            }
            for (unsigned int symbol = 0; symbol < lengths.size(); ++symbol)
            {
                const unsigned int length = lengths[symbol];
                if (length != 0) mEntries.push_back({ ReverseBits(next[length]++, length), length, symbol });
            }
        }

        unsigned int Decode(BitReader& reader) const
        {
            std::uint32_t code = 0;
            for (unsigned int length = 1; length <= 15; ++length)
            {
                code |= reader.Read(1) << (length - 1);
                for (const Entry& entry : mEntries)
                    if (entry.length == length && entry.code == code) return entry.symbol;
            }
            throw FbxError("invalid DEFLATE Huffman code");
        }

    private:
        struct Entry { std::uint32_t code; unsigned int length; unsigned int symbol; };
        std::vector<Entry> mEntries;
    };

    std::vector<std::byte> Inflate(std::span<const std::byte> input, std::size_t expectedSize)
    {
        if (input.size() < 6 || expectedSize > MaximumDecodedArrayBytes)
            throw FbxError("invalid or excessively large zlib stream");
        const unsigned int cmf = std::to_integer<unsigned char>(input[0]);
        const unsigned int flg = std::to_integer<unsigned char>(input[1]);
        if ((cmf & 15) != 8 || ((cmf << 8) + flg) % 31 != 0 || (flg & 32) != 0)
            throw FbxError("unsupported zlib header in FBX array");

        BitReader reader(input.subspan(2, input.size() - 6));
        std::vector<std::byte> output;
        output.reserve(expectedSize);
        bool finalBlock = false;
        static constexpr std::array<unsigned int, 29> LengthBase = {
            3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        static constexpr std::array<unsigned int, 29> LengthExtra = {
            0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        static constexpr std::array<unsigned int, 30> DistanceBase = {
            1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        static constexpr std::array<unsigned int, 30> DistanceExtra = {
            0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

        while (!finalBlock)
        {
            finalBlock = reader.Read(1) != 0;
            const unsigned int blockType = reader.Read(2);
            if (blockType == 0)
            {
                reader.Align();
                const unsigned int length = reader.Read(16);
                if ((length ^ 0xffffu) != reader.Read(16)) throw FbxError("invalid uncompressed DEFLATE block");
                if (length > expectedSize - output.size())
                    throw FbxError("FBX array expands beyond its declared size");
                for (unsigned int index = 0; index < length; ++index)
                    output.push_back(static_cast<std::byte>(reader.Read(8)));
                continue;
            }
            if (blockType == 3) throw FbxError("reserved DEFLATE block type");

            std::vector<unsigned int> literalLengths;
            std::vector<unsigned int> distanceLengths;
            if (blockType == 1)
            {
                literalLengths.resize(288, 8);
                std::fill(literalLengths.begin() + 144, literalLengths.begin() + 256, 9);
                std::fill(literalLengths.begin() + 256, literalLengths.begin() + 280, 7);
                distanceLengths.resize(32, 5);
            }
            else
            {
                const unsigned int literalCount = reader.Read(5) + 257;
                const unsigned int distanceCount = reader.Read(5) + 1;
                const unsigned int codeCount = reader.Read(4) + 4;
                static constexpr std::array<unsigned int, 19> Order = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                std::vector<unsigned int> codeLengths(19);
                for (unsigned int index = 0; index < codeCount; ++index) codeLengths[Order[index]] = reader.Read(3);
                const Huffman codeTree(codeLengths);
                std::vector<unsigned int> lengths;
                lengths.reserve(literalCount + distanceCount);
                while (lengths.size() < literalCount + distanceCount)
                {
                    const unsigned int symbol = codeTree.Decode(reader);
                    if (symbol <= 15) lengths.push_back(symbol);
                    else if (symbol == 16)
                    {
                        if (lengths.empty()) throw FbxError("invalid repeated DEFLATE code length");
                        const unsigned int count = reader.Read(2) + 3;
                        lengths.insert(lengths.end(), count, lengths.back());
                    }
                    else if (symbol == 17) lengths.insert(lengths.end(), reader.Read(3) + 3, 0);
                    else if (symbol == 18) lengths.insert(lengths.end(), reader.Read(7) + 11, 0);
                    else throw FbxError("invalid DEFLATE code length symbol");
                    if (lengths.size() > literalCount + distanceCount) throw FbxError("too many DEFLATE code lengths");
                }
                literalLengths.assign(lengths.begin(), lengths.begin() + literalCount);
                distanceLengths.assign(lengths.begin() + literalCount, lengths.end());
            }

            const Huffman literalTree(literalLengths);
            const Huffman distanceTree(distanceLengths);
            while (true)
            {
                const unsigned int symbol = literalTree.Decode(reader);
                if (symbol < 256)
                {
                    if (output.size() >= expectedSize)
                        throw FbxError("FBX array expands beyond its declared size");
                    output.push_back(static_cast<std::byte>(symbol));
                    continue;
                }
                if (symbol == 256) break;
                if (symbol < 257 || symbol > 285) throw FbxError("invalid DEFLATE length symbol");
                const unsigned int lengthIndex = symbol - 257;
                const unsigned int length = LengthBase[lengthIndex] + reader.Read(LengthExtra[lengthIndex]);
                const unsigned int distanceSymbol = distanceTree.Decode(reader);
                if (distanceSymbol >= 30) throw FbxError("invalid DEFLATE distance symbol");
                const unsigned int distance = DistanceBase[distanceSymbol] + reader.Read(DistanceExtra[distanceSymbol]);
                if (distance == 0 || distance > output.size()) throw FbxError("invalid DEFLATE back-reference");
                if (length > expectedSize - output.size())
                    throw FbxError("FBX array expands beyond its declared size");
                for (unsigned int index = 0; index < length; ++index)
                    output.push_back(output[output.size() - distance]);
            }
        }
        if (output.size() != expectedSize) throw FbxError("FBX array size does not match its declaration");
        std::uint32_t a = 1;
        std::uint32_t b = 0;
        for (const std::byte value : output)
        {
            a = (a + std::to_integer<unsigned char>(value)) % 65521;
            b = (b + a) % 65521;
        }
        const std::size_t checksum = input.size() - 4;
        const std::uint32_t expectedChecksum =
            (std::to_integer<std::uint32_t>(input[checksum]) << 24) |
            (std::to_integer<std::uint32_t>(input[checksum + 1]) << 16) |
            (std::to_integer<std::uint32_t>(input[checksum + 2]) << 8) |
            std::to_integer<std::uint32_t>(input[checksum + 3]);
        if ((b << 16 | a) != expectedChecksum) throw FbxError("FBX array zlib checksum mismatch");
        return output;
    }

    class BinaryParser final
    {
    public:
        explicit BinaryParser(std::span<const std::byte> data) : mData(data)
        {
            constexpr std::string_view Signature("Kaydara FBX Binary  \0\x1a\0", 23);
            if (data.size() < 27 || std::memcmp(data.data(), Signature.data(), Signature.size()) != 0)
                throw FbxError("only binary FBX files are supported");
            mPosition = 23;
            mVersion = ReadLittle<std::uint32_t>(mData, mPosition);
            mWideRecords = mVersion >= 7500;
        }

        std::vector<Node> Parse()
        {
            std::vector<Node> nodes;
            while (mPosition < mData.size())
            {
                std::optional<Node> node = ParseNode(0, mData.size());
                if (!node) break;
                nodes.push_back(std::move(*node));
            }
            return nodes;
        }

    private:
        std::optional<Node> ParseNode(const std::size_t depth, const std::size_t parentEnd)
        {
            if (depth >= MaximumNodeDepth) throw FbxError("FBX node nesting is too deep");
            const std::size_t recordStart = mPosition;
            const std::uint64_t endOffset = mWideRecords
                ? ReadLittle<std::uint64_t>(mData, mPosition)
                : ReadLittle<std::uint32_t>(mData, mPosition);
            const std::uint64_t propertyCount = mWideRecords
                ? ReadLittle<std::uint64_t>(mData, mPosition)
                : ReadLittle<std::uint32_t>(mData, mPosition);
            if (mWideRecords) (void)ReadLittle<std::uint64_t>(mData, mPosition);
            else (void)ReadLittle<std::uint32_t>(mData, mPosition);
            const unsigned int nameLength = ReadLittle<std::uint8_t>(mData, mPosition);
            if (endOffset == 0 && propertyCount == 0 && nameLength == 0) return std::nullopt;
            if (endOffset <= recordStart || endOffset > parentEnd || mPosition + nameLength > endOffset)
                throw FbxError("invalid FBX node bounds");
            if (++mNodeCount > MaximumNodeCount) throw FbxError("FBX has too many nodes");
            Node node;
            node.name.assign(reinterpret_cast<const char*>(mData.data() + mPosition), nameLength);
            mPosition += nameLength;
            // Every property consumes at least a type byte and one value byte. Reject fabricated
            // counts before reserve, and bound total allocations across all nodes in this file.
            if (propertyCount > (endOffset - mPosition) / 2 ||
                propertyCount > MaximumPropertyCount - mPropertyCount)
                throw FbxError("invalid or excessive FBX property count");
            mPropertyCount += static_cast<std::size_t>(propertyCount);
            node.properties.reserve(static_cast<std::size_t>(propertyCount));
            for (std::uint64_t index = 0; index < propertyCount; ++index)
            {
                node.properties.push_back(ParseProperty(static_cast<std::size_t>(endOffset)));
                if (mPosition > endOffset) throw FbxError("FBX property exceeds its node bounds");
            }
            const std::size_t nullSize = mWideRecords ? 25 : 13;
            while (mPosition + nullSize < endOffset)
            {
                std::optional<Node> child = ParseNode(depth + 1, static_cast<std::size_t>(endOffset));
                if (!child) break;
                node.children.push_back(std::move(*child));
            }
            mPosition = static_cast<std::size_t>(endOffset);
            return node;
        }

        Property ParseProperty(const std::size_t nodeEnd)
        {
            const char type = static_cast<char>(ReadLittle<std::uint8_t>(mData, mPosition));
            switch (type)
            {
            case 'Y': return static_cast<std::int64_t>(ReadLittle<std::int16_t>(mData, mPosition));
            case 'C': return static_cast<std::int64_t>(ReadLittle<std::uint8_t>(mData, mPosition));
            case 'I': return static_cast<std::int64_t>(ReadLittle<std::int32_t>(mData, mPosition));
            case 'L': return ReadLittle<std::int64_t>(mData, mPosition);
            case 'F': return static_cast<double>(ReadLittle<float>(mData, mPosition));
            case 'D': return ReadLittle<double>(mData, mPosition);
            case 'S': case 'R':
            {
                const std::uint32_t size = ReadLittle<std::uint32_t>(mData, mPosition);
                if (mPosition + size > nodeEnd) throw FbxError("invalid FBX property size");
                std::string value(reinterpret_cast<const char*>(mData.data() + mPosition), size);
                mPosition += size;
                return value;
            }
            case 'f': case 'd': case 'i': case 'l': case 'b':
            {
                const std::uint32_t count = ReadLittle<std::uint32_t>(mData, mPosition);
                const std::uint32_t encoding = ReadLittle<std::uint32_t>(mData, mPosition);
                const std::uint32_t compressedSize = ReadLittle<std::uint32_t>(mData, mPosition);
                const std::size_t elementSize = type == 'd' || type == 'l' ? 8 : type == 'b' ? 1 : 4;
                if (count > (std::numeric_limits<std::size_t>::max)() / elementSize || mPosition + compressedSize > nodeEnd)
                    throw FbxError("invalid FBX array size");
                const std::size_t decodedSize = count * elementSize;
                if (decodedSize > MaximumDecodedArrayBytes - mDecodedArrayBytes)
                    throw FbxError("FBX decoded arrays exceed the import memory limit");
                mDecodedArrayBytes += decodedSize;
                ArrayValue array{ type, count, {} };
                const std::span<const std::byte> source = mData.subspan(mPosition, compressedSize);
                if (encoding == 0)
                {
                    if (compressedSize != count * elementSize) throw FbxError("invalid uncompressed FBX array size");
                    array.bytes.assign(source.begin(), source.end());
                }
                else if (encoding == 1) array.bytes = Inflate(source, count * elementSize);
                else throw FbxError("unsupported FBX array encoding");
                mPosition += compressedSize;
                return array;
            }
            default: throw FbxError("unsupported FBX property type");
            }
        }

        std::span<const std::byte> mData;
        std::size_t mPosition = 0;
        std::uint32_t mVersion = 0;
        bool mWideRecords = false;
        std::size_t mNodeCount = 0;
        std::size_t mPropertyCount = 0;
        std::size_t mDecodedArrayBytes = 0;
    };

    const Node* FindChild(const Node& node, std::string_view name)
    {
        const auto iterator = std::ranges::find(node.children, name, &Node::name);
        return iterator == node.children.end() ? nullptr : &*iterator;
    }

    const Node* FindRoot(const std::vector<Node>& nodes, std::string_view name)
    {
        const auto iterator = std::ranges::find(nodes, name, &Node::name);
        return iterator == nodes.end() ? nullptr : &*iterator;
    }

    std::string PropertyString(const Node* node, std::size_t index = 0)
    {
        if (!node || index >= node->properties.size()) return {};
        const std::string* value = std::get_if<std::string>(&node->properties[index]);
        return value ? *value : std::string{};
    }

    /// <summary>
    /// FBX 객체의 이름이다. 객체 레코드의 속성 1이 이름과 클래스를 "\0\x01" 구분자로 이어
    /// 담으므로, 그 구분자부터는 잘라 낸다. 남는 것이 모델링 도구가 아웃라이너에 보여주는
    /// 이름이다.
    /// </summary>
    std::string ObjectName(const Node* node)
    {
        std::string name = PropertyString(node, 1);
        if (const std::size_t separator = name.find('\0'); separator != std::string::npos)
        {
            name.resize(separator);
        }
        return name;
    }

    std::optional<std::int64_t> PropertyInteger(const Node* node, std::size_t index = 0)
    {
        if (!node || index >= node->properties.size()) return std::nullopt;
        if (const std::int64_t* value = std::get_if<std::int64_t>(&node->properties[index])) return *value;
        return std::nullopt;
    }

    std::optional<double> PropertyNumber(const Node* node, std::size_t index)
    {
        if (!node || index >= node->properties.size()) return std::nullopt;
        if (const double* value = std::get_if<double>(&node->properties[index])) return *value;
        if (const std::int64_t* value = std::get_if<std::int64_t>(&node->properties[index]))
            return static_cast<double>(*value);
        return std::nullopt;
    }

    const Node* FindProperty70(const Node& owner, std::string_view name)
    {
        const Node* properties = FindChild(owner, "Properties70");
        if (!properties) return nullptr;
        const auto iterator = std::ranges::find_if(properties->children, [name](const Node& property)
        {
            return property.name == "P" && PropertyString(&property) == name;
        });
        return iterator == properties->children.end() ? nullptr : &*iterator;
    }

    Math::Vector3 ReadVectorProperty(
        const Node& owner,
        std::string_view name,
        const Math::Vector3& fallback)
    {
        const Node* property = FindProperty70(owner, name);
        if (!property || property->properties.size() < 7) return fallback;
        const std::optional<double> x = PropertyNumber(property, property->properties.size() - 3);
        const std::optional<double> y = PropertyNumber(property, property->properties.size() - 2);
        const std::optional<double> z = PropertyNumber(property, property->properties.size() - 1);
        if (!x || !y || !z) return fallback;
        return { static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z) };
    }

    int ReadIntegerProperty(const Node& owner, std::string_view name, int fallback)
    {
        const Node* property = FindProperty70(owner, name);
        if (!property || property->properties.size() < 5) return fallback;
        const std::optional<double> value = PropertyNumber(property, property->properties.size() - 1);
        return value ? static_cast<int>(*value) : fallback;
    }

    Math::Matrix4x4 Translation(const Math::Vector3& value)
    {
        return Math::Matrix4x4::CreateTranslation(value);
    }

    Math::Matrix4x4 InverseTranslation(const Math::Vector3& value)
    {
        return Math::Matrix4x4::CreateTranslation({ -value.GetX(), -value.GetY(), -value.GetZ() });
    }

    Math::Matrix4x4 EulerRotation(const Math::Vector3& degrees, int order)
    {
        const Math::Matrix4x4 x = Math::Matrix4x4::CreateRotationXDegrees(degrees.GetX());
        const Math::Matrix4x4 y = Math::Matrix4x4::CreateRotationYDegrees(degrees.GetY());
        const Math::Matrix4x4 z = Math::Matrix4x4::CreateRotationZDegrees(degrees.GetZ());
        switch (order)
        {
        case 0: return x * y * z; // XYZ
        case 1: return x * z * y; // XZY
        case 2: return y * z * x; // YZX
        case 3: return y * x * z; // YXZ
        case 4: return z * x * y; // ZXY
        case 5: return z * y * x; // ZYX
        default: return x * y * z;
        }
    }

    struct Model
    {
        const Node* node = nullptr;
        std::optional<std::int64_t> parentId;
        Math::Matrix4x4 local;
        Math::Matrix4x4 geometric;
    };

    /// <summary>
    /// 로컬 변환 공식이 필요로 하지만 애니메이션 곡선이 움직이지 않는 부분이다 — 피벗과 오프셋,
    /// 회전 순서. translation/rotation/scaling만 시각마다 갈리므로, 이것들은 뼈마다 한 번만
    /// 읽어 정적 바인드 포즈와 애니메이션 채점이 같은 값을 공유한다.
    /// </summary>
    struct LocalTransformPivots
    {
        Math::Vector3 preRotation;
        Math::Vector3 postRotation;
        Math::Vector3 rotationOffset;
        Math::Vector3 rotationPivot;
        Math::Vector3 scalingOffset;
        Math::Vector3 scalingPivot;
        int rotationOrder = 0;
    };

    LocalTransformPivots ReadLocalTransformPivots(const Node& model)
    {
        const Math::Vector3 zero{};
        LocalTransformPivots pivots;
        pivots.preRotation = ReadVectorProperty(model, "PreRotation", zero);
        pivots.postRotation = ReadVectorProperty(model, "PostRotation", zero);
        pivots.rotationOffset = ReadVectorProperty(model, "RotationOffset", zero);
        pivots.rotationPivot = ReadVectorProperty(model, "RotationPivot", zero);
        pivots.scalingOffset = ReadVectorProperty(model, "ScalingOffset", zero);
        pivots.scalingPivot = ReadVectorProperty(model, "ScalingPivot", zero);
        pivots.rotationOrder = ReadIntegerProperty(model, "RotationOrder", 0);
        return pivots;
    }

    /// <summary>
    /// FBX 로컬 변환 공식이다. 정적 바인드 포즈는 이것을 노드의 정적 Lcl 속성으로 부르고,
    /// 애니메이션 채점은 같은 공식을 그 시각의 채점된 translation/rotation/scaling으로 부른다 —
    /// 둘이 갈리면 애니메이션 시작 프레임이 바인드 포즈와 어긋나 보이는데, 공식을 하나로 모으면
    /// 그 갈림이 구조적으로 없어진다.
    /// </summary>
    Math::Matrix4x4 ComposeLocalTransform(
        const Math::Vector3& translation, const Math::Vector3& rotation, const Math::Vector3& scaling,
        const LocalTransformPivots& pivots)
    {
        const Math::Matrix4x4 scale = Math::Matrix4x4::CreateScale(scaling);
        const Math::Matrix4x4 pre = EulerRotation(pivots.preRotation, pivots.rotationOrder);
        const Math::Matrix4x4 localRotation = EulerRotation(rotation, pivots.rotationOrder);
        // A rotation is orthonormal, so its transpose is its inverse.
        const Math::Matrix4x4 postInverse = EulerRotation(pivots.postRotation, pivots.rotationOrder).Transpose();

        // FBX pivot formula written for the engine's row-vector convention.
        return InverseTranslation(pivots.scalingPivot) * scale * Translation(pivots.scalingPivot) *
            Translation(pivots.scalingOffset) * InverseTranslation(pivots.rotationPivot) * postInverse *
            localRotation * pre * Translation(pivots.rotationPivot) * Translation(pivots.rotationOffset) *
            Translation(translation);
    }

    Math::Matrix4x4 ReadLocalTransform(const Node& model)
    {
        const Math::Vector3 zero{};
        const Math::Vector3 one{ 1.0f, 1.0f, 1.0f };
        const Math::Vector3 translation = ReadVectorProperty(model, "Lcl Translation", zero);
        const Math::Vector3 rotation = ReadVectorProperty(model, "Lcl Rotation", zero);
        const Math::Vector3 scaling = ReadVectorProperty(model, "Lcl Scaling", one);
        return ComposeLocalTransform(translation, rotation, scaling, ReadLocalTransformPivots(model));
    }

    Math::Matrix4x4 ReadGeometricTransform(const Node& model)
    {
        const Math::Vector3 zero{};
        const Math::Vector3 one{ 1.0f, 1.0f, 1.0f };
        const Math::Vector3 translation = ReadVectorProperty(model, "GeometricTranslation", zero);
        const Math::Vector3 rotation = ReadVectorProperty(model, "GeometricRotation", zero);
        const Math::Vector3 scaling = ReadVectorProperty(model, "GeometricScaling", one);
        return Math::Matrix4x4::CreateScale(scaling) *
            EulerRotation(rotation, 0) * Translation(translation);
    }

    Math::Matrix4x4 ResolveGlobalTransform(
        std::int64_t id,
        const std::unordered_map<std::int64_t, Model>& models,
        std::unordered_map<std::int64_t, Math::Matrix4x4>& cache,
        std::unordered_set<std::int64_t>& resolving)
    {
        if (const auto cached = cache.find(id); cached != cache.end()) return cached->second;
        const auto model = models.find(id);
        if (model == models.end()) return Math::Matrix4x4::Identity();
        if (!resolving.insert(id).second) throw FbxError("circular FBX Model hierarchy");
        Math::Matrix4x4 global = model->second.local;
        if (model->second.parentId && models.contains(*model->second.parentId))
            global = global * ResolveGlobalTransform(*model->second.parentId, models, cache, resolving);
        resolving.erase(id);
        cache.emplace(id, global);
        return global;
    }

    struct AxisConversion
    {
        int rightAxis = 0;
        int upAxis = 1;
        int frontAxis = 2;
        float rightSign = 1.0f;
        float upSign = 1.0f;
        float frontSign = -1.0f;
        float unitScale = 1.0f;

        Math::Vector3 ConvertDirection(const Math::Vector3& value) const
        {
            const float components[3] = { value.GetX(), value.GetY(), value.GetZ() };
            return {
                rightSign * components[rightAxis],
                upSign * components[upAxis],
                frontSign * components[frontAxis]
            };
        }

        Math::Vector3 ConvertPosition(const Math::Vector3& value) const
        {
            const Math::Vector3 converted = ConvertDirection(value);
            return {
                converted.GetX() * unitScale,
                converted.GetY() * unitScale,
                converted.GetZ() * unitScale
            };
        }

        [[nodiscard]] float DeterminantSign() const
        {
            const int permutation[3] = { rightAxis, upAxis, frontAxis };
            int inversions = 0;
            for (int a = 0; a < 3; ++a)
                for (int b = a + 1; b < 3; ++b)
                    if (permutation[a] > permutation[b]) ++inversions;
            return (inversions % 2 == 0 ? 1.0f : -1.0f) * rightSign * upSign * frontSign;
        }
    };

    AxisConversion ReadAxisConversion(const std::vector<Node>& nodes)
    {
        AxisConversion conversion;
        const Node* settings = FindRoot(nodes, "GlobalSettings");
        if (!settings) return conversion;
        conversion.upAxis = ReadIntegerProperty(*settings, "UpAxis", 1);
        conversion.upSign = static_cast<float>(ReadIntegerProperty(*settings, "UpAxisSign", 1));
        conversion.frontAxis = ReadIntegerProperty(*settings, "FrontAxis", 2);
        conversion.frontSign = static_cast<float>(ReadIntegerProperty(*settings, "FrontAxisSign", -1));
        conversion.rightAxis = ReadIntegerProperty(*settings, "CoordAxis", 0);
        conversion.rightSign = static_cast<float>(ReadIntegerProperty(*settings, "CoordAxisSign", 1));
        if (const Node* unit = FindProperty70(*settings, "UnitScaleFactor"))
        {
            const std::optional<double> centimeters = PropertyNumber(unit, unit->properties.size() - 1);
            if (!centimeters || !std::isfinite(*centimeters) || *centimeters <= 0.0)
                throw FbxError("invalid FBX system unit");
            // The importer exposes centimeters as engine-space units.
            conversion.unitScale = static_cast<float>(*centimeters);
        }
        const auto validAxis = [](int value) { return value >= 0 && value <= 2; };
        if (!validAxis(conversion.rightAxis) || !validAxis(conversion.upAxis) ||
            !validAxis(conversion.frontAxis) || conversion.rightAxis == conversion.upAxis ||
            conversion.rightAxis == conversion.frontAxis || conversion.upAxis == conversion.frontAxis)
            throw FbxError("invalid FBX axis system");
        const auto validSign = [](float value) { return value == -1.0f || value == 1.0f; };
        if (!validSign(conversion.rightSign) || !validSign(conversion.upSign) ||
            !validSign(conversion.frontSign) || !std::isfinite(conversion.unitScale))
            throw FbxError("invalid FBX axis sign or system unit");
        // FBX files may use either handedness. Normalize the semantic right/up/front basis
        // to the left-handed coordinate system expected by the D3D renderer.
        if (conversion.DeterminantSign() > 0.0f)
            conversion.frontSign = -conversion.frontSign;
        return conversion;
    }

    template<typename T>
    std::vector<T> ReadArray(const Node* node)
    {
        if (!node || node->properties.empty()) return {};
        const ArrayValue* array = std::get_if<ArrayValue>(&node->properties[0]);
        if (!array || array->bytes.size() != array->count * sizeof(T)) return {};
        std::vector<T> result(array->count);
        std::memcpy(result.data(), array->bytes.data(), array->bytes.size());
        return result;
    }

    std::vector<double> ReadNumericArray(const Node* node)
    {
        if (!node || node->properties.empty()) return {};
        const ArrayValue* array = std::get_if<ArrayValue>(&node->properties[0]);
        if (!array) return {};
        std::vector<double> result(array->count);
        if (array->type == 'd')
        {
            std::memcpy(result.data(), array->bytes.data(), array->bytes.size());
        }
        else if (array->type == 'f')
        {
            for (std::size_t index = 0; index < result.size(); ++index)
            {
                float value = 0.0f;
                std::memcpy(&value, array->bytes.data() + index * sizeof(float), sizeof(float));
                result[index] = value;
            }
        }
        else return {};
        return result;
    }

    struct Layer
    {
        std::string mapping;
        std::string reference;
        std::vector<double> values;
        std::vector<std::int32_t> indices;
        std::size_t tupleSize = 0;
    };

    Layer ReadLayer(const Node* geometry, std::string_view layerName, std::string_view valuesName, std::size_t tupleSize)
    {
        Layer layer;
        const Node* node = FindChild(*geometry, layerName);
        if (!node) return layer;
        layer.mapping = PropertyString(FindChild(*node, "MappingInformationType"));
        layer.reference = PropertyString(FindChild(*node, "ReferenceInformationType"));
        layer.values = ReadNumericArray(FindChild(*node, valuesName));
        layer.indices = ReadArray<std::int32_t>(FindChild(*node, std::string(valuesName) + "Index"));
        layer.tupleSize = tupleSize;
        return layer;
    }

    bool ReadLayerValue(const Layer& layer, std::size_t controlPoint, std::size_t polygonVertex,
        std::size_t polygon, double* output)
    {
        if (layer.values.empty()) return false;
        std::size_t mapped = 0;
        if (layer.mapping == "ByControlPoint") mapped = controlPoint;
        else if (layer.mapping == "ByPolygonVertex") mapped = polygonVertex;
        else if (layer.mapping == "ByPolygon") mapped = polygon;
        else if (layer.mapping != "AllSame") return false;
        std::size_t direct = mapped;
        if (layer.reference == "IndexToDirect" || layer.reference == "Index")
        {
            if (mapped >= layer.indices.size() || layer.indices[mapped] < 0) return false;
            direct = static_cast<std::size_t>(layer.indices[mapped]);
        }
        if (layer.tupleSize == 0 || direct >= layer.values.size() / layer.tupleSize) return false;
        for (std::size_t index = 0; index < layer.tupleSize; ++index) output[index] = layer.values[direct * layer.tupleSize + index];
        return true;
    }

    ImportedMeshVertex MakeVertex(
        const std::vector<double>& positions,
        std::int32_t controlPoint,
        std::size_t polygonVertex,
        std::size_t polygon,
        const Layer& normals,
        const Layer& uvs,
        const Math::Matrix4x4& transform,
        const Math::Matrix4x4& normalTransform,
        const AxisConversion& axisConversion)
    {
        if (controlPoint < 0 || static_cast<std::size_t>(controlPoint) * 3 + 2 >= positions.size())
            throw FbxError("FBX polygon references an invalid control point");
        ImportedMeshVertex vertex{};
        const std::size_t positionIndex = static_cast<std::size_t>(controlPoint) * 3;
        const Math::Vector3 sourcePosition{
            static_cast<float>(positions[positionIndex]),
            static_cast<float>(positions[positionIndex + 1]),
            static_cast<float>(positions[positionIndex + 2]) };
        const Math::Vector3 transformedPosition =
            axisConversion.ConvertPosition(transform.TransformPoint(sourcePosition));
        vertex.position[0] = transformedPosition.GetX();
        vertex.position[1] = transformedPosition.GetY();
        vertex.position[2] = transformedPosition.GetZ();
        double normal[3]{};
        if (ReadLayerValue(normals, controlPoint, polygonVertex, polygon, normal))
        {
            const Math::Vector3 sourceNormal{
                static_cast<float>(normal[0]),
                static_cast<float>(normal[1]),
                static_cast<float>(normal[2]) };
            // A normal is transformed by the inverse transpose, and the axis conversion can mirror,
            // so normalize after both steps.
            const Math::Vector3 transformedNormal = axisConversion.ConvertDirection(
                normalTransform.TransformDirection(sourceNormal).Normalized()).Normalized();
            vertex.normal[0] = transformedNormal.GetX();
            vertex.normal[1] = transformedNormal.GetY();
            vertex.normal[2] = transformedNormal.GetZ();
        }
        double uv[2]{};
        if (ReadLayerValue(uvs, controlPoint, polygonVertex, polygon, uv))
        {
            vertex.textureCoordinate[0] = static_cast<float>(uv[0]);
            vertex.textureCoordinate[1] = static_cast<float>(1.0 - uv[1]);
        }
        return vertex;
    }

    void GenerateTriangleNormal(ImportedMeshVertex& a, ImportedMeshVertex& b, ImportedMeshVertex& c)
    {
        const float ab[3] = { b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2] };
        const float ac[3] = { c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2] };
        float normal[3] = {
            ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0] };
        const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 0.0f) for (float& value : normal) value /= length;
        std::copy(std::begin(normal), std::end(normal), a.normal);
        std::copy(std::begin(normal), std::end(normal), b.normal);
        std::copy(std::begin(normal), std::end(normal), c.normal);
    }

    /// <summary>
    /// <c>Load</c>와 <c>LoadSkeleton</c>이 함께 필요로 하는 노드 트리, geometry와 model의
    /// 표, 인스턴스 연결, 축 변환을 한 번만 만든다. 메시와 스켈레톤 읽기가 같은 준비 결과를
    /// 사용해야 파일의 구조와 변환을 일관되게 해석한다.
    /// </summary>
    struct ParsedScene
    {
        std::vector<Node> nodes;
        std::unordered_map<std::int64_t, const Node*> geometries;
        std::unordered_map<std::int64_t, Model> models;
        std::unordered_map<std::int64_t, std::vector<std::int64_t>> geometryInstances;
        AxisConversion axisConversion;
    };

    ParsedScene ParseSceneGraph(const std::span<const std::byte> fileBytes)
    {
        if (fileBytes.empty()) throw FbxError("the FBX file is empty");
        ParsedScene scene;
        scene.nodes = BinaryParser(fileBytes).Parse();
        const Node* objects = FindRoot(scene.nodes, "Objects");
        if (!objects) throw FbxError("FBX file has no Objects section");

        for (const Node& object : objects->children)
        {
            const std::optional<std::int64_t> id = PropertyInteger(&object);
            if (!id) continue;
            if (object.name == "Geometry" && PropertyString(&object, 2) == "Mesh")
            {
                if (!scene.geometries.emplace(*id, &object).second)
                    throw FbxError("duplicate FBX Geometry id");
            }
            else if (object.name == "Model")
            {
                Model model;
                model.node = &object;
                model.local = ReadLocalTransform(object);
                model.geometric = ReadGeometricTransform(object);
                if (!scene.models.emplace(*id, model).second)
                    throw FbxError("duplicate FBX Model id");
            }
        }

        if (const Node* connections = FindRoot(scene.nodes, "Connections"))
        {
            for (const Node& connection : connections->children)
            {
                if (connection.name != "C" || PropertyString(&connection) != "OO") continue;
                const std::optional<std::int64_t> child = PropertyInteger(&connection, 1);
                const std::optional<std::int64_t> parent = PropertyInteger(&connection, 2);
                if (!child || !parent) continue;
                if (scene.geometries.contains(*child) && scene.models.contains(*parent))
                    scene.geometryInstances[*child].push_back(*parent);
                else if (scene.models.contains(*child) && scene.models.contains(*parent))
                    scene.models.at(*child).parentId = *parent;
            }
        }

        scene.axisConversion = ReadAxisConversion(scene.nodes);
        return scene;
    }

    void ImportGeometry(
        const Node& geometry,
        const Math::Matrix4x4& transform,
        const AxisConversion& axisConversion,
        ImportedMesh& mesh)
    {
        const std::vector<double> positions = ReadNumericArray(FindChild(geometry, "Vertices"));
        const std::vector<std::int32_t> polygonIndices = ReadArray<std::int32_t>(FindChild(geometry, "PolygonVertexIndex"));
        if (positions.empty() || positions.size() % 3 != 0 || polygonIndices.empty()) return;
        const Layer normals = ReadLayer(&geometry, "LayerElementNormal", "Normals", 3);
        const Layer uvs = ReadLayer(&geometry, "LayerElementUV", "UV", 2);
        const float transformDeterminant = transform.GetDeterminant();
        Math::Matrix4x4 inverseTransform;
        if (std::abs(transformDeterminant) <= 1.0e-8f || !transform.TryInvert(inverseTransform))
            throw FbxError("FBX Model has a singular transform");
        const Math::Matrix4x4 normalTransform = inverseTransform.Transpose();
        const bool flipWinding = transformDeterminant * axisConversion.DeterminantSign() < 0.0f;
        struct Corner { std::int32_t controlPoint; std::size_t polygonVertex; };
        std::vector<Corner> corners;
        std::size_t polygon = 0;
        for (std::size_t polygonVertex = 0; polygonVertex < polygonIndices.size(); ++polygonVertex)
        {
            const std::int32_t encoded = polygonIndices[polygonVertex];
            corners.push_back({ encoded < 0 ? -encoded - 1 : encoded, polygonVertex });
            if (encoded >= 0) continue;
            for (std::size_t index = 1; index + 1 < corners.size(); ++index)
            {
                const Corner& second = flipWinding ? corners[index + 1] : corners[index];
                const Corner& third = flipWinding ? corners[index] : corners[index + 1];
                ImportedMeshVertex a = MakeVertex(positions, corners[0].controlPoint,
                    corners[0].polygonVertex, polygon, normals, uvs, transform,
                    normalTransform, axisConversion);
                ImportedMeshVertex b = MakeVertex(positions, second.controlPoint,
                    second.polygonVertex, polygon, normals, uvs, transform,
                    normalTransform, axisConversion);
                ImportedMeshVertex c = MakeVertex(positions, third.controlPoint,
                    third.polygonVertex, polygon, normals, uvs, transform,
                    normalTransform, axisConversion);
                if (normals.values.empty()) GenerateTriangleNormal(a, b, c);
                if (mesh.vertices.size() > (std::numeric_limits<unsigned int>::max)() - 3)
                    throw FbxError("FBX mesh has too many vertices");
                const unsigned int base = static_cast<unsigned int>(mesh.vertices.size());
                mesh.vertices.push_back(a); mesh.vertices.push_back(b); mesh.vertices.push_back(c);
                mesh.indices.push_back(base); mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
            }
            corners.clear();
            ++polygon;
        }
        if (!corners.empty()) throw FbxError("unterminated FBX polygon");
    }

    /// <summary>SubDeformer(Cluster)다. 뼈 하나가 통제점 몇 개를 어느 무게로 당기는지다.</summary>
    struct Cluster
    {
        const Node* node = nullptr;
        /// <summary>이 클러스터가 당기는 뼈다 — Cluster -OO-> Model 연결에서 얻는다.</summary>
        std::optional<std::int64_t> targetModelId;
    };

    /// <summary>Deformer(Skin)다. 한 geometry에 매인 클러스터들의 모음이다.</summary>
    struct Skin
    {
        std::int64_t geometryId = 0;
        std::vector<std::int64_t> clusterIds;
    };

    struct ParsedSkinning
    {
        std::unordered_map<std::int64_t, Skin> skins;
        std::unordered_map<std::int64_t, Cluster> clusters;
        /// <summary><c>Skin::geometryId</c>의 역방향이다 — geometry 하나가 매인 skin을 바로 찾는다.</summary>
        std::unordered_map<std::int64_t, std::int64_t> skinByGeometry;
    };

    /// <summary>
    /// Objects의 Deformer(Skin)·SubDeformer(Cluster)와 그들을 잇는 Connections를 읽는다. 파일에
    /// 스킨이 하나도 없으면 빈 채로 돌아온다 — 그것은 오류가 아니라 정적 파일이라는 뜻이고, 그
    /// 판단은 부르는 쪽(LoadSkeleton)의 몫이다.
    /// </summary>
    ParsedSkinning ParseSkinning(const ParsedScene& scene)
    {
        ParsedSkinning skinning;
        const Node* objects = FindRoot(scene.nodes, "Objects");
        if (!objects) return skinning;

        for (const Node& object : objects->children)
        {
            const std::optional<std::int64_t> id = PropertyInteger(&object);
            if (!id) continue;
            // Binary FBX names both Skin and Cluster deformer objects "Deformer" -- "SubDeformer"
            // is only ASCII text embedded in a Cluster's own object name (e.g. "...Cluster
            // SubDeformer"), not the node's element name. Property 2 (the class/subclass string) is
            // what actually distinguishes them.
            if (object.name == "Deformer" && PropertyString(&object, 2) == "Skin")
            {
                skinning.skins.emplace(*id, Skin{});
            }
            else if (object.name == "Deformer" && PropertyString(&object, 2) == "Cluster")
            {
                Cluster cluster;
                cluster.node = &object;
                skinning.clusters.emplace(*id, cluster);
            }
        }

        if (const Node* connections = FindRoot(scene.nodes, "Connections"))
        {
            for (const Node& connection : connections->children)
            {
                if (connection.name != "C" || PropertyString(&connection) != "OO") continue;
                const std::optional<std::int64_t> child = PropertyInteger(&connection, 1);
                const std::optional<std::int64_t> parent = PropertyInteger(&connection, 2);
                if (!child || !parent) continue;
                if (const auto cluster = skinning.clusters.find(*child); cluster != skinning.clusters.end())
                {
                    // Cluster -> Skin: this cluster belongs to that skin.
                    if (const auto skin = skinning.skins.find(*parent); skin != skinning.skins.end())
                        skin->second.clusterIds.push_back(*child);
                }
                else if (const auto skin = skinning.skins.find(*child); skin != skinning.skins.end())
                {
                    // Skin -> Geometry: this skin deforms that geometry.
                    if (scene.geometries.contains(*parent))
                    {
                        skin->second.geometryId = *parent;
                        skinning.skinByGeometry.emplace(*parent, *child);
                    }
                }
                else if (const auto targetOfCluster = skinning.clusters.find(*parent);
                         targetOfCluster != skinning.clusters.end() && scene.models.contains(*child))
                {
                    // Model -> Cluster: unlike the other two connections, the bone is the child and
                    // the cluster that pulls it is the parent.
                    targetOfCluster->second.targetModelId = *child;
                }
            }
        }
        return skinning;
    }

    struct BoneOrder
    {
        /// <summary>부모가 자식보다 앞에 오는 순서다 — 골격 배열이 그대로 이 순서가 된다.</summary>
        std::vector<std::int64_t> orderedModelIds;
        std::unordered_map<std::int64_t, std::uint32_t> indexByModelId;
    };

    /// <summary>
    /// 클러스터가 당기는 뼈들과, 그 각각에서 FBX Model 계층의 진짜 꼭대기까지 올라가는 길에 있는
    /// 모든 조상을 뼈로 삼는다 — 아무것도 당기지 않는 뿌리 관절이 흔하고, 그 자신의 로컬 변환이
    /// 자손들의 바인드 포즈에 여전히 곱해지기 때문이다.
    /// </summary>
    BoneOrder BuildBoneOrder(
        const std::vector<std::int64_t>& clusterBoneModelIds,
        const std::unordered_map<std::int64_t, Model>& models,
        const bool includeAllAncestors = false)
    {
        std::unordered_set<std::int64_t> required;
        for (const std::int64_t boneId : clusterBoneModelIds)
        {
            std::int64_t current = boneId;
            while (required.insert(current).second)
            {
                const auto model = models.find(current);
                if (model == models.end() || !model->second.parentId ||
                    !models.contains(*model->second.parentId))
                    break;
                // Stop at the top of the joint chain. A rig's own root Model (often a "Null"
                // grouping the skeleton with the mesh it skins) is not itself a joint, so it must
                // not become a bone -- only LimbNodes compose the bind-pose hierarchy.
                const Model& parentModel = models.at(*model->second.parentId);
                if (!includeAllAncestors && PropertyString(parentModel.node, 2) != "LimbNode") break;
                current = *model->second.parentId;
            }
        }

        // Repeatedly place the required models whose parent is already placed (or isn't part of the
        // skeleton). Ties broken by id keep the order reproducible run to run.
        BoneOrder order;
        order.orderedModelIds.reserve(required.size());
        std::unordered_set<std::int64_t> placed;
        while (order.orderedModelIds.size() < required.size())
        {
            std::vector<std::int64_t> ready;
            for (const std::int64_t modelId : required)
            {
                if (placed.contains(modelId)) continue;
                const Model& model = models.at(modelId);
                if (!model.parentId || placed.contains(*model.parentId) || !required.contains(*model.parentId))
                    ready.push_back(modelId);
            }
            if (ready.empty()) throw FbxError("circular FBX bone hierarchy");
            std::ranges::sort(ready);
            for (const std::int64_t modelId : ready)
            {
                order.indexByModelId.emplace(modelId, static_cast<std::uint32_t>(order.orderedModelIds.size()));
                order.orderedModelIds.push_back(modelId);
            }
            placed.insert(ready.begin(), ready.end());
        }
        return order;
    }

    /// <summary>
    /// 축 변환을 4x4 행렬로 만든다. <c>ConvertPosition</c>은 선형이고 이동이 없으므로, 표준
    /// 기저 벡터 셋을 그대로 통과시키면 그 결과가 곧 이 변환의 행이다.
    /// </summary>
    Math::Matrix4x4 AxisConversionMatrix(const AxisConversion& axisConversion)
    {
        const Math::Vector3 axisX{ 1.0f, 0.0f, 0.0f };
        const Math::Vector3 axisY{ 0.0f, 1.0f, 0.0f };
        const Math::Vector3 axisZ{ 0.0f, 0.0f, 1.0f };
        return Math::Matrix4x4::FromRows(
            axisConversion.ConvertPosition(axisX), axisConversion.ConvertPosition(axisY),
            axisConversion.ConvertPosition(axisZ), Math::Vector3{});
    }

    /// <summary>
    /// 뼈 계층의 뿌리가 되는 뼈의, FBX 부모 쪽 조상 전역 변환이다 — 그 뼈 자신의 로컬 변환에는
    /// 없는 부분이다. 골격 뿌리의 FBX 부모는 흔히 리그와 메시를 함께 묶는 "Null" 하나이고,
    /// 그것은 뼈가 아니므로 골격에 들지 않지만, 그 자신의 변환은 사라지지 않는다 — 스킨드
    /// 메시의 정점이 굽는 전체 전역 변환에는 그 Null도 들어 있으므로, 뼈와 정점이 같은 공간에
    /// 서 있으려면 뿌리 뼈도 그것을 접어 가져야 한다.
    /// </summary>
    Math::Matrix4x4 AncestorGlobalTransform(
        const Model& model, const std::unordered_map<std::int64_t, Model>& models)
    {
        if (!model.parentId || !models.contains(*model.parentId)) return Math::Matrix4x4::Identity();
        std::unordered_map<std::int64_t, Math::Matrix4x4> cache;
        std::unordered_set<std::int64_t> resolving;
        return ResolveGlobalTransform(*model.parentId, models, cache, resolving);
    }

    /// <summary>
    /// 뼈마다 부모 상대 바인드 포즈를 낸다. 뼈의 FBX 로컬 변환을 축 변환 행렬로 켤레화한다
    /// (M⁻¹·local·M) — 켤레화는 합성에 분배되므로, 각 뼈를 이렇게 따로 변환해 잇는 것이
    /// 전체 전역 변환을 변환한 뒤 나누는 것과 같은 답을 내면서 계산은 훨씬 단순하다.
    /// </summary>
    Animation::Skeleton BuildSkeleton(
        const BoneOrder& order,
        const std::unordered_map<std::int64_t, Model>& models,
        const Math::Matrix4x4& axisMatrix,
        const Math::Matrix4x4& inverseAxisMatrix)
    {
        Animation::Skeleton skeleton;
        skeleton.bones.reserve(order.orderedModelIds.size());
        for (const std::int64_t modelId : order.orderedModelIds)
        {
            const Model& model = models.at(modelId);
            Animation::Bone bone;
            bone.name = ObjectName(model.node);
            const bool hasBoneParent = model.parentId && order.indexByModelId.contains(*model.parentId);
            bone.parentIndex = hasBoneParent ? order.indexByModelId.at(*model.parentId) : Animation::Bone::NoParent;

            // A non-root bone needs no correction: every Model between it and its bone-hierarchy
            // root is itself a bone here, so composing local-to-parent transforms up the chain
            // already reconstructs the right space. A root bone's own FBX parent falls outside the
            // skeleton, so its transform is folded in here instead.
            const Math::Matrix4x4 localFbx =
                hasBoneParent ? model.local : model.local * AncestorGlobalTransform(model, models);
            const Math::Matrix4x4 localConverted = inverseAxisMatrix * localFbx * axisMatrix;
            Math::Vector3 rotationDegreesUnused;
            localConverted.Decompose(bone.bindPoseScale, rotationDegreesUnused, bone.bindPosePosition);
            bone.bindPoseRotation = Math::Quaternion::FromMatrix(localConverted);
            skeleton.bones.push_back(bone);
        }
        return skeleton;
    }

    /// <summary>정점 하나가 받는, 가중치 내림차순 상위 4개까지의 뼈 영향이다.</summary>
    struct BoneInfluence
    {
        std::array<std::uint32_t, 4> boneIndices{};
        Core::Float4 boneWeights;
    };

    /// <summary>
    /// 한 Skin에 속한 모든 클러스터의 (통제점, 무게)를 통제점별로 모아 상위 4개로 자르고 그
    /// 합이 1이 되게 정규화한다. 어느 클러스터도 당기지 않는 통제점은 첫 뼈에 무게 1로 묶는다 —
    /// 원점으로 무너지는 것보다 덜 놀라운, 가중치 없는 정점이 할 수 있는 가장 무난한 일이다.
    /// </summary>
    std::vector<BoneInfluence> BuildBoneInfluences(
        const Skin& skin,
        const ParsedSkinning& skinning,
        const BoneOrder& boneOrder,
        const std::size_t controlPointCount)
    {
        std::vector<std::vector<std::pair<std::uint32_t, double>>> accumulated(controlPointCount);
        for (const std::int64_t clusterId : skin.clusterIds)
        {
            const auto clusterIterator = skinning.clusters.find(clusterId);
            if (clusterIterator == skinning.clusters.end() || !clusterIterator->second.targetModelId) continue;
            const auto boneIndexIterator = boneOrder.indexByModelId.find(*clusterIterator->second.targetModelId);
            if (boneIndexIterator == boneOrder.indexByModelId.end()) continue;
            const std::uint32_t boneIndex = boneIndexIterator->second;

            const std::vector<std::int32_t> indexes =
                ReadArray<std::int32_t>(FindChild(*clusterIterator->second.node, "Indexes"));
            const std::vector<double> weights =
                ReadNumericArray(FindChild(*clusterIterator->second.node, "Weights"));
            const std::size_t count = (std::min)(indexes.size(), weights.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::int32_t controlPoint = indexes[index];
                if (controlPoint < 0 || static_cast<std::size_t>(controlPoint) >= accumulated.size()) continue;
                if (weights[index] > 0.0)
                    accumulated[static_cast<std::size_t>(controlPoint)].push_back({ boneIndex, weights[index] });
            }
        }

        std::vector<BoneInfluence> influences(controlPointCount);
        for (std::size_t point = 0; point < controlPointCount; ++point)
        {
            std::vector<std::pair<std::uint32_t, double>>& raw = accumulated[point];
            std::ranges::sort(raw, [](const auto& a, const auto& b) { return a.second > b.second; });
            if (raw.size() > 4) raw.resize(4);
            double total = 0.0;
            for (const auto& entry : raw) total += entry.second;

            BoneInfluence influence;
            if (total > 0.0)
            {
                for (std::size_t slot = 0; slot < raw.size(); ++slot) influence.boneIndices[slot] = raw[slot].first;
                float weights[4] = {};
                for (std::size_t slot = 0; slot < raw.size(); ++slot)
                    weights[slot] = static_cast<float>(raw[slot].second / total);
                influence.boneWeights = { weights[0], weights[1], weights[2], weights[3] };
            }
            else
            {
                influence.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
            }
            influences[point] = influence;
        }
        return influences;
    }

    void GenerateSkinnedTriangleNormal(
        Core::SkinnedMeshVertex& a, Core::SkinnedMeshVertex& b, Core::SkinnedMeshVertex& c)
    {
        const float ab[3] = { b.position.x - a.position.x, b.position.y - a.position.y, b.position.z - a.position.z };
        const float ac[3] = { c.position.x - a.position.x, c.position.y - a.position.y, c.position.z - a.position.z };
        float normal[3] = {
            ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0] };
        const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 0.0f) for (float& value : normal) value /= length;
        a.normal = { normal[0], normal[1], normal[2] };
        b.normal = { normal[0], normal[1], normal[2] };
        c.normal = { normal[0], normal[1], normal[2] };
    }

    Core::SkinnedMeshVertex MakeSkinnedVertex(
        const std::vector<double>& positions,
        std::int32_t controlPoint,
        std::size_t polygonVertex,
        std::size_t polygon,
        const Layer& normals,
        const Layer& uvs,
        const Math::Matrix4x4& transform,
        const Math::Matrix4x4& normalTransform,
        const AxisConversion& axisConversion,
        const std::vector<BoneInfluence>& influences)
    {
        const ImportedMeshVertex base = MakeVertex(
            positions, controlPoint, polygonVertex, polygon, normals, uvs, transform, normalTransform,
            axisConversion);
        Core::SkinnedMeshVertex vertex{};
        vertex.position = { base.position[0], base.position[1], base.position[2] };
        vertex.normal = { base.normal[0], base.normal[1], base.normal[2] };
        vertex.textureCoordinate = { base.textureCoordinate[0], base.textureCoordinate[1] };
        if (controlPoint >= 0 && static_cast<std::size_t>(controlPoint) < influences.size())
        {
            const BoneInfluence& influence = influences[static_cast<std::size_t>(controlPoint)];
            vertex.boneIndices = influence.boneIndices;
            vertex.boneWeights = influence.boneWeights;
        }
        return vertex;
    }

    /// <summary>
    /// <c>ImportGeometry</c>와 같은 삼각화이지만 <c>SkinnedMeshData</c>를 내놓고 뼈 영향을
    /// 함께 싣는다. 정점을 공유하지 않는 것도 같은 이유다 — 통제점 조회 표는 이미 그것과 무관하게
    /// 따로 있으므로, 공유를 더하는 것은 이 단위의 몫이 아니다.
    /// </summary>
    void ImportSkinnedGeometry(
        const Node& geometry,
        const Math::Matrix4x4& transform,
        const AxisConversion& axisConversion,
        const std::vector<BoneInfluence>& influences,
        SkinnedMeshData& mesh)
    {
        const std::vector<double> positions = ReadNumericArray(FindChild(geometry, "Vertices"));
        const std::vector<std::int32_t> polygonIndices = ReadArray<std::int32_t>(FindChild(geometry, "PolygonVertexIndex"));
        if (positions.empty() || positions.size() % 3 != 0 || polygonIndices.empty()) return;
        const Layer normals = ReadLayer(&geometry, "LayerElementNormal", "Normals", 3);
        const Layer uvs = ReadLayer(&geometry, "LayerElementUV", "UV", 2);
        const float transformDeterminant = transform.GetDeterminant();
        Math::Matrix4x4 inverseTransform;
        if (std::abs(transformDeterminant) <= 1.0e-8f || !transform.TryInvert(inverseTransform))
            throw FbxError("FBX Model has a singular transform");
        const Math::Matrix4x4 normalTransform = inverseTransform.Transpose();
        const bool flipWinding = transformDeterminant * axisConversion.DeterminantSign() < 0.0f;
        struct Corner { std::int32_t controlPoint; std::size_t polygonVertex; };
        std::vector<Corner> corners;
        std::size_t polygon = 0;
        for (std::size_t polygonVertex = 0; polygonVertex < polygonIndices.size(); ++polygonVertex)
        {
            const std::int32_t encoded = polygonIndices[polygonVertex];
            corners.push_back({ encoded < 0 ? -encoded - 1 : encoded, polygonVertex });
            if (encoded >= 0) continue;
            for (std::size_t index = 1; index + 1 < corners.size(); ++index)
            {
                const Corner& second = flipWinding ? corners[index + 1] : corners[index];
                const Corner& third = flipWinding ? corners[index] : corners[index + 1];
                Core::SkinnedMeshVertex a = MakeSkinnedVertex(positions, corners[0].controlPoint,
                    corners[0].polygonVertex, polygon, normals, uvs, transform, normalTransform,
                    axisConversion, influences);
                Core::SkinnedMeshVertex b = MakeSkinnedVertex(positions, second.controlPoint,
                    second.polygonVertex, polygon, normals, uvs, transform, normalTransform,
                    axisConversion, influences);
                Core::SkinnedMeshVertex c = MakeSkinnedVertex(positions, third.controlPoint,
                    third.polygonVertex, polygon, normals, uvs, transform, normalTransform,
                    axisConversion, influences);
                if (normals.values.empty()) GenerateSkinnedTriangleNormal(a, b, c);
                if (mesh.vertices.size() > (std::numeric_limits<unsigned int>::max)() - 3)
                    throw FbxError("FBX mesh has too many vertices");
                const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(a); mesh.vertices.push_back(b); mesh.vertices.push_back(c);
                mesh.indices.push_back(base); mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
            }
            corners.clear();
            ++polygon;
        }
        if (!corners.empty()) throw FbxError("unterminated FBX polygon");
    }

    /// <summary>시간(초)마다 키를 가진 단일 실수 채널이다. FBX ticks는 초로 미리 바꿔 둔다.</summary>
    struct AnimCurve
    {
        struct Attribute
        {
            std::uint32_t flags = 4; // Linear when the file has no key attributes.
            float rightSlope = 0.0f;
            float nextLeftSlope = 0.0f;
        };
        std::vector<float> timesSeconds;
        std::vector<float> values;
        std::vector<Attribute> attributes;
    };

    AnimCurve ReadAnimCurve(const Node& curveNode, const bool readInterpolation = false)
    {
        // FBX의 1초는 정확히 이만큼의 ticks다 — 파일 전체에 고정된 상수이며 GlobalSettings에
        // 없다.
        constexpr double ticksPerSecond = 46186158000.0;
        AnimCurve curve;
        const std::vector<std::int64_t> ticks = ReadArray<std::int64_t>(FindChild(curveNode, "KeyTime"));
        const std::vector<float> values = ReadArray<float>(FindChild(curveNode, "KeyValueFloat"));
        const std::size_t count = (std::min)(ticks.size(), values.size());
        curve.timesSeconds.reserve(count);
        curve.values.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            curve.timesSeconds.push_back(static_cast<float>(static_cast<double>(ticks[index]) / ticksPerSecond));
            curve.values.push_back(values[index]);
            if (readInterpolation && (!std::isfinite(values[index]) ||
                (index > 0 && curve.timesSeconds[index] <= curve.timesSeconds[index - 1])))
                throw FbxError("invalid FBX rigid animation key values or times");
        }
        if (readInterpolation)
        {
            if (ticks.size() != values.size()) throw FbxError("mismatched FBX animation key arrays");
            const auto flags = ReadArray<std::int32_t>(FindChild(curveNode, "KeyAttrFlags"));
            const auto data = ReadArray<float>(FindChild(curveNode, "KeyAttrDataFloat"));
            const auto references = ReadArray<std::int32_t>(FindChild(curveNode, "KeyAttrRefCount"));
            if (!flags.empty())
            {
                if (data.size() / 4 != flags.size() || data.size() % 4 != 0 ||
                    (!references.empty() && references.size() != flags.size()))
                    throw FbxError("invalid FBX animation key attributes");
                curve.attributes.reserve(count);
                for (std::size_t group = 0; group < flags.size(); ++group)
                {
                    // FBX stores run-length encoded (flags, four floats) tuples. The first two
                    // floats are this key's outgoing slope and the next key's incoming slope.
                    const std::int32_t repeats = references.empty() ? 1 : references[group];
                    if (repeats <= 0 || static_cast<std::size_t>(repeats) > count - curve.attributes.size())
                        throw FbxError("invalid FBX animation attribute reference count");
                    AnimCurve::Attribute attribute{
                        static_cast<std::uint32_t>(flags[group]), data[group * 4], data[group * 4 + 1] };
                    const bool userTangent = (attribute.flags & 0x400u) != 0;
                    const bool timeIndependentAuto = (attribute.flags & 0x2100u) == 0x2100u;
                    if ((attribute.flags & 8u) != 0 && ((attribute.flags & 0x33000200u) != 0 ||
                        (!userTangent && !timeIndependentAuto)))
                        throw FbxError("unsupported FBX rigid animation tangent mode (weighted, velocity, TCB or biased auto)");
                    if ((attribute.flags & 8u) != 0 &&
                        (!std::isfinite(attribute.rightSlope) || !std::isfinite(attribute.nextLeftSlope)))
                        throw FbxError("non-finite FBX animation tangent");
                    curve.attributes.insert(curve.attributes.end(), static_cast<std::size_t>(repeats), attribute);
                }
                if (curve.attributes.size() != count) throw FbxError("incomplete FBX animation key attributes");
                for (std::size_t index = 0; index < count; ++index)
                {
                    AnimCurve::Attribute& attribute = curve.attributes[index];
                    if ((attribute.flags & 8u) == 0 || (attribute.flags & 0x400u) != 0) continue;
                    // Time-independent automatic tangents use the secant through both neighbors.
                    // Progressive clamp bounds both Bezier handles to the neighboring value range;
                    // at an endpoint the missing side makes the progressive tangent horizontal.
                    const std::size_t previous = index == 0 ? index : index - 1;
                    const std::size_t next = index + 1 < count ? index + 1 : index;
                    double slope = next == previous ? 0.0 :
                        (static_cast<double>(curve.values[next]) - curve.values[previous]) /
                        (static_cast<double>(curve.timesSeconds[next]) - curve.timesSeconds[previous]);
                    const double leftDelta = static_cast<double>(curve.values[index]) - curve.values[previous];
                    const double rightDelta = static_cast<double>(curve.values[next]) - curve.values[index];
                    if ((attribute.flags & 0x1000u) != 0 &&
                        (std::abs(leftDelta) <= 1e-6 || std::abs(rightDelta) <= 1e-6)) slope = 0.0;
                    if ((attribute.flags & 0x4000u) != 0)
                    {
                        if (previous == index || next == index || leftDelta * rightDelta <= 0.0) slope = 0.0;
                        else
                        {
                            const double leftLimit = 3.0 * std::abs(leftDelta) /
                                (static_cast<double>(curve.timesSeconds[index]) - curve.timesSeconds[previous]);
                            const double rightLimit = 3.0 * std::abs(rightDelta) /
                                (static_cast<double>(curve.timesSeconds[next]) - curve.timesSeconds[index]);
                            slope = std::copysign((std::min)({ std::abs(slope), leftLimit, rightLimit }), slope);
                        }
                    }
                    if (!std::isfinite(slope) || std::abs(slope) > (std::numeric_limits<float>::max)())
                        throw FbxError("non-finite FBX automatic tangent");
                    attribute.rightSlope = static_cast<float>(slope);
                    if (index > 0) curve.attributes[index - 1].nextLeftSlope = static_cast<float>(slope);
                }
            }
        }
        return curve;
    }

    /// <summary>주어진 시각에서의 채널 값이다. 양 끝 밖은 그 끝 값을 유지하고, 안쪽은 선형 보간한다.</summary>
    float EvaluateAnimCurve(const AnimCurve& curve, const float time, const float fallback)
    {
        if (curve.timesSeconds.empty()) return fallback;
        if (time <= curve.timesSeconds.front()) return curve.values.front();
        if (time >= curve.timesSeconds.back()) return curve.values.back();
        for (std::size_t index = 1; index < curve.timesSeconds.size(); ++index)
        {
            if (time <= curve.timesSeconds[index])
            {
                const float t0 = curve.timesSeconds[index - 1];
                const float t1 = curve.timesSeconds[index];
                const float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
                // rigid 경로가 읽은 속성이 있으면 원본의 constant/cubic을 따른다. 없으면 기존 선형 경로다.
                if (!curve.attributes.empty())
                {
                    const AnimCurve::Attribute& attribute = curve.attributes[index - 1];
                    if (time == t1) return curve.values[index];
                    if ((attribute.flags & 2u) != 0)
                        return curve.values[(attribute.flags & 0x100u) != 0 ? index : index - 1];
                    if ((attribute.flags & 8u) != 0)
                    {
                        const double a = alpha;
                        const double a2 = a * a;
                        const double a3 = a2 * a;
                        return static_cast<float>((2 * a3 - 3 * a2 + 1) * curve.values[index - 1] +
                            (a3 - 2 * a2 + a) * (t1 - t0) * attribute.rightSlope +
                            (-2 * a3 + 3 * a2) * curve.values[index] +
                            (a3 - a2) * (t1 - t0) * attribute.nextLeftSlope);
                    }
                }
                return curve.values[index - 1] + (curve.values[index] - curve.values[index - 1]) * alpha;
            }
        }
        return curve.values.back();
    }

    /// <summary>AnimationCurveNode가 애니메이션시키는 자리다 — 어느 Model의 어느 벡터 속성인지.</summary>
    struct CurveNodeTarget
    {
        std::int64_t modelId = 0;
        std::string property;
    };

    struct AnimCurveNode
    {
        std::optional<CurveNodeTarget> target;
        std::optional<std::int64_t> curveIdX;
        std::optional<std::int64_t> curveIdY;
        std::optional<std::int64_t> curveIdZ;
    };

    struct ParsedAnimation
    {
        /// <summary>재현 가능한 순서를 위해 id 오름차순으로 정렬된 AnimationStack들이다.</summary>
        std::vector<std::int64_t> orderedStackIds;
        std::unordered_map<std::int64_t, std::string> stackNames;
        std::unordered_map<std::int64_t, std::vector<std::int64_t>> layersByStack;
        std::unordered_map<std::int64_t, std::vector<std::int64_t>> curveNodesByLayer;
        std::unordered_map<std::int64_t, AnimCurveNode> curveNodes;
        std::unordered_map<std::int64_t, const Node*> curves;
    };

    /// <summary>
    /// AnimationStack -OO-> AnimationLayer -OO-> AnimationCurveNode -OP-> Model(속성 이름)과,
    /// AnimationCurve -OP-> AnimationCurveNode(축 이름 "d|X"/"d|Y"/"d|Z")를 읽는다.
    /// </summary>
    ParsedAnimation ParseAnimationGraph(const std::vector<Node>& nodes)
    {
        ParsedAnimation animation;
        const Node* objects = FindRoot(nodes, "Objects");
        if (!objects) return animation;

        std::unordered_set<std::int64_t> stackIds;
        std::unordered_set<std::int64_t> layerIds;
        for (const Node& object : objects->children)
        {
            const std::optional<std::int64_t> id = PropertyInteger(&object);
            if (!id) continue;
            if (object.name == "AnimationStack")
            {
                if (stackIds.insert(*id).second)
                {
                    animation.orderedStackIds.push_back(*id);
                    animation.stackNames.emplace(*id, ObjectName(&object));
                }
            }
            else if (object.name == "AnimationLayer")
            {
                layerIds.insert(*id);
            }
            else if (object.name == "AnimationCurveNode")
            {
                animation.curveNodes.emplace(*id, AnimCurveNode{});
            }
            else if (object.name == "AnimationCurve")
            {
                animation.curves.emplace(*id, &object);
            }
        }
        std::ranges::sort(animation.orderedStackIds);

        if (const Node* connections = FindRoot(nodes, "Connections"))
        {
            for (const Node& connection : connections->children)
            {
                if (connection.name != "C") continue;
                const std::string kind = PropertyString(&connection);
                const std::optional<std::int64_t> child = PropertyInteger(&connection, 1);
                const std::optional<std::int64_t> parent = PropertyInteger(&connection, 2);
                if (!child || !parent) continue;
                if (kind == "OO")
                {
                    if (layerIds.contains(*child) && stackIds.contains(*parent))
                        animation.layersByStack[*parent].push_back(*child);
                    else if (animation.curveNodes.contains(*child) && layerIds.contains(*parent))
                        animation.curveNodesByLayer[*parent].push_back(*child);
                }
                else if (kind == "OP")
                {
                    const std::string propertyName = PropertyString(&connection, 3);
                    if (const auto curveNode = animation.curveNodes.find(*child);
                        curveNode != animation.curveNodes.end())
                    {
                        curveNode->second.target = CurveNodeTarget{ *parent, propertyName };
                    }
                    else if (animation.curves.contains(*child))
                    {
                        const auto targetCurveNode = animation.curveNodes.find(*parent);
                        if (targetCurveNode != animation.curveNodes.end())
                        {
                            if (propertyName == "d|X") targetCurveNode->second.curveIdX = *child;
                            else if (propertyName == "d|Y") targetCurveNode->second.curveIdY = *child;
                            else if (propertyName == "d|Z") targetCurveNode->second.curveIdZ = *child;
                        }
                    }
                }
            }
        }
        return animation;
    }

    std::vector<std::int64_t> CurveNodesOfStack(const ParsedAnimation& animation, const std::int64_t stackId)
    {
        std::vector<std::int64_t> result;
        const auto layers = animation.layersByStack.find(stackId);
        if (layers == animation.layersByStack.end()) return result;
        for (const std::int64_t layerId : layers->second)
        {
            const auto curveNodes = animation.curveNodesByLayer.find(layerId);
            if (curveNodes == animation.curveNodesByLayer.end()) continue;
            result.insert(result.end(), curveNodes->second.begin(), curveNodes->second.end());
        }
        return result;
    }

    /// <summary>한 AnimationCurveNode의 최대 세 축(X/Y/Z)이, 미리 디코딩된 채로다.</summary>
    struct DecodedVectorCurves
    {
        std::optional<AnimCurve> x;
        std::optional<AnimCurve> y;
        std::optional<AnimCurve> z;
    };

    DecodedVectorCurves DecodeVectorCurves(
        const AnimCurveNode* node, const ParsedAnimation& animation, const bool readInterpolation = false)
    {
        DecodedVectorCurves decoded;
        if (!node) return decoded;
        const auto decodeOne = [&animation, readInterpolation](const std::optional<std::int64_t>& curveId) -> std::optional<AnimCurve>
        {
            if (!curveId) return std::nullopt;
            const auto curve = animation.curves.find(*curveId);
            if (curve == animation.curves.end()) return std::nullopt;
            return ReadAnimCurve(*curve->second, readInterpolation);
        };
        decoded.x = decodeOne(node->curveIdX);
        decoded.y = decodeOne(node->curveIdY);
        decoded.z = decodeOne(node->curveIdZ);
        return decoded;
    }

    Math::Vector3 EvaluateVectorCurves(const DecodedVectorCurves& curves, const Math::Vector3& fallback, const float time)
    {
        return {
            curves.x ? EvaluateAnimCurve(*curves.x, time, fallback.GetX()) : fallback.GetX(),
            curves.y ? EvaluateAnimCurve(*curves.y, time, fallback.GetY()) : fallback.GetY(),
            curves.z ? EvaluateAnimCurve(*curves.z, time, fallback.GetZ()) : fallback.GetZ()
        };
    }

    // Only geometry moved by an actual stack's Model TRS track becomes a rigid skin. Static FBX
    // files and blend-shape-only animation retain their existing sub-asset list.
    std::unordered_set<std::int64_t> RigidAnimatedInstances(
        const ParsedScene& scene, const ParsedAnimation& animation)
    {
        std::unordered_set<std::int64_t> animatedModels;
        for (const std::int64_t stackId : animation.orderedStackIds)
        {
            for (const std::int64_t nodeId : CurveNodesOfStack(animation, stackId))
            {
                const AnimCurveNode& node = animation.curveNodes.at(nodeId);
                if (!node.target || !scene.models.contains(node.target->modelId)) continue;
                const std::string& property = node.target->property;
                if (property != "Lcl Translation" && property != "Lcl Rotation" && property != "Lcl Scaling") continue;
                const DecodedVectorCurves curves = DecodeVectorCurves(&node, animation);
                if ((curves.x && !curves.x->timesSeconds.empty()) ||
                    (curves.y && !curves.y->timesSeconds.empty()) ||
                    (curves.z && !curves.z->timesSeconds.empty()))
                    animatedModels.insert(node.target->modelId);
            }
        }
        std::unordered_set<std::int64_t> instances;
        for (const auto& [geometryId, modelIds] : scene.geometryInstances)
        {
            if (!scene.geometries.contains(geometryId)) continue;
            for (const std::int64_t modelId : modelIds)
            {
                std::unordered_set<std::int64_t> visited;
                std::optional<std::int64_t> current = modelId;
                while (current && scene.models.contains(*current))
                {
                    if (!visited.insert(*current).second) throw FbxError("circular FBX model hierarchy");
                    if (animatedModels.contains(*current))
                    {
                        instances.insert(modelId);
                        break;
                    }
                    current = scene.models.at(*current).parentId;
                }
            }
        }
        return instances;
    }

    double MaximumCurveSpeed(const std::optional<AnimCurve>& curve)
    {
        if (!curve) return 0.0;
        double maximum = 0.0;
        for (std::size_t index = 1; index < curve->timesSeconds.size(); ++index)
        {
            const double dt = static_cast<double>(curve->timesSeconds[index]) - curve->timesSeconds[index - 1];
            const double delta = static_cast<double>(curve->values[index]) - curve->values[index - 1];
            const AnimCurve::Attribute attribute = curve->attributes.empty()
                ? AnimCurve::Attribute{} : curve->attributes[index - 1];
            if ((attribute.flags & 2u) != 0) continue;
            double speed = std::abs(delta / dt);
            if ((attribute.flags & 8u) != 0)
            {
                // Derivative of the Hermite polynomial in normalized time. Its absolute maximum
                // is at an endpoint or the quadratic's extremum, including tangent overshoots.
                const double m0 = attribute.rightSlope * dt;
                const double m1 = attribute.nextLeftSlope * dt;
                const double a = -6 * delta + 3 * m0 + 3 * m1;
                const double b = 6 * delta - 4 * m0 - 2 * m1;
                speed = (std::max)(std::abs(attribute.rightSlope), std::abs(attribute.nextLeftSlope));
                if (a != 0.0)
                {
                    const double u = -b / (2 * a);
                    if (u > 0.0 && u < 1.0) speed = (std::max)(speed, std::abs((a * u * u + b * u + m0) / dt));
                }
            }
            maximum = (std::max)(maximum, speed);
        }
        return maximum;
    }

    constexpr std::size_t gMaximumRigidSamples = 262144;

    void BakeRigidSampleTimes(
        std::vector<float>& times, const DecodedVectorCurves& rotations, std::size_t& remainingSamples)
    {
        // Sum of Euler-axis speeds bounds the composed orientation speed, even when several axes
        // move together. Keep each quaternion interval below 45 degrees so Slerp keeps full turns.
        const double angularSpeed = MaximumCurveSpeed(rotations.x) + MaximumCurveSpeed(rotations.y) +
            MaximumCurveSpeed(rotations.z);
        const double samplesPerSecond = (std::max)(120.0, angularSpeed / 45.0);
        std::vector<float> baked;
        if (remainingSamples == 0) throw FbxError("FBX rigid animation exceeds baked key limit");
        baked.push_back(times.front());
        for (std::size_t index = 1; index < times.size(); ++index)
        {
            const double start = times[index - 1];
            const double dt = static_cast<double>(times[index]) - start;
            const double steps = (std::max)(1.0, std::ceil(dt * samplesPerSecond));
            if (!std::isfinite(steps) || steps > static_cast<double>(remainingSamples - baked.size()))
                throw FbxError("FBX rigid animation exceeds baked key limit");
            const std::size_t count = static_cast<std::size_t>(steps);
            for (std::size_t step = 1; step < count; ++step)
                baked.push_back(static_cast<float>(start + dt * static_cast<double>(step) / static_cast<double>(count)));
            baked.push_back(times[index]);
        }
        remainingSamples -= baked.size();
        times = std::move(baked);
    }

    /// <summary>
    /// 한 AnimationStack을 한 AnimationClip으로 채점한다. translation·rotation·scaling이 피벗
    /// 공식을 통해 서로 얽히므로(비어 있지 않은 피벗에서는 이동조차 회전에 좌우된다), 세 채널을
    /// 따로 채점하지 않는다 — 셋의 키 시각을 모두 합친 시점마다 전체 로컬 변환을 다시 조립해
    /// 분해한다. 그래서 한 뼈의 세 키 배열은 언제나 같은 시각 집합을 갖는다.
    /// </summary>
    Animation::AnimationClip BuildClip(
        const std::int64_t stackId,
        const ParsedAnimation& animation,
        const ParsedScene& scene,
        const BoneOrder& boneOrder,
        const Math::Matrix4x4& axisMatrix,
        const Math::Matrix4x4& inverseAxisMatrix,
        const bool bakeRigid = false)
    {
        Animation::AnimationClip clip;
        const auto nameIterator = animation.stackNames.find(stackId);
        clip.name = nameIterator != animation.stackNames.end() ? nameIterator->second : std::string{};

        struct BonePropertyCurveNodes
        {
            const AnimCurveNode* translation = nullptr;
            const AnimCurveNode* rotation = nullptr;
            const AnimCurveNode* scaling = nullptr;
        };
        std::map<std::uint32_t, BonePropertyCurveNodes> byBone;
        for (const std::int64_t curveNodeId : CurveNodesOfStack(animation, stackId))
        {
            const auto curveNodeIterator = animation.curveNodes.find(curveNodeId);
            if (curveNodeIterator == animation.curveNodes.end() || !curveNodeIterator->second.target) continue;
            const auto boneIndexIterator = boneOrder.indexByModelId.find(curveNodeIterator->second.target->modelId);
            if (boneIndexIterator == boneOrder.indexByModelId.end()) continue;
            BonePropertyCurveNodes& entry = byBone[boneIndexIterator->second];
            const std::string& property = curveNodeIterator->second.target->property;
            if (property == "Lcl Translation") entry.translation = &curveNodeIterator->second;
            else if (property == "Lcl Rotation") entry.rotation = &curveNodeIterator->second;
            else if (property == "Lcl Scaling") entry.scaling = &curveNodeIterator->second;
        }

        float duration = 0.0f;
        float firstTime = (std::numeric_limits<float>::max)();
        // 베이크 예산은 뼈마다 새로 주지 않고 클립 전체가 나눠 쓴다.
        std::size_t remainingSamples = gMaximumRigidSamples;
        for (const auto& [boneIndex, entry] : byBone)
        {
            const Model& model = scene.models.at(boneOrder.orderedModelIds[boneIndex]);
            // A root bone's ancestor (outside the skeleton) is assumed unanimated -- same
            // correction as BuildSkeleton's bind pose, folded in here so the animated pose stays in
            // the same space as the bind pose it displaces.
            const Math::Matrix4x4 ancestorGlobal = (model.parentId && boneOrder.indexByModelId.contains(*model.parentId))
                ? Math::Matrix4x4::Identity()
                : AncestorGlobalTransform(model, scene.models);
            const LocalTransformPivots pivots = ReadLocalTransformPivots(*model.node);
            const Math::Vector3 defaultTranslation = ReadVectorProperty(*model.node, "Lcl Translation", Math::Vector3{});
            const Math::Vector3 defaultRotation = ReadVectorProperty(*model.node, "Lcl Rotation", Math::Vector3{});
            const Math::Vector3 defaultScaling =
                ReadVectorProperty(*model.node, "Lcl Scaling", Math::Vector3{ 1.0f, 1.0f, 1.0f });

            const DecodedVectorCurves translationCurves = DecodeVectorCurves(entry.translation, animation, bakeRigid);
            const DecodedVectorCurves rotationCurves = DecodeVectorCurves(entry.rotation, animation, bakeRigid);
            const DecodedVectorCurves scalingCurves = DecodeVectorCurves(entry.scaling, animation, bakeRigid);

            std::vector<float> sampleTimes;
            const auto collectTimes = [&sampleTimes](const DecodedVectorCurves& curves)
            {
                if (curves.x) sampleTimes.insert(sampleTimes.end(), curves.x->timesSeconds.begin(), curves.x->timesSeconds.end());
                if (curves.y) sampleTimes.insert(sampleTimes.end(), curves.y->timesSeconds.begin(), curves.y->timesSeconds.end());
                if (curves.z) sampleTimes.insert(sampleTimes.end(), curves.z->timesSeconds.begin(), curves.z->timesSeconds.end());
            };
            collectTimes(translationCurves);
            collectTimes(rotationCurves);
            collectTimes(scalingCurves);
            if (sampleTimes.empty()) continue;

            std::ranges::sort(sampleTimes);
            sampleTimes.erase(
                std::ranges::unique(sampleTimes, [](const float a, const float b) { return std::abs(a - b) < 1e-6f; }).begin(),
                sampleTimes.end());
            firstTime = (std::min)(firstTime, sampleTimes.front());
            if (bakeRigid) BakeRigidSampleTimes(sampleTimes, rotationCurves, remainingSamples);

            Animation::BoneTrack track;
            track.boneIndex = boneIndex;
            track.positionKeys.reserve(sampleTimes.size());
            track.rotationKeys.reserve(sampleTimes.size());
            track.scaleKeys.reserve(sampleTimes.size());
            for (const float time : sampleTimes)
            {
                const Math::Vector3 translation = EvaluateVectorCurves(translationCurves, defaultTranslation, time);
                const Math::Vector3 rotation = EvaluateVectorCurves(rotationCurves, defaultRotation, time);
                const Math::Vector3 scaling = EvaluateVectorCurves(scalingCurves, defaultScaling, time);
                const Math::Matrix4x4 local = ComposeLocalTransform(translation, rotation, scaling, pivots) * ancestorGlobal;
                const Math::Matrix4x4 converted = inverseAxisMatrix * local * axisMatrix;

                Math::Vector3 scale, rotationDegreesUnused, position;
                converted.Decompose(scale, rotationDegreesUnused, position);
                track.positionKeys.push_back({ time, position });
                track.rotationKeys.push_back({ time, Math::Quaternion::FromMatrix(converted) });
                track.scaleKeys.push_back({ time, scale });
                duration = (std::max)(duration, time);
            }
            clip.tracks.push_back(std::move(track));
        }
        if (bakeRigid && !clip.tracks.empty())
        {
            // 모든 트랙을 같은 원점으로 옮겨야 늦게 시작하는 뼈의 지연을 보존하며 클립은 0초에 시작한다.
            duration = 0.0f;
            for (Animation::BoneTrack& track : clip.tracks)
            {
                for (auto& key : track.positionKeys) key.time -= firstTime;
                for (auto& key : track.rotationKeys) key.time -= firstTime;
                for (auto& key : track.scaleKeys) key.time -= firstTime;
                duration = (std::max)(duration, track.rotationKeys.back().time);
            }
        }
        clip.duration = duration;
        return clip;
    }
}

bool FbxImporter::Load(
    const std::span<const std::byte> fileBytes,
    std::vector<ImportedMesh>& meshes,
    std::string& error)
{
    meshes.clear();
    error.clear();
    try
    {
        // The caller supplies the complete file bytes. The importer parses those bytes without
        // deciding where project files live.
        ParsedScene scene = ParseSceneGraph(fileBytes);
        std::unordered_map<std::int64_t, Math::Matrix4x4> globalTransforms;
        std::unordered_set<std::int64_t> resolving;

        // Visit geometries in id order. The map that holds them is unordered, and the position of a
        // mesh in the result is what an AssetReference's local id names, so an unspecified order
        // would make a saved reference mean something different from one run to the next.
        std::vector<std::int64_t> orderedGeometryIds;
        orderedGeometryIds.reserve(scene.geometries.size());
        for (const auto& [geometryId, geometry] : scene.geometries) orderedGeometryIds.push_back(geometryId);
        std::ranges::sort(orderedGeometryIds);

        // A mesh is named after the model that places it, because that is the name a modelling tool
        // shows; the geometry's own name is the fallback for a geometry no model instances. The name
        // is what the editor lists a sub-asset by, so an unnamed mesh still has its local id.
        const auto appendMesh = [&meshes](ImportedMesh&& imported, std::string name)
        {
            if (!imported.vertices.empty() && !imported.indices.empty())
            {
                imported.name = std::move(name);
                meshes.push_back(std::move(imported));
            }
        };

        for (const std::int64_t geometryId : orderedGeometryIds)
        {
            const Node* const geometry = scene.geometries.at(geometryId);
            const auto instances = scene.geometryInstances.find(geometryId);
            if (instances == scene.geometryInstances.end() || instances->second.empty())
            {
                ImportedMesh imported;
                ImportGeometry(*geometry, Math::Matrix4x4::Identity(), scene.axisConversion, imported);
                appendMesh(std::move(imported), ObjectName(geometry));
                continue;
            }
            // Instances keep the order the connections were read in, which the file already fixes.
            for (const std::int64_t modelId : instances->second)
            {
                const Model& model = scene.models.at(modelId);
                const Math::Matrix4x4 transform = model.geometric * ResolveGlobalTransform(
                    modelId, scene.models, globalTransforms, resolving);
                ImportedMesh imported;
                ImportGeometry(*geometry, transform, scene.axisConversion, imported);
                std::string name = ObjectName(model.node);
                appendMesh(std::move(imported), name.empty() ? ObjectName(geometry) : std::move(name));
            }
        }
        if (meshes.empty()) throw FbxError("FBX file contains no supported static mesh geometry");
        return true;
    }
    catch (const std::exception& exception)
    {
        meshes.clear();
        error = exception.what();
        return false;
    }
}

bool FbxImporter::LoadSkeleton(
    const std::span<const std::byte> fileBytes,
    std::vector<SkinnedMeshData>& meshes,
    std::vector<std::string>& meshNames,
    Animation::Skeleton& skeleton,
    std::vector<Animation::AnimationClip>& clips,
    std::string& error,
    bool* const rigidAnimation)
{
    if (rigidAnimation) *rigidAnimation = false;
    meshes.clear();
    meshNames.clear();
    skeleton = Animation::Skeleton{};
    clips.clear();
    error.clear();
    try
    {
        const ParsedScene scene = ParseSceneGraph(fileBytes);
        const ParsedSkinning skinning = ParseSkinning(scene);
        const ParsedAnimation animation = ParseAnimationGraph(scene.nodes);

        std::vector<std::int64_t> clusterBoneIds;
        for (const auto& [clusterId, cluster] : skinning.clusters)
        {
            if (cluster.targetModelId) clusterBoneIds.push_back(*cluster.targetModelId);
        }
        const bool rigid = clusterBoneIds.empty();
        const std::unordered_set<std::int64_t> rigidInstances = rigid
            ? RigidAnimatedInstances(scene, animation) : std::unordered_set<std::int64_t>{};
        if (rigid) clusterBoneIds.assign(rigidInstances.begin(), rigidInstances.end());
        if (clusterBoneIds.empty()) throw FbxError("FBX file has no skinned or rigid animated geometry");
        std::ranges::sort(clusterBoneIds);
        clusterBoneIds.erase(std::ranges::unique(clusterBoneIds).begin(), clusterBoneIds.end());

        const BoneOrder boneOrder = BuildBoneOrder(clusterBoneIds, scene.models, rigid);
        const Math::Matrix4x4 axisMatrix = AxisConversionMatrix(scene.axisConversion);
        Math::Matrix4x4 inverseAxisMatrix;
        if (!axisMatrix.TryInvert(inverseAxisMatrix)) throw FbxError("FBX axis conversion is singular");
        skeleton = BuildSkeleton(boneOrder, scene.models, axisMatrix, inverseAxisMatrix);
        if (!skeleton.IsValid()) throw FbxError("FBX file produced an invalid bone hierarchy");

        // Skinned geometries in id order, for the same reason Load() visits meshes in id order: the
        // position in the result is what a saved reference names.
        std::vector<std::int64_t> orderedGeometryIds;
        if (rigid)
        {
            for (const auto& [geometryId, instances] : scene.geometryInstances)
            {
                if (std::ranges::any_of(instances, [&rigidInstances](const auto id) { return rigidInstances.contains(id); }))
                    orderedGeometryIds.push_back(geometryId);
            }
        }
        else
        {
            for (const auto& [geometryId, skinId] : skinning.skinByGeometry) orderedGeometryIds.push_back(geometryId);
        }
        std::ranges::sort(orderedGeometryIds);

        std::unordered_map<std::int64_t, Math::Matrix4x4> globalTransforms;
        std::unordered_set<std::int64_t> resolving;
        for (const std::int64_t geometryId : orderedGeometryIds)
        {
            const auto geometryIterator = scene.geometries.find(geometryId);
            const auto instancesIterator = scene.geometryInstances.find(geometryId);
            if (geometryIterator == scene.geometries.end() ||
                instancesIterator == scene.geometryInstances.end() || instancesIterator->second.empty())
                continue;
            const std::vector<double> positions = ReadNumericArray(FindChild(*geometryIterator->second, "Vertices"));
            // A skinned geometry is bound to a single bind pose, so only its first instance is used —
            // rigid geometry instead has one independent bind model for every animated instance.
            std::vector<std::int64_t> instances = instancesIterator->second;
            if (!rigid) instances.resize(1);
            for (const std::int64_t modelId : instances)
            {
                if (rigid && !rigidInstances.contains(modelId)) continue;
                std::vector<BoneInfluence> influences;
                if (rigid)
                {
                    // Model 변환을 뼈 하나의 weight 1로 표현하면 변형 없는 회전도 기존 포즈 경로로 재생된다.
                    BoneInfluence influence;
                    influence.boneIndices[0] = boneOrder.indexByModelId.at(modelId);
                    influence.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
                    influences.assign(positions.size() / 3, influence);
                }
                else
                {
                    const Skin& skin = skinning.skins.at(skinning.skinByGeometry.at(geometryId));
                    influences = BuildBoneInfluences(skin, skinning, boneOrder, positions.size() / 3);
                }
                const Model& model = scene.models.at(modelId);
                const Math::Matrix4x4 transform =
                    model.geometric * ResolveGlobalTransform(modelId, scene.models, globalTransforms, resolving);
                SkinnedMeshData mesh;
                ImportSkinnedGeometry(*geometryIterator->second, transform, scene.axisConversion, influences, mesh);
                if (mesh.vertices.empty() || mesh.indices.empty()) continue;
                mesh.bounds = ComputeSkinnedBounds(mesh.vertices);
                std::string name = ObjectName(model.node);
                meshNames.push_back(name.empty() ? ObjectName(geometryIterator->second) : std::move(name));
                meshes.push_back(std::move(mesh));
            }
        }
        if (meshes.empty()) throw FbxError("FBX file's skinned geometry produced no drawable mesh");

        for (const std::int64_t stackId : animation.orderedStackIds)
        {
            Animation::AnimationClip clip =
                BuildClip(stackId, animation, scene, boneOrder, axisMatrix, inverseAxisMatrix, rigid);
            // A stack with no bone tracks at all is indistinguishable from no animation, so it is
            // not a clip a caller could do anything with.
            if (!clip.tracks.empty()) clips.push_back(std::move(clip));
        }
        if (rigidAnimation) *rigidAnimation = rigid;
        return true;
    }
    catch (const std::exception& exception)
    {
        meshes.clear();
        meshNames.clear();
        skeleton = Animation::Skeleton{};
        clips.clear();
        error = exception.what();
        return false;
    }
}

}
