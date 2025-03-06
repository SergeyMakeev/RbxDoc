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
    uint32_t compressedSize; // if compressedSize is 0, chunk data is not compressed
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

static void readString(BinaryBlob& blob, std::string& res)
{
    uint32_t length;
    blob.read(length);
    res.resize(length);
    blob.read(&res[0], length);
}

static int decodeInt(int32_t value) { return (static_cast<uint32_t>(value) >> 1) ^ (-(value & 1)); }

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
        uint8_t v0;
        uint8_t v1;
        uint8_t v2;
        uint8_t v3;
        v0 = blob.at(startOffset + count * 0 + i);
        v1 = blob.at(startOffset + count * 1 + i);
        v2 = blob.at(startOffset + count * 2 + i);
        v3 = blob.at(startOffset + count * 3 + i);
        values.push_back((v0 << 24) | (v1 << 16) | (v2 << 8) | v3);
    }

    blob.skip(count * 4);
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
        printf("  -> %d\n", instanceId);
        bool isServiceRooted = isServiceType ? isServiceRootedArray[i] : false;
        if (instanceId >= doc.instances.size())
        {
            int a = 0;
        }

        if (instanceId >= doc.instances.size())
        {
            throw std::runtime_error("Incorrect instance index");
        }
        doc.instances[instanceId] = Instance{-1, instanceId, typeIndex, isServiceType, isServiceRooted};
    }
}

void BinaryReader::readStringProperties(const std::string& name, BinaryBlob& blob, Document& doc,
                                        const std::vector<uint32_t>& typeInstances)
{
    std::string tmp;
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[i];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Enum});
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
        Instance& inst = doc.instances[i];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Enum});
        Property& prop = inst.properties.back();
        prop.data = values[i];
    }
}

void BinaryReader::readBoolProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[i];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Enum});
        Property& prop = inst.properties.back();
        char tmp;
        blob.read(tmp);
        bool val = (tmp != 0);
        prop.data = val;
    }
}
void BinaryReader::readIntProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    std::vector<int32_t> values;
    readIntVector(blob, values, typeInstances.size());
    
    for (size_t i = 0; i < typeInstances.size(); i++)
    {
        Instance& inst = doc.instances[i];
        inst.properties.push_back(Property{name.c_str(), PropertyType::Enum});
        Property& prop = inst.properties.back();
        prop.data = values[i];
    }
}

void BinaryReader::readFloatProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances)
{
    //
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

    // Note: this is constant across all instances
    switch (propertyType)
    {
    case PropertyType::String:
        readStringProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Bool:
        readBoolProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Int:
        readIntProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Float:
        readFloatProperties(propertyName, blob, doc, typeInstances);
        break;
    case PropertyType::Enum:
        readEnumProperties(propertyName, blob, doc, typeInstances);
        break;
    default:
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
            printf("kChunkEnd\n");
            break;
        }
        else
        {
            //  Unknown chunk, skip
            printf("unknown\n");
        }
    }

    return LoadResult::OK;
}

} // namespace rbxdoc
