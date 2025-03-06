#pragma once

namespace rbxdoc
{
struct ChunkHeader;
class BinaryBlob;

enum class LoadResult;
class Document;

class BinaryReader
{
    static void readStringProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readBoolProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readIntProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readFloatProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readEnumProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);

    static void readInstances(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc);
    static void readParentsChunk(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc);
    static void readProperties(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc);

  public:
    static LoadResult loadBinary(const char* fileName, Document& doc);
};

} // namespace rbxdoc
