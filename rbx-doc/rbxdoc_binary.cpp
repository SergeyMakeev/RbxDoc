#include <stdexcept>
#include <stdio.h>
#include <vector>

#include <lz4.h>
#include <zstd.h>

#include "rbxdoc.h"
#include "rbxdoc_binary.h"

namespace rbxdoc
{

// zstd frame header (https://datatracker.ietf.org/doc/rfc8878/, 3.1.1)
static const char kZStdFrameHeader[] = "\x28\xb5\x2f\xfd";

static const char kMagicHeader[] = "<roblox!";
static const char kHeaderSignature[] = "\x89\xff\x0d\x0a\x1a\x0a";
static const char kChunkInstances[] = "INST";
static const char kChunkProperty[] = "PROP";
static const char kChunkParents[] = "PRNT";
static const char kChunkMetadata[] = "META";
static const char kChunkSharedStrings[] = "SSTR";
static const char kChunkSignatures[] = "SIGN";
static const char kChunkHash[] = "HASH";
static const char kChunkEnd[] = "END\0";

struct FileHeader
{
    char magic[8];
    char signature[6];
    uint16_t version;

    uint32_t types;
    uint32_t objects;
    uint32_t reserved[2];
};

struct ChunkHeader
{
    char name[4];
    // if compressedSize is 0, chunk data is not compressed
    uint32_t compressedSize; 
    uint32_t size;
    uint32_t reserved;
};

enum BinaryObjectFormat
{
    bofPlain,
    bofServiceType
};

enum BinaryParentLinkFormat
{
    bplfPlain,
};

class BinaryBlob
{
  public:
    BinaryBlob()
        : offset(0)
    {
    }

    void initFromFile(const char* filename)
    {
        offset = 0;

        FILE* file = fopen(filename, "rb");
        if (!file)
        {
            throw std::runtime_error("Failed to open file");
        }

        if (fseek(file, 0, SEEK_END) != 0)
        {
            fclose(file);
            throw std::runtime_error("Failed to seek to end of file");
        }

        long fileSize = ftell(file);
        if (fileSize < 0)
        {
            fclose(file);
            throw std::runtime_error("Failed to determine file size");
        }
        ::rewind(file);
        buffer.resize(static_cast<size_t>(fileSize));
        size_t readBytes = fread(buffer.data(), 1, buffer.size(), file);
        fclose(file);
        if (readBytes != buffer.size())
        {
            buffer.clear();
            throw std::runtime_error("Failed to read entire file");
        }
    }

    void initFromBlob(BinaryBlob& other, size_t size)
    {
        buffer.resize(size);
        other.read(buffer.data(), size);
        offset = 0;
    }

    size_t initFromCompressed(const char* compressedBytes, size_t compressedSize, size_t size)
    {
        buffer.resize(size);
        size_t decompressedSize = 0;
        if (compressedSize > 4 && memcmp(compressedBytes, kZStdFrameHeader, 4) == 0)
        {
            decompressedSize = ZSTD_decompress(buffer.data(), size, compressedBytes, compressedSize);
        }
        else
        {
            decompressedSize = LZ4_decompress_safe(compressedBytes, reinterpret_cast<char*>(buffer.data()), int(compressedSize), int(size));
        }
        offset = 0;
        return decompressedSize;
    }

    void read(void* dest, size_t bytesToRead)
    {
        if (offset + bytesToRead > buffer.size())
        {
            throw std::runtime_error("Attempt to read beyond available data");
        }
        std::memcpy(dest, buffer.data() + offset, bytesToRead);
        offset += bytesToRead;
    }

    template <typename T> void read(T& result)
    {
        if (offset + sizeof(T) > buffer.size())
        {
            throw std::runtime_error("Not enough data to read object");
        }
        std::memcpy(&result, buffer.data() + offset, sizeof(T));
        offset += sizeof(T);
    }

