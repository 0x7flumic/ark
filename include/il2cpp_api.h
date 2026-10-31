#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace arkhan::il2cpp {

using Il2CppMethodPointer = void(*)();

struct MethodInfo;
struct Il2CppClass;

struct VirtualInvokeData {
    Il2CppMethodPointer methodPtr;
    const MethodInfo* method;
};

struct Il2CppType {
    void* data;
    unsigned int bits;
};

struct Il2CppObject {
    Il2CppClass* klass;
    void* monitor;
};

struct Il2CppString : Il2CppObject {
    int32_t length;
    wchar_t chars[1];
};

struct Il2CppClassPart1 {
    void* image;
    void* gcDesc;
    const char* name;
    const char* namespaze;
    Il2CppType byvalArg;
    Il2CppType thisArg;
    Il2CppClass* elementClass;
    Il2CppClass* castClass;
    Il2CppClass* declaringType;
    Il2CppClass* parent;
    void* genericClass;
    void* typeMetadataHandle;
    void* interopData;
    Il2CppClass* klass;
    void* fields;
    void* events;
    void* properties;
    void* methods;
    Il2CppClass** nestedTypes;
    Il2CppClass** implementedInterfaces;
    void* interfaceOffsets;
};

struct Il2CppClassPart2 {
    Il2CppClass** typeHierarchy;
    void* unityUserData;
    uint32_t initializationExceptionGCHandle;
    uint32_t cctorStarted;
    uint32_t cctorFinished;
    std::size_t cctorThread;
    void* genericContainerHandle;
    uint32_t instanceSize;
    uint32_t actualSize;
    uint32_t elementSize;
    int32_t nativeSize;
    uint32_t staticFieldsSize;
    uint32_t threadStaticFieldsSize;
    int32_t threadStaticFieldsOffset;
    uint32_t flags;
    uint32_t token;
    uint16_t methodCount;
    uint16_t propertyCount;
    uint16_t fieldCount;
    uint16_t eventCount;
    uint16_t nestedTypeCount;
    uint16_t vtableCount;
    uint16_t interfacesCount;
    uint16_t interfaceOffsetsCount;
    uint8_t typeHierarchyDepth;
    uint8_t genericRecursionDepth;
    uint8_t rank;
    uint8_t minimumAlignment;
    uint8_t naturalAlignment;
    uint8_t packingSize;
    uint8_t bitflags1;
    uint8_t bitflags2;
};

struct Il2CppClass {
    Il2CppClassPart1 part1;
    void* staticFields;
    void* rgctxData;
    Il2CppClassPart2 part2;
    VirtualInvokeData vtable[1];
};

using InvokerMethod = void(*)(Il2CppMethodPointer, const MethodInfo*, void*, void**, void*);

struct MethodInfo {
    Il2CppMethodPointer methodPointer;
    Il2CppMethodPointer virtualMethodPointer;
    InvokerMethod invokerMethod;
    const char* name;
    Il2CppClass* klass;
    const Il2CppType* returnType;
    const Il2CppType** parameters;
    const void* rgctxOrMetadata;
    const void* genericMethodOrContainer;
    uint32_t token;
    uint16_t flags;
    uint16_t iflags;
    uint16_t slot;
    uint8_t parametersCount;
    uint8_t bitflags;
};

class Api final {
public:
    bool Initialize(HMODULE gameAssembly, std::string* missingRequiredExports = nullptr);
    bool Ready() const;
    void* AttachCurrentThread() const;
    void DetachThread(void* thread) const;
    Il2CppClass* FindClass(const char* namespaze, const char* name) const;
    const MethodInfo* FindMethod(Il2CppClass* klass, const char* name, int parameterCount) const;
    int32_t FindFieldOffset(Il2CppClass* klass, const char* name) const;
    Il2CppString* NewString(const std::string& value) const;
    std::string ToUtf8(const Il2CppString* value) const;

private:
    using DomainGet = void*(*)();
    using DomainGetAssemblies = const void**(*)(const void*, std::size_t*);
    using AssemblyGetImage = const void*(*)(const void*);
    using ImageGetName = const char*(*)(const void*);
    using ClassFromName = Il2CppClass*(*)(const void*, const char*, const char*);
    using ClassGetMethod = const MethodInfo*(*)(Il2CppClass*, const char*, int);
    using ClassGetField = void*(*)(Il2CppClass*, const char*);
    using FieldGetOffset = std::size_t(*)(void*);
    using ThreadAttach = void*(*)(void*);
    using ThreadDetach = void(*)(void*);
    using StringNew = Il2CppString*(*)(const char*);

    DomainGet domainGet_{};
    DomainGetAssemblies domainGetAssemblies_{};
    AssemblyGetImage assemblyGetImage_{};
    ClassFromName classFromName_{};
    ClassGetMethod classGetMethod_{};
    ClassGetField classGetField_{};
    FieldGetOffset fieldGetOffset_{};
    ThreadAttach threadAttach_{};
    ThreadDetach threadDetach_{};
    StringNew stringNew_{};
};

} // namespace arkhan::il2cpp
