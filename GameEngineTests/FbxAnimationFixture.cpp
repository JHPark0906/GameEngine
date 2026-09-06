#include "FbxAnimationFixture.h"

#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace TestSupport::FbxFixture
{
namespace
{
    template<typename T>
    void Append(Bytes& bytes, const T value)
    {
        const auto* first = reinterpret_cast<const std::byte*>(&value);
        bytes.insert(bytes.end(), first, first + sizeof(T));
    }

    template<typename T>
    Bytes Scalar(const char type, const T value)
    {
        Bytes bytes{ static_cast<std::byte>(type) };
        Append(bytes, value);
        return bytes;
    }

    Bytes String(const std::string_view value)
    {
        Bytes bytes = Scalar('S', static_cast<std::uint32_t>(value.size()));
        const auto* first = reinterpret_cast<const std::byte*>(value.data());
        bytes.insert(bytes.end(), first, first + value.size());
        return bytes;
    }

    template<typename T>
    Bytes Array(const char type, const std::initializer_list<T> values)
    {
        Bytes bytes = Scalar(type, static_cast<std::uint32_t>(values.size()));
        Append(bytes, std::uint32_t{ 0 });
        Append(bytes, static_cast<std::uint32_t>(values.size() * sizeof(T)));
        for (const T value : values) Append(bytes, value);
        return bytes;
    }

    struct Node
    {
        std::string name;
        std::vector<Bytes> properties;
        std::vector<Node> children;
    };

    void WriteNode(Bytes& bytes, const Node& node)
    {
        const std::size_t header = bytes.size();
        Append(bytes, std::uint64_t{ 0 });
        Append(bytes, static_cast<std::uint64_t>(node.properties.size()));
        std::uint64_t propertyBytes = 0;
        for (const Bytes& property : node.properties) propertyBytes += property.size();
        Append(bytes, propertyBytes);
        Append(bytes, static_cast<std::uint8_t>(node.name.size()));
        for (const char value : node.name) bytes.push_back(static_cast<std::byte>(value));
        for (const Bytes& property : node.properties) bytes.insert(bytes.end(), property.begin(), property.end());
        for (const Node& child : node.children) WriteNode(bytes, child);
        bytes.resize(bytes.size() + 25); // FBX 7.7 uses 64-bit node records and a 25-byte sentinel.
        const std::uint64_t end = bytes.size();
        std::memcpy(bytes.data() + header, &end, sizeof(end));
    }

    Node VectorProperty(const std::string& name, const double x, const double y, const double z)
    {
        return { "P", { String(name), String("Vector3D"), String("Vector"), String(""),
            Scalar('D', x), Scalar('D', y), Scalar('D', z) }, {} };
    }

    Node Object(const std::string& kind, const std::int64_t id, const std::string& name,
        const std::string& subtype, std::vector<Node> children = {})
    {
        return { kind, { Scalar('L', id), String(name), String(subtype) }, std::move(children) };
    }

    Node Connection(const std::int64_t child, const std::int64_t parent, const std::string& property = {})
    {
        Node node{ "C", { String(property.empty() ? "OO" : "OP"), Scalar('L', child), Scalar('L', parent) }, {} };
        if (!property.empty()) node.properties.push_back(String(property));
        return node;
    }

}

Bytes MakeFixture(const FixtureOptions options)
{
    std::vector<Node> objects{
        Object("Geometry", 10, "Triangle", "Mesh", {
            { "Vertices", { Array<double>('d', { 0, 0, 0, 1, 0, 0, 0, 1, 0 }) }, {} },
            { "PolygonVertexIndex", { Array<std::int32_t>('i', { 0, 1, -3 }) }, {} } }),
        Object("Model", 20, "Child", "Mesh", { { "Properties70", {}, {
            VectorProperty("Lcl Translation", 3, 0, 0),
            VectorProperty("GeometricTranslation", options.geometricTranslation, 0, 0) } } }),
        Object("Model", 30, "Parent", "Null", { { "Properties70", {}, {
            VectorProperty("Lcl Translation", 10, 0, 0) } } }) };
    std::vector<Node> connections{ Connection(10, 20), Connection(20, 30) };
    if (options.animated)
    {
        constexpr std::int64_t second = 46186158000ll;
        // Rigid import covers pre-roll normalization; the skinned fixture animates from startup.
        const std::int64_t firstKey = options.skin ? 0 : second * 2;
        const std::int64_t lastKey = firstKey + second * 2;
        objects.push_back(Object("AnimationStack", 100, "Turns", ""));
        objects.push_back(Object("AnimationLayer", 101, "Layer", ""));
        objects.push_back(Object("AnimationCurveNode", 102, "R", ""));
        objects.push_back(Object("AnimationCurve", 103, "Yaw", "", {
            { "KeyTime", { Array<std::int64_t>('l', { firstKey, lastKey }) }, {} },
            { "KeyValueFloat", { Array<float>('f', { 0.0f, options.angle }) }, {} },
            { "KeyAttrFlags", { Array<std::int32_t>('i', { options.flags }) }, {} },
            { "KeyAttrDataFloat", { Array<float>('f', { options.angle * (5.0f / 6.0f),
                options.angle / 6.0f, 0.0f, 0.0f }) }, {} },
            { "KeyAttrRefCount", { Array<std::int32_t>('i', { options.references }) }, {} } }));
        // A constant-valued time-independent auto curve exercises FBX's compressed key groups
        // and keeps a later, unanimated child in the same hierarchy as its moving parent.
        objects.push_back(Object("AnimationCurveNode", 104, "T", ""));
        objects.push_back(Object("AnimationCurve", 105, "ChildX", "", {
            { "KeyTime", { Array<std::int64_t>('l', { firstKey, lastKey }) }, {} },
            { "KeyValueFloat", { Array<float>('f', { 3.0f, 3.0f }) }, {} },
            { "KeyAttrFlags", { Array<std::int32_t>('i', { 24840 }) }, {} },
            { "KeyAttrDataFloat", { Array<float>('f', { 0, 0, 0, 0 }) }, {} },
            { "KeyAttrRefCount", { Array<std::int32_t>('i', { 2 }) }, {} } }));
        connections.push_back(Connection(101, 100));
        connections.push_back(Connection(102, 101));
        connections.push_back(Connection(102, options.animateParent ? 30 : 20, "Lcl Rotation"));
        connections.push_back(Connection(103, 102, "d|Y"));
        connections.push_back(Connection(104, 101));
        connections.push_back(Connection(104, 20, "Lcl Translation"));
        connections.push_back(Connection(105, 104, "d|X"));
    }
    if (options.skin)
    {
        objects.push_back(Object("Deformer", 200, "Skin", "Skin"));
        objects.push_back(Object("Deformer", 201, "Cluster", "Cluster", {
            { "Indexes", { Array<std::int32_t>('i', { 0, 1, 2 }) }, {} },
            { "Weights", { Array<double>('d', { 1, 1, 1 }) }, {} } }));
        connections.push_back(Connection(200, 10));
        connections.push_back(Connection(201, 200));
        connections.push_back(Connection(20, 201));
    }
    constexpr char signature[] = "Kaydara FBX Binary  \0\x1a\0";
    Bytes bytes;
    for (std::size_t index = 0; index < 23; ++index) bytes.push_back(static_cast<std::byte>(signature[index]));
    Append(bytes, std::uint32_t{ 7700 });
    WriteNode(bytes, { "Objects", {}, std::move(objects) });
    WriteNode(bytes, { "Connections", {}, std::move(connections) });
    bytes.resize(bytes.size() + 25);
    return bytes;
}

Bytes MakeSkinnedFixture()
{
    FixtureOptions options;
    options.skin = true;
    options.animateParent = false;
    options.angle = 90.0f;
    options.geometricTranslation = 0.0;
    return MakeFixture(options);
}

}
