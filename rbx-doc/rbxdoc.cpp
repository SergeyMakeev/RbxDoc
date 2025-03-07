#include "rbxdoc.h"
#include "rbxdoc_binary.h"
#include <string.h>

namespace rbxdoc
{

Type::Type(std::string&& _name)
    : name(std::move(_name))
{
}

const char* Type::getName() const { return name.c_str(); }

Property::Property(const char* _name, PropertyType _type)
    : name(_name)
    , type(_type)
{
}

PropertyType Property::getType() const { return type; }
const char* Property::getName() const { return name.c_str(); }

const char* Property::asString(const char* defaultVal) const
{
    if (type != PropertyType::String)
    {
        return defaultVal;
    }

    return std::get<std::string>(data).c_str();
}

float Property::asFloat(float defaultVal) const
{
    if (type != PropertyType::Float)
    {
        return defaultVal;
    }

    return std::get<float>(data);
}

const Vec3& Property::asVec3(const Vec3& defaultVal) const
{
    if (type != PropertyType::Vector3)
    {
        return defaultVal;
    }

    return std::get<Vec3>(data);
}

const CFrame& Property::asCFrame(const CFrame& defaultVal) const
{
    if (type != PropertyType::CFrameMatrix && type != PropertyType::CFrameQuat && type != PropertyType::OptionalCFrame)
    {
        return defaultVal;
    }

    if (type == PropertyType::OptionalCFrame)
    {
        const OptionalCFrame& ocf = std::get<OptionalCFrame>(data);
        if (ocf.hasData)
        {
            return ocf.val;
        }
    }
    return std::get<CFrame>(data);
}


Instance::Instance(int32_t _parentId, int32_t _id, uint32_t _typeIndex, bool _isService, bool _isServiceRooted)
    : parentId(_parentId)
    , id(_id)
    , typeIndex(_typeIndex)
    , isService(_isService)
    , isServiceRooted(_isServiceRooted)
{
}

ArrayView<Property> Instance::getProperties() const { return ArrayView<Property>(properties.begin(), properties.end()); }

LoadResult Document::loadFile(const char* fileName)
{
    if (!fileName)
    {
        return LoadResult::Error;
    }

    size_t len = strlen(fileName);
    if (len == 0)
    {
        return LoadResult::Error;
    }

    char lastChar = fileName[len - 1];
    if (lastChar == 'x' || lastChar == 'X')
    {
        // rbxlx, or rbxmx = XML based format (not supported for now)
        return LoadResult::Error;
    }

    try
    {
        LoadResult res = BinaryReader::loadBinary(fileName, *this);
        return res;
    }

    catch (...)
    {
        return LoadResult::Error;
    }
}

ArrayView<Instance> Document::getInstances() const { return ArrayView<Instance>(instances.begin(), instances.end()); }

const char* Document::getTypeName(const Instance& inst) const
{
    if (inst.id < 0 || size_t(inst.id) >= instances.size())
    {
        return "";
    }

    const Instance& instRef = instances[inst.id];
    if (&instRef != &inst)
    {
        return "";
    }

    if (inst.typeIndex >= types.size())
    {
        return "";
    }

    const Type& type = types[inst.typeIndex];
    return type.getName();
}

} // namespace rbxdoc