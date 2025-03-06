#pragma once

#include <string>
#include <variant>
#include <vector>

namespace rbxdoc
{

template <typename T> class ArrayView
{
  public:
    using value_type = T;
    using const_iterator = const T*;
    using size_type = size_t;

    // Default constructor creates an empty view.
    ArrayView()
        : data_(nullptr)
        , size_(0)
    {
    }

    // Constructor from a pointer to const data and a size.
    ArrayView(const T* data, size_type size)
        : data_(data)
        , size_(size)
    {
    }

    // Constructor from an iterator pair.
    // It assumes that the iterators refer to a contiguous block of memory.
    template <typename Iterator>
    ArrayView(Iterator first, Iterator last)
        : data_(first != last ? &*first : nullptr)
        , size_(std::distance(first, last))
    {
    }

    // Begin and end iterators to support range-based for loops.
    const_iterator begin() const { return data_; }
    const_iterator end() const { return data_ + size_; }

    // Returns the number of elements in the view.
    size_type size() const { return size_; }

    // Returns the underlying const data pointer.
    const T* data() const { return data_; }

    // Element access by index.
    const T& operator[](size_type i) const { return data_[i]; }

  private:
    const T* data_;
    size_type size_;
};

enum class PropertyType
{
    Unknown = 0,
    String,
    Bool,
    Int,
    Float,
    Double,
    UDim,
    UDim2,
    Ray,
    Faces,
    Axes,
    BrickColor,
    Color3,
    Vector2,
    Vector3,
    Vector2int16,
    CFrameMatrix,
    CFrameQuat,
    Enum,
    Ref,
    Vector3int16,
    NumberSequence,
    ColorSequenceV1,
    NumberRange,
    Rect2D,
    PhysicalProperties,
    Color3uint8,
    Int64,
    SharedStringDictionaryIndex,
    Bytecode,
    OptionalCFrame,
    UniqueId,
    Font,
    SecurityCapabilities,
    Content,
};

struct Vec3
{
    float x;
    float y;
    float z;
};

struct Mat3x3
{
    float v[9];
};

struct CFrame
{
    Mat3x3 rotation;
    Vec3 translation;
};

class Property
{
  public:
    Property() = default;
    Property(const char* _name, PropertyType _type);

    PropertyType getType() const;
    const char* getName() const;

    const char* asString(const char* defaultVal = "") const;
    float asFloat(float defaultVal = 0.0f) const;

  private:
    std::string name;
    PropertyType type;

    std::variant<std::string, bool, float, int32_t, uint32_t, CFrame> data;

    friend class BinaryReader;
    friend class Document;
};

class Instance
{
  public:
    Instance() = default;
    Instance(int32_t _parentId, int32_t _id, uint32_t _typeIndex, bool _isService, bool _isServiceRooted);

    ArrayView<Property> getProperties() const;

  private:
    std::vector<Property> properties;
    std::vector<int32_t> childIds;
    int32_t parentId = -1;
    int32_t id = -1;
    uint32_t typeIndex = uint32_t(-1);
    bool isService = false;
    bool isServiceRooted = false;

    friend class BinaryReader;
    friend class Document;
};

class Type
{
  public:
    Type() = default;
    Type(std::string&& _name);

    const char* getName() const;

  private:
    std::string name;
};

enum class LoadResult
{
    Error = 0,
    OK = 1
};

class Document
{
  public:
    LoadResult loadFile(const char* fileName);

    ArrayView<Instance> getInstances() const;

    const char* getTypeName(const Instance& inst) const;

  private:
    std::vector<Instance> instances;
    std::vector<Type> types;

    friend class BinaryReader;
};

} // namespace rbxdoc
