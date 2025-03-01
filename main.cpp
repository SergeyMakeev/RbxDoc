#define _CRT_SECURE_NO_WARNINGS
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <lz4.h>
#include <stdexcept>
#include <vector>
#include <zstd.h>

// zstd frame header (https://datatracker.ietf.org/doc/rfc8878/, 3.1.1)
static const char kZStd[] = "\x28\xb5\x2f\xfd";

class BinaryBlob
{
  public:
    // Default constructor: no file is loaded.
    BinaryBlob()
        : offset(0)
    {
    }

    // Explicit initialization function: opens the file, loads its entire contents
    // into the vector, and closes the file.
    void initFromFile(const char* filename)
    {
        offset = 0;
        // Open the file in binary mode.
        FILE* file = fopen(filename, "rb");
        if (!file)
        {
            throw std::runtime_error("Failed to open file");
        }

        // Determine the file size.
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

        // Resize the vector to hold the entire file.
        buffer.resize(static_cast<size_t>(fileSize));

        // Read the entire file into the vector.
        size_t readBytes = fread(buffer.data(), 1, buffer.size(), file);
        fclose(file);
        if (readBytes != buffer.size())
        {
            buffer.clear();
            throw std::runtime_error("Failed to read entire file");
        }
    }

    void initFromMemory(std::vector<uint8_t>&& memory)
    {
        buffer = std::move(memory);
        offset = 0;
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
        if (compressedSize > 4 && memcmp(compressedBytes, kZStd, 4) == 0)
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

    // Uses memcpy to copy the data and advances the internal offset.
    void read(void* dest, size_t bytesToRead)
    {
        if (offset + bytesToRead > buffer.size())
        {
            throw std::runtime_error("Attempt to read beyond available data");
        }
        std::memcpy(dest, buffer.data() + offset, bytesToRead);
        offset += bytesToRead;
    }

    // Template method to read an object of type T from the vector.
    template <typename T> void read(T& result)
    {
        if (offset + sizeof(T) > buffer.size())
        {
            throw std::runtime_error("Not enough data to read object");
        }
        std::memcpy(&result, buffer.data() + offset, sizeof(T));
        offset += sizeof(T);
    }

    // Resets the reading offset to the beginning of the vector.
    void rewind() { offset = 0; }

    // Returns whether the file data has been successfully loaded.
    bool isLoaded() const { return !buffer.empty(); }

    // Returns the total size of the loaded file.
    size_t size() const { return buffer.size(); }

    // Returns the current reading offset.
    size_t tell() const { return offset; }

    void skip(unsigned int numBytes) { offset += size_t(numBytes); }

  private:
    std::vector<uint8_t> buffer; // In-memory storage for the file data.
    size_t offset;               // Current read offset within the vector.
};

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

/*
    Format description:

    File is structured as follows - header (FileHeader), followed by one or more chunks, followed by footer (kMagicSuffix)
    Each chunk has header (ChunkHeader) and a stream of optionally lz4-compressed data.
    The chunk has a name; currently, the following chunk names are supported:

    INST chunk declares instance ids for a specific type.
    It contains the type index (an integer), the type name and a list of object ids of that type.
    Object ids are arbitrary unique integers; they are currently generated via depth-first pre-order traversal.
    There is some additional information for service types; read the source for details.

    PROP chunk specifies property values for all objects of a specific type.
    Each chunk has the type index, a property name, followed by the storage format (see BinaryPropertyFormat), followed by a stream of
    property data. Property data is encoded depending on the type, with interleaving/bit shuffling to improve LZ compression rates.

    PRNT chunk specifies parent-child relationship.
    It contains two lists of ids, where the first id is the id of the object, and the second if is the id of the parent.
    The parent-child list is currently stored in depth-first post-order traversal - this improves the time it takes to do setParent() over
    depth-first pre-order.

    META chunk includes file-level metadata.
    If present it must be the first chunk in the file. It contains a number of key/value string pairs.

    SSTR chunk

    SIGN chunk includes a list of signatures. The entire file should be signed with the exception of the contents of the SIGN chunk.
    If present, each signature has a type, public key id, and the signature itself

    HASH chunk is an simple array of hash data that's utilized by ModuleScript shared strings if the Lua code is provided as bytecode.
    This is targeted an opimization for LuaApp patch bundler as the number of files included in the RBXM is very large.
*/

struct FileHeader
{
    char magic[8];
    char signature[6];
    unsigned short version;