    size_t size() const { return buffer.size(); }
    size_t tell() const { return offset; }

    uint8_t at(size_t offset) const { return buffer[offset]; }

    void skip(size_t numBytes) { offset += numBytes; }

  private:
    std::vector<uint8_t> buffer;
    size_t offset;
};

enum class NormalId
{
    Right = 0,
    Top = 1,
    Back = 2,

    Left = 3,
    Bottom = 4,
    Front = 5
};

static Vec3 normalIdToVector3(NormalId normalId)
{
    float coords[] = {0.0f, 0.0f, 0.0f};
    int index = (int)(normalId);
    coords[index % 3] = ((normalId >= NormalId::Left) ? -1.0f : 1.0f);
    return Vec3{coords[0], coords[1], coords[2]};
}

static Vec3 vec3_cross(const Vec3& a, const Vec3& b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

static void idToMatrix3(int orientId, Mat3x3& matrix)
{
    NormalId xNormal = (NormalId)(orientId / 6);
    NormalId yNormal = (NormalId)(orientId % 6);
    Vec3 r0 = normalIdToVector3(xNormal);
    Vec3 r1 = normalIdToVector3(yNormal);
    Vec3 r2 = vec3_cross(r0, r1);
    matrix = Mat3x3{r0.x, r0.y, r0.z, r1.x, r1.y, r1.z, r2.x, r2.y, r2.z};
}

static void readExactRotation(BinaryBlob& blob, Mat3x3& res)
{
    char orientId;
    blob.read(orientId);
    if (orientId)
    {
        idToMatrix3(orientId - 1, res);
    }
    else
    {
        blob.read(&res.v[0], sizeof(float) * 9);
    }
}

static void readString(BinaryBlob& blob, std::string& res)
{
    uint32_t length;
    blob.read(length);
    res.resize(length);
    blob.read(&res[0], length);
}

union FloatBitcast
{
    float f;
    unsigned int i;
};

static float decodeFloat(unsigned int value)
{
    FloatBitcast bitcast;
    bitcast.i = (value >> 1) | (value << 31);
    return bitcast.f;
}

static int decodeInt(int32_t value) { return (static_cast<uint32_t>(value) >> 1) ^ (-(value & 1)); }
static int64_t decodeInt64(int64_t value) { return (static_cast<uint64_t>(value) >> 1) ^ (-(value & 1)); }

static void readIntVector(BinaryBlob& blob, std::vector<int32_t>& values, size_t count)
{
    values.clear();
    values.reserve(count);

    if (blob.tell() + count * 4 > blob.size())
    {

        throw std::runtime_error("readIntVector offset is out of bounds");
    }

    size_t startOffset = blob.tell();
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t v0;
        uint8_t v1;
        uint8_t v2;
        uint8_t v3;
        v0 = blob.at(startOffset + count * 0 + i);
        v1 = blob.at(startOffset + count * 1 + i);
        v2 = blob.at(startOffset + count * 2 + i);
        v3 = blob.at(startOffset + count * 3 + i);
        values.push_back(decodeInt((v0 << 24) | (v1 << 16) | (v2 << 8) | v3));
    }

    blob.skip(count * 4);
}

static void readUIntVector(BinaryBlob& blob, std::vector<uint32_t>& values, size_t count)
{
    values.clear();
    values.reserve(count);

    if (blob.tell() + count * 4 > blob.size())
    {
        throw std::runtime_error("readUIntVector offset is out of bounds");
    }

    size_t startOffset = blob.tell();
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t v0 = blob.at(startOffset + count * 0 + i);
        uint8_t v1 = blob.at(startOffset + count * 1 + i);
        uint8_t v2 = blob.at(startOffset + count * 2 + i);
        uint8_t v3 = blob.at(startOffset + count * 3 + i);
        values.push_back((v0 << 24) | (v1 << 16) | (v2 << 8) | v3);
    }

    blob.skip(count * 4);
}

