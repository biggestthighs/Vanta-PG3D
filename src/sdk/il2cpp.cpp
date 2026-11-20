#include "il2cpp.h"

namespace il2 {
    Api api{};
    static bool s_ready = false;
    static std::unordered_map<std::string, void*> s_classes;
    static std::mutex s_mtx;

    static void* Sym(HMODULE m, const char* n) { return reinterpret_cast<void*>(GetProcAddress(m, n)); }

    bool Init() {
        if (s_ready) return true;
        const HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
        if (!ga) { Log("[il2] GameAssembly.dll missing"); return false; }

#define BIND(f, n) api.f = reinterpret_cast<decltype(api.f)>(Sym(ga, n))
        BIND(domain_get, "il2cpp_domain_get");
        BIND(domain_get_assemblies, "il2cpp_domain_get_assemblies");
        BIND(assembly_get_image, "il2cpp_assembly_get_image");
        BIND(class_from_name, "il2cpp_class_from_name");
        BIND(class_get_fields, "il2cpp_class_get_fields");
        BIND(field_get_name, "il2cpp_field_get_name");
        BIND(field_get_offset, "il2cpp_field_get_offset");
        BIND(class_get_methods, "il2cpp_class_get_methods");
        BIND(method_get_name, "il2cpp_method_get_name");
        BIND(class_get_method_from_name, "il2cpp_class_get_method_from_name");
        BIND(method_get_pointer, "il2cpp_method_get_pointer");
        BIND(thread_attach, "il2cpp_thread_attach");
        BIND(object_new, "il2cpp_object_new");
        BIND(string_new, "il2cpp_string_new");
        BIND(class_get_name, "il2cpp_class_get_name");
        BIND(image_get_class_count, "il2cpp_image_get_class_count");
        BIND(image_get_class, "il2cpp_image_get_class");
        BIND(object_get_class, "il2cpp_object_get_class");
        BIND(method_get_param, "il2cpp_method_get_param");
        BIND(method_get_param_count, "il2cpp_method_get_param_count");
        BIND(class_from_type, "il2cpp_class_from_type");
        BIND(class_get_type, "il2cpp_class_get_type");
        BIND(type_get_object, "il2cpp_type_get_object");
#undef BIND

        if (!api.domain_get || !api.class_from_name || !api.class_get_fields ||
            !api.field_get_offset || !api.class_get_method_from_name || !api.thread_attach) {
            Log("[il2] critical exports missing");
            return false;
        }
        s_ready = true;
        Attach();
        Log("[il2] api ready");
        return true;
    }

    bool Ready() { return s_ready; }

    void Attach() {
        if (!api.domain_get || !api.thread_attach) return;
        if (void* d = api.domain_get()) api.thread_attach(d);
    }

    void* Image(const char* name) {
        if (!api.domain_get || !api.domain_get_assemblies) return nullptr;
        void* dom = api.domain_get();
        if (!dom) return nullptr;
        size_t count = 0;
        void** asms = static_cast<void**>(api.domain_get_assemblies(dom, &count));
        for (size_t i = 0; asms && i < count; i++) {
            void* img = api.assembly_get_image(asms[i]);
            if (!img) continue;
            const char* iname = *reinterpret_cast<const char**>(img);
            if (iname && strstr(iname, name)) return img;
        }
        return nullptr;
    }

    void* Class(const char* name, const char* ns) {
        if (!s_ready) return nullptr;
        const std::string key = std::string(ns) + "::" + name;
        {
            std::lock_guard<std::mutex> g(s_mtx);
            auto it = s_classes.find(key);
            if (it != s_classes.end()) return it->second;
        }
        void* found = nullptr;
        if (void* img = Image("Assembly-CSharp")) {
            if (api.image_get_class_count && api.image_get_class) {
                const size_t n = api.image_get_class_count(img);
                for (size_t i = 0; i < n && !found; i++) {
                    void* k = api.image_get_class(img, i);
                    if (!k || !api.class_get_name) continue;
                    const char* cn = api.class_get_name(k);
                    if (cn && strcmp(cn, name) == 0) found = k;
                }
            }
        }
        if (!found && api.class_from_name && api.domain_get) {
            void* dom = api.domain_get();
            size_t count = 0;
            void** asms = api.domain_get_assemblies ? static_cast<void**>(api.domain_get_assemblies(dom, &count)) : nullptr;
            for (size_t i = 0; asms && i < count && !found; i++) {
                void* img = api.assembly_get_image(asms[i]);
                if (img) found = api.class_from_name(img, ns, name);
            }
        }
        if (found) {
            std::lock_guard<std::mutex> g(s_mtx);
            s_classes[key] = found;
        }
        return found;
    }