    unsigned int types;
    unsigned int objects;
    unsigned int reserved[2];
};

struct ChunkHeader
{
    char name[4];
    unsigned int compressedSize; // if compressedSize is 0, chunk data is not compressed
    unsigned int size;
    unsigned int reserved;
};

enum BinaryObjectFormat
{
    bofPlain,
    bofServiceType
};

enum BinaryPropertyFormat
{
    bpfUnknown = 0,
    bpfString,
    bpfBool,
    bpfInt,
    bpfFloat,
    bpfDouble,
    bpfUDim,
    bpfUDim2,
    bpfRay,
    bpfFaces,
    bpfAxes,
    bpfBrickColor,
    bpfColor3,
    bpfVector2,
    bpfVector3,
    bpfVector2int16,
    bpfCFrameMatrix,
    bpfCFrameQuat,
    bpfEnum,
    bpfRef,
    bpfVector3int16,
    bpfNumberSequence,
    bpfColorSequenceV1,
    bpfNumberRange,
    bpfRect2D,
    bpfPhysicalProperties,
    bpfColor3uint8,
    bpfInt64,
    bpfSharedStringDictionaryIndex,
    bpfBytecode,
    bpfOptionalCFrame,
    bpfUniqueId,
    bpfFont,
    bpfSecurityCapabilities,
    bpfContent,
};

const char* toString(BinaryPropertyFormat v)
{
    switch (v)
    {
    case bpfUnknown:
        return "bpfUnknown";
    case bpfString:
        return "bpfString";
    case bpfBool:
        return "bpfBool";
    case bpfInt:
        return "bpfInt";
    case bpfFloat:
        return "bpfFloat";
    case bpfDouble:
        return "bpfDouble";
    case bpfUDim:
        return "bpfUDim";
    case bpfUDim2:
        return "bpfUDim2";
    case bpfRay:
        return "bpfRay";
    case bpfFaces:
        return "bpfFaces";
    case bpfAxes:
        return "bpfAxes";
    case bpfBrickColor:
        return "bpfBrickColor";
    case bpfColor3:
        return "bpfColor3";
    case bpfVector2:
        return "bpfVector2";
    case bpfVector3:
        return "bpfVector3";
    case bpfVector2int16:
        return "bpfVector2int16";
    case bpfCFrameMatrix:
        return "bpfCFrameMatrix";
    case bpfCFrameQuat:
        return "bpfCFrameQuat";
    case bpfEnum:
        return "bpfEnum";
    case bpfRef:
        return "bpfRef";
    case bpfVector3int16:
        return "bpfVector3int16";
    case bpfNumberSequence:
        return "bpfNumberSequence";
    case bpfColorSequenceV1:
        return "bpfColorSequenceV1";
    case bpfNumberRange:
        return "bpfNumberRange";
    case bpfRect2D:
        return "bpfRect2D";
    case bpfPhysicalProperties:
        return "bpfPhysicalProperties";
    case bpfColor3uint8:
        return "bpfColor3uint8";
    case bpfInt64:
        return "bpfInt64";
    case bpfSharedStringDictionaryIndex:
        return "bpfSharedStringDictionaryIndex";
    case bpfBytecode:
        return "bpfBytecode";
    case bpfOptionalCFrame:
        return "bpfOptionalCFrame";
    case bpfUniqueId:
        return "bpfUniqueId";
    case bpfFont:
        return "bpfFont";
    case bpfSecurityCapabilities:
        return "bpfSecurityCapabilities";
    case bpfContent:
        return "bpfContent";
    default:
        return "Unknown BinaryPropertyFormat";
    }
}


void readChunkData(const ChunkHeader& chunk, BinaryBlob& blob, BinaryBlob& bytes)
{
    static std::vector<char> compressed;

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
        compressed.clear();
        compressed.resize(chunk.compressedSize);
        blob.read(compressed.data(), chunk.compressedSize);
        size_t decompressed = bytes.initFromCompressed(compressed.data(), compressed.size(), chunk.size);
        if (decompressed != bytes.size())
        {
            throw std::runtime_error("Malformed data");
        }
    }
}

void readString(BinaryBlob& blob, std::string& res)
{
    uint32_t length;
    blob.read(length);
    res.resize(length);
    blob.read(&res[0], length);
}

int decodeInt(int value) { return (static_cast<unsigned int>(value) >> 1) ^ (-(value & 1)); }

void readIntVector(BinaryBlob& blob, std::vector<int>& values, size_t count)
{
    values.clear();
    values.reserve(count);

    
    if (blob.tell() + count * 4 > blob.size())
    {

        throw std::runtime_error("readIntVector offset is out of bounds");
    }

    for (size_t i = 0; i < count; ++i)
    {
        unsigned char v0;
        unsigned char v1;
        unsigned char v2;
        unsigned char v3;
        blob.read(v0);
        blob.read(v1);
        blob.read(v2);
        blob.read(v3);
        values.push_back(decodeInt((v0 << 24) | (v1 << 16) | (v2 << 8) | v3));
    }
}


void readIdVector(BinaryBlob& blob, std::vector<int>& values, size_t count)
{
    readIntVector(blob, values, count);

    int last = 0;
    for (size_t i = 0; i < count; ++i)
    {
        values[i] += last;
        last = values[i];
    }
}