static void readInt64Vector(BinaryBlob& blob, std::vector<int64_t>& values, size_t count)
{
    values.clear();
    values.reserve(count);

    if (blob.tell() + count * 8 > blob.size())
    {
        throw std::runtime_error("readInt64Vector offset is out of bounds");
    }

    size_t startOffset = blob.tell();
    for (size_t i = 0; i < count; ++i)
    {
        uint64_t v0 = blob.at(startOffset + count * 0 + i);
        uint64_t v1 = blob.at(startOffset + count * 0 + i);
        uint64_t v2 = blob.at(startOffset + count * 0 + i);
        uint64_t v3 = blob.at(startOffset + count * 0 + i);
        uint64_t v4 = blob.at(startOffset + count * 0 + i);
        uint64_t v5 = blob.at(startOffset + count * 0 + i);
        uint64_t v6 = blob.at(startOffset + count * 0 + i);
        uint64_t v7 = blob.at(startOffset + count * 0 + i);
        values.push_back(decodeInt64((v0 << 56) | (v1 << 48) | (v2 << 40) | (v3 << 32) | (v4 << 24) | (v5 << 16) | (v6 << 8) | v7));
    }

    blob.skip(count * 8);
}

static void readFloatVector(BinaryBlob& blob, std::vector<float>& values, size_t count)
{
    values.clear();
    values.reserve(count);

    if (blob.tell() + count * 4 > blob.size())
    {
        throw std::runtime_error("readFloatVector offset is out of bounds");
    }

    size_t startOffset = blob.tell();
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t v0 = blob.at(startOffset + count * 0 + i);
        uint8_t v1 = blob.at(startOffset + count * 1 + i);
        uint8_t v2 = blob.at(startOffset + count * 2 + i);
        uint8_t v3 = blob.at(startOffset + count * 3 + i);
        values.push_back(decodeFloat((v0 << 24) | (v1 << 16) | (v2 << 8) | v3));
    }

    blob.skip(count * 4);
}

static void readUInt8Vector(BinaryBlob& blob, std::vector<uint8_t>& values, size_t count)
{
    values.clear();

    if (blob.tell() + count > blob.size())
    {
        throw std::runtime_error("readUInt8Vector offset is out of bounds");
    }

    values.resize(count);
    blob.read(values.data(), count);
}

static void readIdVector(BinaryBlob& blob, std::vector<int>& values, size_t count)
{
    readIntVector(blob, values, count);

    int last = 0;
    for (size_t i = 0; i < count; ++i)
    {
        values[i] += last;
        last = values[i];
    }
}

static void readChunkData(const ChunkHeader& chunk, BinaryBlob& blob, BinaryBlob& bytes)
{
    if (chunk.size == 0)
    {
        return;
    }

    if (chunk.compressedSize == 0)
    {
        bytes.initFromBlob(blob, chunk.size);
    }
    else
    {
        std::vector<char> compressed;
        compressed.resize(chunk.compressedSize);
        blob.read(compressed.data(), chunk.compressedSize);
        size_t decompressed = bytes.initFromCompressed(compressed.data(), compressed.size(), chunk.size);
        if (decompressed != bytes.size())
        {
            throw std::runtime_error("Malformed data");
        }
    }
}

