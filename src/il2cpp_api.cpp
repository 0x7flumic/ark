#include "il2cpp_api.h"

#include <vector>

namespace arkhan::il2cpp {

namespace {

template <typename T>
bool Resolve(HMODULE module, const char* name, T& output) {
    output = reinterpret_cast<T>(GetProcAddress(module, name));
    return output != nullptr;
}

} // namespace

bool Api::Initialize(HMODULE gameAssembly, std::string* missingRequiredExports) {
    if (!gameAssembly) return false;
    if (missingRequiredExports) missingRequiredExports->clear();

    bool complete = true;
    const auto require = [&](const char* name, auto& output) {
        if (Resolve(gameAssembly, name, output)) return;
        complete = false;
        if (!missingRequiredExports) return;
        if (!missingRequiredExports->empty()) *missingRequiredExports += ',';
        *missingRequiredExports += name;
    };

    require("il2cpp_domain_get", domainGet_);
    require("il2cpp_domain_get_assemblies", domainGetAssemblies_);
    require("il2cpp_assembly_get_image", assemblyGetImage_);
    require("il2cpp_class_from_name", classFromName_);
    require("il2cpp_class_get_method_from_name", classGetMethod_);
    require("il2cpp_class_get_field_from_name", classGetField_);
    require("il2cpp_field_get_offset", fieldGetOffset_);
    require("il2cpp_thread_attach", threadAttach_);
    require("il2cpp_string_new", stringNew_);

    Resolve(gameAssembly, "il2cpp_thread_detach", threadDetach_);
    return complete;
}

bool Api::Ready() const {
    return domainGet_ && domainGetAssemblies_ && assemblyGetImage_ &&
           classFromName_ && classGetMethod_ && classGetField_ && fieldGetOffset_ &&
           threadAttach_ && stringNew_;
}

void* Api::AttachCurrentThread() const {
    const auto domain = domainGet_ ? domainGet_() : nullptr;
    return domain && threadAttach_ ? threadAttach_(domain) : nullptr;
}

void Api::DetachThread(void* thread) const {
    if (thread && threadDetach_) threadDetach_(thread);
}

Il2CppClass* Api::FindClass(const char* namespaze, const char* name) const {
    if (!Ready()) return nullptr;
    const auto domain = domainGet_();
    if (!domain) return nullptr;
    std::size_t count = 0;
    const auto assemblies = domainGetAssemblies_(domain, &count);
    if (!assemblies) return nullptr;

    for (std::size_t index = 0; index < count; ++index) {
        const auto image = assemblyGetImage_(assemblies[index]);
        if (!image) continue;
        if (auto* klass = classFromName_(image, namespaze, name)) return klass;
    }
    return nullptr;
}

const MethodInfo* Api::FindMethod(Il2CppClass* klass, const char* name, int parameterCount) const {
    return klass && classGetMethod_ ? classGetMethod_(klass, name, parameterCount) : nullptr;
}

int32_t Api::FindFieldOffset(Il2CppClass* klass, const char* name) const {
    if (!klass || !classGetField_ || !fieldGetOffset_) return -1;
    const auto field = classGetField_(klass, name);
    if (!field) return -1;
    const auto offset = fieldGetOffset_(field);
    return offset > static_cast<std::size_t>(INT32_MAX) ? -1 : static_cast<int32_t>(offset);
}

Il2CppString* Api::NewString(const std::string& value) const {
    return stringNew_ ? stringNew_(value.c_str()) : nullptr;
}

std::string Api::ToUtf8(const Il2CppString* value) const {
    if (!value || value->length <= 0) return {};
    const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value->chars, value->length,
                                               nullptr, 0, nullptr, nullptr);
    if (required <= 0) return "<string-conversion-failed>";
    std::string result(static_cast<std::size_t>(required), '\0');
    const auto written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value->chars, value->length,
                                              result.data(), required, nullptr, nullptr);
    return written == required ? result : "<string-conversion-failed>";
}

} // namespace arkhan::il2cpp