void readMetadata(const ChunkHeader& chunk, BinaryBlob& blob)
{
    if (chunk.reserved != 0)
    {
        throw std::runtime_error("Unrecognized metadata chunk version");
    }

    uint32_t length;
    blob.read(length);

    std::string name;
    std::string value;
    for (uint32_t i = 0; i < length; ++i)
    {
        readString(blob, name);
        readString(blob, value);

        printf("name = '%s'\n", name.c_str());
        printf("value = '%s'\n", value.c_str());
    }
}

void readSharedStrings(const ChunkHeader& chunk, BinaryBlob& blob)
{
    uint32_t version = 0;
    blob.read(version);
    if (version != 0)
    {
        throw std::runtime_error("Unknown shared string dictionary version");
    }

    uint32_t count = 0;
    blob.read(count);
    char md5Buffer[16];
    std::string value;
    for (uint32_t i = 0; i < count; ++i)
    {
        blob.read(md5Buffer, 16);
        readString(blob, value);
        printf("name = '%s'\n", value.c_str());
    }
}

void readProperty(const ChunkHeader& chunk, BinaryBlob& blob)
{
//
    unsigned int typeIndex;
    blob.read(typeIndex);

    std::string propertyName;
    readString(blob, propertyName);

    char format;
    blob.read(format);
    printf("Type index %d, prop name '%s', prop format = %s\n", typeIndex, propertyName.c_str(), toString(BinaryPropertyFormat(format)));
}

void readInstances(const ChunkHeader& chunk, BinaryBlob& blob)
{
    unsigned int typeIndex;
    blob.read(typeIndex);

    std::string typeName;
    readString(blob, typeName);
    printf("Type index %d, type name '%s'\n", typeIndex, typeName.c_str());

    char format;
    blob.read(format);

    if (format != bofPlain && format != bofServiceType)
    {
        throw std::runtime_error("Unrecognized object format");
    }

    unsigned int idCount;
    blob.read(idCount);

    std::vector<int> ids;
    readIdVector(blob, ids, idCount);

    bool isServiceType = (format == bofServiceType);
    std::vector<bool> isServiceRooted;
    if (isServiceType)
    {
        isServiceRooted.reserve(ids.size());

        for (size_t i = 0; i < ids.size(); ++i)
        {
            bool value;
            blob.read(value);
            isServiceRooted.push_back(value);
        }
    }

    for (size_t i = 0; i < ids.size(); ++i)
    {
    }
}

void load(const char* fileName)
{
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
        throw std::runtime_error("The file header is corrupted, unexpected signature.");
    }

    if (header.version != 0)
    {
        throw std::runtime_error("Unrecognized version.");
    }

    while (fileBlob.tell() < fileBlob.size())
    {
        ChunkHeader chunk = {};
        fileBlob.read(chunk);
        readChunkData(chunk, fileBlob, chunkBlob);

        if (memcmp(chunk.name, kChunkInstances, sizeof(chunk.name)) == 0)
        {
            printf("kChunkInstances\n");
            readInstances(chunk, chunkBlob);
        }
        else if (memcmp(chunk.name, kChunkHash, sizeof(chunk.name)) == 0)
        {
            printf("kChunkHash\n");
        }
        else if (memcmp(chunk.name, kChunkProperty, sizeof(chunk.name)) == 0)
        {
            printf("kChunkProperty\n");
            readProperty(chunk, chunkBlob);
        }
        else if (memcmp(chunk.name, kChunkParents, sizeof(chunk.name)) == 0)
        {
            printf("kChunkParents\n");
        }
        else if (memcmp(chunk.name, kChunkMetadata, sizeof(chunk.name)) == 0)
        {
            printf("kChunkMetadata\n");
            readMetadata(chunk, chunkBlob);
        }
        else if (memcmp(chunk.name, kChunkSharedStrings, sizeof(chunk.name)) == 0)
        {
            printf("kChunkSharedStrings\n");
            readSharedStrings(chunk, chunkBlob);
        }
        else if (memcmp(chunk.name, kChunkSignatures, sizeof(chunk.name)) == 0)
        {
            printf("kChunkSignatures\n");
        }
        else if (memcmp(chunk.name, kChunkEnd, sizeof(chunk.name)) == 0)
        {
            printf("kChunkEnd\n");
            break;
        }
        else
        {
            // ZSTD_decompress and LZ4_decompress_safe
            //  Unknown chunk, skip
            printf("unknown\n");
        }

        // unsigned int chunkSize = (chunk.compressedSize == 0) ? chunk.size : chunk.compressedSize;
        // blob.skip(chunkSize);
    }
}

int main()
{
    load("../data/test.rbxm");

    return 0;
}