void BinaryReader::readInstances(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc)
{
    uint32_t typeIndex;
    blob.read(typeIndex);

    std::string typeName;
    readString(blob, typeName);

    char format;
    blob.read(format);

    if (format != bofPlain && format != bofServiceType)
    {
        throw std::runtime_error("Unrecognized object format");
    }

    uint32_t idCount;
    blob.read(idCount);

    std::vector<int32_t> ids;
    readIdVector(blob, ids, idCount);
    size_t numInstances = ids.size();

    bool isServiceType = (format == bofServiceType);
    std::vector<bool> isServiceRootedArray;
    if (isServiceType)
    {
        isServiceRootedArray.resize(numInstances);

        for (size_t i = 0; i < numInstances; ++i)
        {
            char value;
            blob.read(value);
            isServiceRootedArray[i] = value;
        }
    }

    if (typeIndex >= doc.types.size())
    {
        throw std::runtime_error("Incorrect type index");
    }
    doc.types[typeIndex] = Type{std::move(typeName)};
    // note: do not use typeName it was moved!

    for (size_t i = 0; i < numInstances; ++i)
    {
        int instanceId = ids[i];
        bool isServiceRooted = isServiceType ? isServiceRootedArray[i] : false;

        if (instanceId >= doc.instances.size())
        {
            throw std::runtime_error("Incorrect instance index");
        }
        doc.instances[instanceId] = Instance{-1, instanceId, typeIndex, isServiceType, isServiceRooted};
    }
}

void BinaryReader::readStringProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::string tmp;
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::String});
        Property& prop = inst.properties.back();
        readString(blob, tmp);
        prop.data = tmp;
    }
}

void BinaryReader::readEnumProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<uint32_t> values;
    readUIntVector(blob, values, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Enum});
        Property& prop = inst.properties.back();
        prop.data = values[i];
    }
}

void BinaryReader::readBoolProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Bool});
        Property& prop = inst.properties.back();
        char tmp;
        blob.read(tmp);
        bool val = (tmp != 0);
        prop.data = val;
    }
}
void BinaryReader::readInt32Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<int32_t> values;
    readIntVector(blob, values, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Int32});
        Property& prop = inst.properties.back();
        prop.data = values[i];
    }
}

void BinaryReader::readInt64Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<int64_t> values;
    readInt64Vector(blob, values, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Int64});
        Property& prop = inst.properties.back();
        prop.data = values[i];
    }
}

void BinaryReader::readFloatProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<float> values;
    readFloatVector(blob, values, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Float});
        Property& prop = inst.properties.back();
        prop.data = values[i];
    }
}

void BinaryReader::readDoubleProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        double val;
        blob.read(val);

        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Double});
        Property& prop = inst.properties.back();
        prop.data = val;
    }
}

void BinaryReader::readRect2DProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<float> x0;
    std::vector<float> y0;
    std::vector<float> x1;
    std::vector<float> y1;
    readFloatVector(blob, x0, typeInstances.size());
    readFloatVector(blob, y0, typeInstances.size());
    readFloatVector(blob, x1, typeInstances.size());
    readFloatVector(blob, y1, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Rect2D});
        Property& prop = inst.properties.back();
        prop.data = Rect2D{x0[i], y0[i], x1[i], y1[i]};
    }
}

void BinaryReader::readUdim2Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<float> sx;
    std::vector<float> sy;
    readFloatVector(blob, sx, typeInstances.size());
    readFloatVector(blob, sy, typeInstances.size());

    std::vector<int32_t> ox;
    std::vector<int32_t> oy;
    readIntVector(blob, ox, typeInstances.size());
    readIntVector(blob, oy, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::UDim2});
        Property& prop = inst.properties.back();
        prop.data = UDim2{sx[i], sy[i], ox[i], oy[i]};
    }
}

void BinaryReader::readVector3Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<float> x;
    std::vector<float> y;
    std::vector<float> z;
    readFloatVector(blob, x, typeInstances.size());
    readFloatVector(blob, y, typeInstances.size());
    readFloatVector(blob, z, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Vector3});
        Property& prop = inst.properties.back();
        prop.data = Vec3{x[i], y[i], z[i]};
    }
}

void readUColor3Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
void BinaryReader::readUColor3Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<uint8_t> r;
    std::vector<uint8_t> g;
    std::vector<uint8_t> b;
    readUInt8Vector(blob, r, typeInstances.size());
    readUInt8Vector(blob, g, typeInstances.size());
    readUInt8Vector(blob, b, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::UColor3});
        Property& prop = inst.properties.back();
        prop.data = Color3{r[i] / 255.0f, g[i] / 255.0f, b[i] / 255.0f};
    }
}

