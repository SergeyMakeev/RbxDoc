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
    static void readInt32Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readInt64Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readFloatProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readDoubleProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readColor3Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readUColor3Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readVector3Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readVector2Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readEnumProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readRefProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readFontProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readSharedStringProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readPhysicalProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readNumberRangeProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readCFrameProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readOptionalCFrameProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readUdim2Properties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readRect2DProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readColorSequenceProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readNumberSequenceProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readBrickColorProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);
    static void readUniqueIdProperties(const std::string& name, BinaryBlob& blob, Document& doc, const std::vector<uint32_t>& typeInstances);

    static void createEmptyProperties(const std::string& name, Document& doc, const std::vector<uint32_t>& typeInstances);

    static void readInstances(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc);
    static void readParentsChunk(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc);
    static void readProperties(const ChunkHeader& chunk, BinaryBlob& blob, Document& doc);

  public:
    static LoadResult loadBinary(const char* fileName, Document& doc);
};

} // namespace rbxdoc
