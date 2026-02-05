#include "string_convert.h"
#include "../zsh_wrapper.h"
#include <cstring>
#include <cstdlib>

namespace node_libzsh {

std::string unmeta(const char* s) {
    if (!s) return "";

    // Make a copy since unmetafy modifies in place
    size_t len = strlen(s);
    char* copy = static_cast<char*>(malloc(len + 1));
    if (!copy) return "";
    memcpy(copy, s, len + 1);

    int newlen;
    char* result = unmetafy(copy, &newlen);
    std::string ret(result, newlen);
    free(copy);
    return ret;
}

char* tometa(const std::string& s, bool useHeap) {
    // For now, simple copy - metafy handles special characters
    char* buf = static_cast<char*>(malloc(s.size() + 1));
    if (!buf) return nullptr;
    memcpy(buf, s.c_str(), s.size() + 1);

    // META_ALLOC = 4 allocates new memory
    // META_USEHEAP = 1 uses zsh heap
    return metafy(buf, static_cast<int>(s.size()), useHeap ? 1 : 4);
}

Napi::String toJsString(Napi::Env env, const char* s) {
    if (!s) return Napi::String::New(env, "");
    std::string str = unmeta(s);
    return Napi::String::New(env, str);
}

std::string fromJsString(const Napi::Value& value) {
    if (!value.IsString()) return "";
    return value.As<Napi::String>().Utf8Value();
}

} // namespace node_libzsh