void BinaryReader::readColor3Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<float> r;
    std::vector<float> g;
    std::vector<float> b;
    readFloatVector(blob, r, typeInstances.size());
    readFloatVector(blob, g, typeInstances.size());
    readFloatVector(blob, b, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Color3});
        Property& prop = inst.properties.back();
        prop.data = Color3{r[i], g[i], b[i]};
    }
}

void BinaryReader::readVector2Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<float> x;
    std::vector<float> y;
    readFloatVector(blob, x, typeInstances.size());
    readFloatVector(blob, y, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Vector2});
        Property& prop = inst.properties.back();
        prop.data = Vec2{x[i], y[i]};
    }
}

void BinaryReader::readFontProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::string family;
    uint16_t weight;
    uint8_t style;
    std::string cachedFaceId;

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Font});
        Property& prop = inst.properties.back();
        

        readString(blob, family);
        blob.read(weight);
        blob.read(style);
        readString(blob, cachedFaceId);

        prop.data = FontInfo{family, weight, style, cachedFaceId};
    }
}

void BinaryReader::readRefProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<int32_t> values;
    readIdVector(blob, values, typeInstances.size());
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Ref});
        Property& prop = inst.properties.back();
        prop.data = values[i];
    }
}

void BinaryReader::readBrickColorProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<uint32_t> values;
    readUIntVector(blob, values, typeInstances.size());
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::BrickColor});
        Property& prop = inst.properties.back();
        prop.data = BrickColor{values[i]};
    }
}

void BinaryReader::readUniqueIdProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<uint32_t> indices;
    std::vector<uint32_t> timestamps;
    std::vector<int64_t> rawbits;

    readUIntVector(blob, indices, typeInstances.size());
    readUIntVector(blob, timestamps, typeInstances.size());
    readInt64Vector(blob, rawbits, typeInstances.size());

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::UniqueId});
        Property& prop = inst.properties.back();
        prop.data = UniqueId{indices[i], timestamps[i], rawbits[i]};
    }
}

void BinaryReader::readNumberRangeProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    NumberRange tmp;
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::NumberRange});
        Property& prop = inst.properties.back();
        blob.read(tmp);
        prop.data = tmp;
    }
}

void BinaryReader::readPhysicalProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    static constexpr uint8_t kCustomizeMask = 0x01;

    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        uint8_t flag;
        blob.read(flag);

        bool customizeProp = flag & kCustomizeMask;
        bool hasAcousticAbsorption = flag >= 2;

        float density = 0.0f;
        float friction = 0.0f;
        float elasticity = 0.0f;
        float frictionWeight = 1.0f;
        float elasticityWeight = 1.0f;
        float acousticAbsorption = 1.0f;
        if (customizeProp)
        {
            blob.read(density);
            blob.read(friction);
            blob.read(elasticity);
            blob.read(frictionWeight);
            blob.read(elasticityWeight);
            if (hasAcousticAbsorption)
            {
                blob.read(acousticAbsorption);
            }
        }

        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::PhysicalProperties});
        Property& prop = inst.properties.back();
        prop.data = PhysicalProperties{density, friction, elasticity, frictionWeight, elasticityWeight, acousticAbsorption};
    }
}

void BinaryReader::readSharedStringProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<uint32_t> indices;
    readUIntVector(blob, indices, typeInstances.size());
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::SharedString});
        Property& prop = inst.properties.back();
        prop.data = std::string("TODO"); // copy from shared string dictionary
    }
}

