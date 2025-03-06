#include "rbxdoc.h"
#include "rbxdoc_binary.h"
#include <string.h>

namespace rbxdoc
{

Type::Type(std::string&& _name)
    : name(std::move(_name))
{
}

Property::Property(const char* _name, PropertyType _type)
    : name(_name)
    , type(_type)
{
}

PropertyType Property::getType() const
{
    //
    return PropertyType::String;
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

    catch(...)
    {
        return LoadResult::Error;
    }
    
    
}

ArrayView<Instance> Document::getInstances() const { return ArrayView<Instance>(instances.begin(), instances.end()); }

} // namespace rbxdoc