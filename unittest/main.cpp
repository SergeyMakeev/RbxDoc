#include <assert.h>
#include <cstdio>
#include <rbxdoc.h>

int main()
{
    rbxdoc::Document doc;
    rbxdoc::LoadResult res = doc.loadFile("../data/test.rbxm");
    if (res != rbxdoc::LoadResult::OK)
    {
        printf("Can't load file\n");
        return -1;
    }

    for (const rbxdoc::Instance& instance : doc.getInstances())
    {
        const char* typeName = doc.getTypeName(instance);
        if (_stricmp(typeName, "MeshPart") != 0)
        {
            continue;
        }

        printf("MeshPart --------\n");

        for (const rbxdoc::Property& prop : instance.getProperties())
        {
            const char* propName = prop.getName();
            rbxdoc::PropertyType propType = prop.getType();
            if (propType == rbxdoc::PropertyType::Unknown)
            {
                continue;
            }

            if (propType == rbxdoc::PropertyType::String && _stricmp(propName, "Name") == 0)
            {
                const char* instanceName = prop.asString();
                printf("Name: '%s'\n", instanceName);
            }

            if (propType == rbxdoc::PropertyType::CFrameMatrix && _stricmp(propName, "CFrame") == 0)
            {
                const rbxdoc::CFrame& cf = prop.asCFrame();
                printf("CFrame: t {%3.2f, %3.2f, %3.2f}\n"
                       "        r {%3.2f, %3.2f, %3.2f}\n"
                       "          {%3.2f, %3.2f, %3.2f}\n"
                       "          {%3.2f, %3.2f, %3.2f}\n",
                       cf.translation.x, cf.translation.y, cf.translation.z, cf.rotation.v[0], cf.rotation.v[1], cf.rotation.v[2], cf.rotation.v[3],
                       cf.rotation.v[4], cf.rotation.v[5], cf.rotation.v[6], cf.rotation.v[7], cf.rotation.v[8]);
            }

            if (propType == rbxdoc::PropertyType::String && _stricmp(propName, "MeshId") == 0)
            {
                const char* meshId = prop.asString();
                printf("MeshId: '%s'\n", meshId);
            }

            if (propType == rbxdoc::PropertyType::Vector3 && _stricmp(propName, "size") == 0)
            {
                const rbxdoc::Vec3& v3 = prop.asVec3();
                printf("Size: {%3.2f, %3.2f, %3.2f}\n", v3.x, v3.y, v3.z);
            }

            if (propType == rbxdoc::PropertyType::Vector3 && _stricmp(propName, "InitialSize") == 0)
            {
                const rbxdoc::Vec3& v3 = prop.asVec3();
                printf("InitialSize: {%3.2f, %3.2f, %3.2f}\n", v3.x, v3.y, v3.z);
            }
        }
    }

    return 0;
}