void BinaryReader::readOptionalCFrameProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    size_t numInstances = typeInstances.size();
    std::vector<Mat3x3> rot(numInstances);
    std::vector<float> tx;
    std::vector<float> ty;
    std::vector<float> tz;

    char fmtCf;
    blob.read(fmtCf);
    if (PropertyType(fmtCf) == PropertyType::CFrameMatrix)
    {
        for (size_t i = 0; i < numInstances; i++)
        {
            readExactRotation(blob, rot[i]);
        }
        readFloatVector(blob, tx, numInstances);
        readFloatVector(blob, ty, numInstances);
        readFloatVector(blob, tz, numInstances);
    }
    else
    {
        throw std::runtime_error("Unsupported OptionalCFrame format");
    }

    char fmtBl;
    blob.read(fmtBl);
    if (PropertyType(fmtBl) != PropertyType::Bool)
    {
        throw std::runtime_error("Unsupported OptionalCFrame format");
    }

    for (size_t i = 0; i < numInstances; i++)
    {
        char val;
        blob.read(val);

        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::OptionalCFrame});
        Property& prop = inst.properties.back();

        bool hasData = (val != 0);
        prop.data = OptionalCFrame{CFrame{rot[i], Vec3{tx[i], ty[i], tz[i]}}, hasData};
    }
}

void BinaryReader::readCFrameProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    size_t numInstances = typeInstances.size();
    std::vector<Mat3x3> rot(numInstances);
    std::vector<float> tx;
    std::vector<float> ty;
    std::vector<float> tz;

    for (size_t i = 0; i < numInstances; i++)
    {
        readExactRotation(blob, rot[i]);
    }
    readFloatVector(blob, tx, numInstances);
    readFloatVector(blob, ty, numInstances);
    readFloatVector(blob, tz, numInstances);

    for (size_t i = 0; i < numInstances; i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::CFrameMatrix});
        Property& prop = inst.properties.back();
        prop.data = CFrame{rot[i], Vec3{tx[i], ty[i], tz[i]}};
    }
}

void BinaryReader::readNumberSequenceProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    NumberSeq sq;
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        uint32_t size;
        blob.read(size);
        sq.data.resize(size);
        blob.read(&sq.data[0], sizeof(NumberSeq::KeyValue) * size);

        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::NumberSequence});
        Property& prop = inst.properties.back();
        prop.data = sq;
    }
}

void BinaryReader::readColorSequenceProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    ColorSeq sq;
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        uint32_t size;
        blob.read(size);
        sq.data.resize(size);
        blob.read(&sq.data[0], sizeof(ColorSeq::KeyValue) * size);

        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::ColorSequenceV1});
        Property& prop = inst.properties.back();
        prop.data = sq;
    }
}

void BinaryReader::createEmptyProperties(const std::string& name, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[typeInstances[i]];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Unknown});
        Property& prop = inst.properties.back();
    }
}

void BinaryReader::readProperties(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc)
{
    //
    uint32_t typeIndex;
    blob.read(typeIndex);

    std::string propertyName;
    readString(blob, propertyName);

    char propFormat;
    blob.read(propFormat);

    PropertyType propertyType = PropertyType(propFormat);

    Type& type = doc.types[typeIndex];

    std::vector<uint32_t> typeInstances;
    for (size_t i = 0; i < doc.instances.size(); i++)
    {
        Instance& inst = doc.instances[i];
        if (inst.typeIndex != typeIndex)
        {
            continue;
        }
        typeInstances.emplace_back(uint32_t(i));
    }

    switch (propertyType)
    {
    case PropertyType::String:
        readStringProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Bool:
        readBoolProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Int32:
        readInt32Properties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Int64:
        readInt64Properties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Float:
        readFloatProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Double:
        readDoubleProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Color3:
        readColor3Properties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::UColor3:
        readUColor3Properties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Vector3:
        readVector3Properties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Vector2:
        readVector2Properties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Enum:
        readEnumProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Ref:
        readRefProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::BrickColor:
        readBrickColorProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::UniqueId:
        readUniqueIdProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::CFrameMatrix:
        readCFrameProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::OptionalCFrame:
        readOptionalCFrameProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::ColorSequenceV1:
        readColorSequenceProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::NumberSequence:
        readNumberSequenceProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::UDim2:
        readUdim2Properties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Rect2D:
        readRect2DProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::SharedString:
        readSharedStringProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::PhysicalProperties:
        readPhysicalProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::NumberRange:
        readNumberRangeProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Font:
        readFontProperties(propertyName, blob, doc, typeInstances);
        break;
    default:
        createEmptyProperties(propertyName, doc, typeInstances);
        break;
    }
}