    int FieldOffset(void* klass, const char* name) {
        if (!klass || !api.class_get_fields) return -1;
        void* iter = nullptr;
        while (void* f = api.class_get_fields(klass, &iter)) {
            const char* fn = api.field_get_name(f);
            if (fn && strcmp(fn, name) == 0) return api.field_get_offset(f);
        }
        return -1;
    }

    int FieldOffsetAny(void* klass, std::initializer_list<const char*> names) {
        for (const char* n : names) {
            const int o = FieldOffset(klass, n);
            if (o > 0) return o;
        }
        return -1;
    }

    void* MethodPtr(void* klass, const char* name, int argc) {
        if (!klass || !api.class_get_method_from_name) return nullptr;
        void* m = api.class_get_method_from_name(klass, name, argc);
        if (!m) return nullptr;
        void* p = api.method_get_pointer ? api.method_get_pointer(m) : nullptr;
        if (!p) p = *reinterpret_cast<void**>(m);
        if (!InModule(reinterpret_cast<uintptr_t>(p), L"GameAssembly.dll")) {
            Log("[il2] %s -> bad ptr %p", name, p);
            return nullptr;
        }
        return p;
    }

    void* MethodBySig(void* klass, const char* name, std::initializer_list<const char*> params) {
        if (!klass || !api.class_get_methods || !api.method_get_name) return nullptr;
        if (!api.method_get_param || !api.method_get_param_count || !api.class_from_type || !api.class_get_name) {
            Log("[il2] %s: no param reflection, cannot match signature", name);
            return nullptr;
        }
        void* iter = nullptr;
        while (void* m = api.class_get_methods(klass, &iter)) {
            const char* mn = api.method_get_name(m);
            if (!mn || strcmp(mn, name) != 0) continue;
            if (api.method_get_param_count(m) != params.size()) continue;
            bool match = true;
            uint32_t i = 0;
            for (const char* want : params) {
                void* t = api.method_get_param(m, i++);
                void* k = t ? api.class_from_type(t) : nullptr;
                const char* tn = k ? api.class_get_name(k) : nullptr;
                if (!tn || strcmp(tn, want) != 0) { match = false; break; }
            }
            if (!match) continue;
            void* p = api.method_get_pointer ? api.method_get_pointer(m) : nullptr;
            if (!p) p = *reinterpret_cast<void**>(m);
            if (!InModule(reinterpret_cast<uintptr_t>(p), L"GameAssembly.dll")) {
                Log("[il2] %s -> bad ptr %p", name, p);
                return nullptr;
            }
            return p;
        }
        return nullptr;
    }

    std::string Str(void* s) {
        if (!Ok(s)) return {};
        const int len = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(s) + 0x10);
        if (len <= 0 || len > 512) return {};
        const wchar_t* w = reinterpret_cast<wchar_t*>(reinterpret_cast<uint8_t*>(s) + 0x14);
        std::string out;
        out.reserve(len);
        for (int i = 0; i < len; i++) out.push_back(w[i] < 128 ? static_cast<char>(w[i]) : '?');
        return out;
    }

    void* TypeObject(void* klass) {
        if (!klass || !api.class_get_type || !api.type_get_object) return nullptr;
        void* t = api.class_get_type(klass);
        return t ? api.type_get_object(t) : nullptr;
    }

    bool ClassName(void* obj, std::string& out) {
        if (!Ok(obj) || !api.object_get_class || !api.class_get_name) return false;
        void* k = api.object_get_class(obj);
        if (!k) return false;
        const char* n = api.class_get_name(k);
        if (!n) return false;
        out = n;
        return true;
    }
}
