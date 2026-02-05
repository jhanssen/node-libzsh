#ifndef NODE_LIBZSH_STRING_CONVERT_H
#define NODE_LIBZSH_STRING_CONVERT_H

#include <string>
#include <napi.h>

// Forward declare zsh types
extern "C" {
    // Metafied string handling
    char *unmetafy(char *s, int *len);
    char *metafy(char *buf, int len, int heap);
}

namespace node_libzsh {

// Convert a metafied zsh string to a regular C++ string
std::string unmeta(const char* s);

// Convert a C++ string to a metafied zsh string (caller must free if not on heap)
char* tometa(const std::string& s, bool useHeap = false);

// Convert zsh string to JavaScript string
Napi::String toJsString(Napi::Env env, const char* s);

// Convert JavaScript string to C++ string
std::string fromJsString(const Napi::Value& value);

} // namespace node_libzsh

#endif // NODE_LIBZSH_STRING_CONVERT_H
