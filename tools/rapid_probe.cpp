#include "../src/core/common.h"
#include "../src/sdk/il2cpp.h"
#include <fstream>
#include <cstring>
static DWORD WINAPI Run(LPVOID module) {
    if (il2::Init()) {
        std::ofstream out(DllDir() + L"\\rapid_probe.txt");
        HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
        auto ns = reinterpret_cast<const char*(*)(void*)>(GetProcAddress(ga,"il2cpp_class_get_namespace"));
        auto ft = reinterpret_cast<void*(*)(void*)>(GetProcAddress(ga,"il2cpp_field_get_type"));
        auto rt = reinterpret_cast<void*(*)(void*)>(GetProcAddress(ga,"il2cpp_method_get_return_type"));
        auto tn = reinterpret_cast<char*(*)(void*)>(GetProcAddress(ga,"il2cpp_type_get_name"));
        auto ff = reinterpret_cast<void(*)(void*)>(GetProcAddress(ga,"il2cpp_free"));
        auto name = [&](void* type) { char* p=type&&tn?tn(type):nullptr; std::string s=p?p:"?"; if(p&&ff)ff(p); return s; };
        size_t count=0;
        auto assemblies=static_cast<void**>(il2::api.domain_get_assemblies(il2::api.domain_get(),&count));
        for(size_t a=0; a<count; ++a) {
            void* image=il2::api.assembly_get_image(assemblies[a]);
            const size_t n=il2::api.image_get_class_count(image);
            for(size_t c=0;c<n;++c) {
                void* klass=il2::api.image_get_class(image,c);
                if(!klass)continue;
                const char* space=ns?ns(klass):"";
                const char* cn=il2::api.class_get_name(klass);
                if(strcmp(cn,"WeaponSounds") && strcmp(cn,"Player_move_c") && (!space || strncmp(space,"NewWeaponSystem",15)))continue;
                out<<"CLASS "<<(space?space:"")<<"."<<cn<<'\n';
                void* it=nullptr;
                while(void* field=il2::api.class_get_fields(klass,&it))
                    out<<"FIELD 0x"<<std::hex<<il2::api.field_get_offset(field)<<std::dec<<" "<<name(ft?ft(field):nullptr)<<" "<<il2::api.field_get_name(field)<<'\n';
                it=nullptr;
                while(void* method=il2::api.class_get_methods(klass,&it)) {
                    void* ptr=il2::api.method_get_pointer?il2::api.method_get_pointer(method):*reinterpret_cast<void**>(method);
                    out<<"METHOD "<<name(rt?rt(method):nullptr)<<" "<<il2::api.method_get_name(method)<<"(";
                    for(uint32_t p=0;p<il2::api.method_get_param_count(method);++p)out<<(p?",":"")<<name(il2::api.method_get_param(method,p));
                    out<<") RVA=0x"<<std::hex<<(reinterpret_cast<uintptr_t>(ptr)-reinterpret_cast<uintptr_t>(ga))<<std::dec<<'\n';
                }
                out<<'\n';
            }
        }
    }
    FreeLibraryAndExitThread(static_cast<HMODULE>(module),0);
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {g_self=module; DisableThreadLibraryCalls(module); HANDLE t=CreateThread(nullptr,0,Run,module,0,nullptr); if(t)CloseHandle(t);}
    return TRUE;
}