void BinaryReader::readParentsChunk(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc)
{
    char format;
    blob.read(format);

    if (format != bplfPlain)
    {
        throw std::runtime_error("Unrecognized parent link format");
    }

    uint32_t linkCount = 0;
    blob.read(linkCount);

    std::vector<int32_t> childIds;
    readIdVector(blob, childIds, linkCount);

    std::vector<int32_t> parentIds;
    readIdVector(blob, parentIds, linkCount);

    for (uint32_t i = 0; i < linkCount; i++)
    {
        int32_t childId = childIds[i];
        int32_t parentId = parentIds[i];

        if (childId >= doc.instances.size())
        {
            throw std::runtime_error("Invalid child index");
        }

        Instance& child = doc.instances[childId];
        child.parentId = (parentId >= 0) ? parentId : -1;

        if (parentId >= 0)
        {
            if (parentId >= doc.instances.size())
            {
                throw std::runtime_error("Invalid parent index");
            }
            Instance& parent = doc.instances[parentId];
            parent.childIds.emplace_back(childId);
        }
    }
    return;
}

LoadResult BinaryReader::loadBinary(const char* fileName, Document& doc)
{
    //
    BinaryBlob chunkBlob;

    BinaryBlob fileBlob;
    fileBlob.initFromFile(fileName);

    FileHeader header = {};
    fileBlob.read(header);

    if (memcmp(header.magic, kMagicHeader, sizeof(header.magic)) != 0)
    {
        throw std::runtime_error("Unrecognized format");
    }

    if (memcmp(header.signature, kHeaderSignature, sizeof(header.signature)) != 0)
    {
        throw std::runtime_error("The file header is corrupted, unexpected signature");
    }

    if (header.version != 0)
    {
        throw std::runtime_error("Unrecognized version");
    }

    doc.instances.clear();
    doc.instances.resize(header.objects);

    doc.types.clear();
    doc.types.resize(header.types);

    while (fileBlob.tell() < fileBlob.size())
    {
        ChunkHeader chunk = {};
        fileBlob.read(chunk);
        readChunkData(chunk, fileBlob, chunkBlob);

        if (memcmp(chunk.name, kChunkInstances, sizeof(chunk.name)) == 0)
        {
            readInstances(chunk, chunkBlob, doc);
        }
        else if (memcmp(chunk.name, kChunkHash, sizeof(chunk.name)) == 0)
        {
        }
        else if (memcmp(chunk.name, kChunkProperty, sizeof(chunk.name)) == 0)
        {
            readProperties(chunk, chunkBlob, doc);
        }
        else if (memcmp(chunk.name, kChunkParents, sizeof(chunk.name)) == 0)
        {
            readParentsChunk(chunk, chunkBlob, doc);
        }
        else if (memcmp(chunk.name, kChunkMetadata, sizeof(chunk.name)) == 0)
        {
        }
        else if (memcmp(chunk.name, kChunkSharedStrings, sizeof(chunk.name)) == 0)
        {
        }
        else if (memcmp(chunk.name, kChunkSignatures, sizeof(chunk.name)) == 0)
        {
        }
        else if (memcmp(chunk.name, kChunkEnd, sizeof(chunk.name)) == 0)
        {
            // we done here
            break;
        }
    }

    return LoadResult::OK;
}

} // namespace rbxdoc
