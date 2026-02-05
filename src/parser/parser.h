#ifndef NODE_LIBZSH_PARSER_H
#define NODE_LIBZSH_PARSER_H

#include <napi.h>
#include <string>

namespace node_libzsh {

// Parse options
struct ParseOptions {
    bool includeWordcode = false;
    bool includeLocations = false;
};

// Parse result
struct ParseResult {
    bool success;
    std::string error;
    int errorLine;
    int errorColumn;
};

// N-API wrapper functions
Napi::Value Parse(const Napi::CallbackInfo& info);
Napi::Value Validate(const Napi::CallbackInfo& info);
Napi::Value Generate(const Napi::CallbackInfo& info);
Napi::Value ExtractPipelines(const Napi::CallbackInfo& info);

} // namespace node_libzsh

#endif // NODE_LIBZSH_PARSER_H
