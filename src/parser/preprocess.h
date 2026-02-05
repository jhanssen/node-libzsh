#ifndef NODE_LIBZSH_PREPROCESS_H
#define NODE_LIBZSH_PREPROCESS_H

#include <napi.h>
#include <string>
#include <vector>

namespace node_libzsh {

// JS block extracted from input
struct JsBlock {
    std::string id;          // Placeholder ID (e.g., "__JS_0__")
    std::string original;    // Original syntax (e.g., "@filter" or "@!{ code }")
    std::string type;        // "call" or "inline"
    bool buffered;           // @! vs @
    std::string content;     // Function name or code block
    std::vector<std::string> args; // Arguments for function calls
    size_t startPos;         // Position in original input
    size_t endPos;           // End position in original input
};

// Preprocess result
struct PreprocessResult {
    std::string processed;          // Input with @... replaced by placeholders
    std::vector<JsBlock> jsBlocks;  // Extracted JS blocks
};

// Preprocess input to extract @ syntax
PreprocessResult preprocessInput(const std::string& input);

// Restore JS nodes in AST
// Takes an AST and replaces placeholder SimpleCommand nodes with JsCall/JsInline nodes
void restoreJsNodes(Napi::Object& ast, const std::vector<JsBlock>& jsBlocks);

// N-API wrappers
Napi::Value Preprocess(const Napi::CallbackInfo& info);
Napi::Value RestoreJs(const Napi::CallbackInfo& info);

} // namespace node_libzsh

#endif // NODE_LIBZSH_PREPROCESS_H
