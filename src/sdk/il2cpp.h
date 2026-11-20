#pragma once
#include "../core/common.h"
#include <initializer_list>

namespace il2 {
    struct Api {
        void* (*domain_get)();
        void* (*domain_get_assemblies)(void*, size_t*);
        void* (*assembly_get_image)(void*);
        void* (*class_from_name)(void*, const char*, const char*);
        void* (*class_get_fields)(void*, void**);
        const char* (*field_get_name)(void*);
        int (*field_get_offset)(void*);
        void* (*class_get_methods)(void*, void**);
        const char* (*method_get_name)(void*);
        void* (*class_get_method_from_name)(void*, const char*, int);
        void* (*method_get_pointer)(void*);
        void* (*thread_attach)(void*);
        void* (*object_new)(void*);
        void* (*string_new)(const char*);
        const char* (*class_get_name)(void*);
        size_t (*image_get_class_count)(void*);
        void* (*image_get_class)(void*, size_t);
        void* (*object_get_class)(void*);
        void* (*method_get_param)(void*, uint32_t);
        uint32_t (*method_get_param_count)(void*);
        void* (*class_from_type)(void*);
        void* (*class_get_type)(void*);
        void* (*type_get_object)(void*);
    };

    extern Api api;
    bool Init();
    void Attach();
    void* Image(const char* name);
    void* Class(const char* name, const char* ns = "");
    int FieldOffset(void* klass, const char* name);
    int FieldOffsetAny(void* klass, std::initializer_list<const char*> names);
    void* MethodPtr(void* klass, const char* name, int argc);
    // Matches on parameter type names ("Vector3", "Int32", "Single", "RaycastHit"...).
    // Arg count alone is ambiguous on the Unity overload sets and picking the wrong
    // overload reads one parameter as another, which corrupts memory.
    void* MethodBySig(void* klass, const char* name, std::initializer_list<const char*> params);
    std::string Str(void* il2cppString);
    bool ClassName(void* obj, std::string& out);
    void* TypeObject(void* klass); // System.Type for a class, as Unity's Find/GetComponent calls want it
    bool Ready();
}
