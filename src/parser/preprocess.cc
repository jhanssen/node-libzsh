#include "preprocess.h"
#include <regex>
#include <sstream>

namespace node_libzsh {

// Helper to check if character is valid for identifier
static bool isIdentChar(char c, bool first) {
    if (first) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Parse a JS function name starting after @
static std::string parseFunctionName(const std::string& input, size_t& pos) {
    std::string name;
    if (pos >= input.size() || !isIdentChar(input[pos], true)) {
        return name;
    }

    while (pos < input.size() && isIdentChar(input[pos], false)) {
        name += input[pos++];
    }

    return name;
}

// Parse arguments after function name (until pipe, newline, or semicolon)
static std::vector<std::string> parseArguments(const std::string& input, size_t& pos) {
    std::vector<std::string> args;

    // Skip leading whitespace
    while (pos < input.size() && input[pos] == ' ') {
        pos++;
    }

    std::string currentArg;
    bool inQuote = false;
    char quoteChar = 0;
    bool escaped = false;

    while (pos < input.size()) {
        char c = input[pos];

        if (escaped) {
            currentArg += c;
            escaped = false;
            pos++;
            continue;
        }

        if (c == '\\' && !inQuote) {
            escaped = true;
            pos++;
            continue;
        }

        if (!inQuote && (c == '"' || c == '\'')) {
            inQuote = true;
            quoteChar = c;
            currentArg += c;
            pos++;
            continue;
        }

        if (inQuote && c == quoteChar) {
            inQuote = false;
            currentArg += c;
            pos++;
            continue;
        }

        // End of argument markers (not in quote)
        if (!inQuote && (c == '|' || c == '\n' || c == ';' || c == '&' || c == ')')) {
            break;
        }

        if (!inQuote && c == ' ') {
            if (!currentArg.empty()) {
                args.push_back(currentArg);
                currentArg.clear();
            }
            pos++;
            continue;
        }

        currentArg += c;
        pos++;
    }

    if (!currentArg.empty()) {
        args.push_back(currentArg);
    }

    return args;
}

// Find matching brace for @{ ... }
static size_t findMatchingBrace(const std::string& input, size_t startPos) {
    int depth = 1;
    size_t pos = startPos;
    bool inString = false;
    char stringChar = 0;
    bool escaped = false;

    while (pos < input.size() && depth > 0) {
        char c = input[pos];

        if (escaped) {
            escaped = false;
            pos++;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            pos++;
            continue;
        }

        if (!inString && (c == '"' || c == '\'' || c == '`')) {
            inString = true;
            stringChar = c;
            pos++;
            continue;
        }

        if (inString && c == stringChar) {
            inString = false;
            pos++;
            continue;
        }

        if (!inString) {
            if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
            }
        }

        pos++;
    }

    return pos - 1; // Return position of closing brace
}

PreprocessResult preprocessInput(const std::string& input) {
    PreprocessResult result;
    std::string& output = result.processed;
    std::vector<JsBlock>& blocks = result.jsBlocks;

    size_t pos = 0;
    int blockId = 0;

    while (pos < input.size()) {
        // Look for @ syntax
        if (input[pos] == '@') {
            size_t startPos = pos;
            pos++; // Skip @

            JsBlock block;
            block.startPos = startPos;

            // Check for buffered mode @!
            if (pos < input.size() && input[pos] == '!') {
                block.buffered = true;
                pos++;
            } else {
                block.buffered = false;
            }

            // Check for inline block @{ or @!{
            if (pos < input.size() && input[pos] == '{') {
                pos++; // Skip {
                size_t codeStart = pos;
                size_t braceEnd = findMatchingBrace(input, pos);

                if (braceEnd >= input.size()) {
                    // No matching brace, treat as literal
                    output += input.substr(startPos, pos - startPos);
                    continue;
                }

                block.type = "inline";
                block.content = input.substr(codeStart, braceEnd - codeStart);
                block.original = input.substr(startPos, braceEnd + 1 - startPos);
                block.endPos = braceEnd + 1;
                pos = braceEnd + 1;
            } else {
                // Function call @funcName or @!funcName
                std::string funcName = parseFunctionName(input, pos);

                if (funcName.empty()) {
                    // Not a valid function name, treat as literal
                    output += '@';
                    if (block.buffered) output += '!';
                    continue;
                }

                block.type = "call";
                block.content = funcName;

                // Parse arguments
                block.args = parseArguments(input, pos);

                block.endPos = pos;
                block.original = input.substr(startPos, pos - startPos);
            }

            // Generate placeholder ID
            std::ostringstream idStream;
            idStream << "__JS_" << blockId++ << "__";
            block.id = idStream.str();

            blocks.push_back(block);
            output += block.id;
        } else {
            output += input[pos++];
        }
    }

    return result;
}

// Helper to convert JsBlock to Napi object
static Napi::Object jsBlockToNapi(Napi::Env env, const JsBlock& block) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("id", block.id);
    obj.Set("original", block.original);
    obj.Set("type", block.type);
    obj.Set("buffered", block.buffered);
    obj.Set("content", block.content);

    if (!block.args.empty()) {
        Napi::Array args = Napi::Array::New(env, block.args.size());
        for (size_t i = 0; i < block.args.size(); i++) {
            args.Set(static_cast<uint32_t>(i), block.args[i]);
        }
        obj.Set("args", args);
    }

    return obj;
}

// Convert Napi object back to JsBlock
static JsBlock napiToJsBlock(const Napi::Object& obj) {
    JsBlock block;
    block.id = obj.Get("id").As<Napi::String>().Utf8Value();
    block.original = obj.Get("original").As<Napi::String>().Utf8Value();
    block.type = obj.Get("type").As<Napi::String>().Utf8Value();
    block.buffered = obj.Get("buffered").As<Napi::Boolean>().Value();
    block.content = obj.Get("content").As<Napi::String>().Utf8Value();

    if (obj.Has("args") && obj.Get("args").IsArray()) {
        Napi::Array args = obj.Get("args").As<Napi::Array>();
        for (uint32_t i = 0; i < args.Length(); i++) {
            block.args.push_back(args.Get(i).As<Napi::String>().Utf8Value());
        }
    }

    return block;
}

// Recursively restore JS nodes in AST
static void restoreJsInNode(Napi::Env env, Napi::Object& node,
                            const std::vector<JsBlock>& blocks) {
    if (!node.Has("type")) return;

    std::string type = node.Get("type").As<Napi::String>().Utf8Value();

    // Check if this is a SimpleCommand that might be a placeholder
    if (type == "SimpleCommand") {
        if (node.Has("words")) {
            Napi::Array words = node.Get("words").As<Napi::Array>();
            if (words.Length() >= 1) {
                std::string firstWord = words.Get(static_cast<uint32_t>(0)).As<Napi::String>().Utf8Value();

                // Check if it matches a placeholder
                for (const auto& block : blocks) {
                    if (firstWord == block.id) {
                        // Transform this node to JsCall or JsInline
                        if (block.type == "call") {
                            node.Set("type", "JsCall");
                            node.Set("name", block.content);
                            node.Set("buffered", block.buffered);

                            Napi::Array args = Napi::Array::New(env, block.args.size());
                            for (size_t i = 0; i < block.args.size(); i++) {
                                args.Set(static_cast<uint32_t>(i), block.args[i]);
                            }
                            node.Set("args", args);
                        } else {
                            node.Set("type", "JsInline");
                            node.Set("code", block.content);
                            node.Set("buffered", block.buffered);
                        }

                        // Remove the words property as it's no longer relevant
                        node.Delete("words");
                        return;
                    }
                }
            }
        }
    }

    // Recursively process child nodes
    if (node.Has("body") && node.Get("body").IsObject()) {
        Napi::Object body = node.Get("body").As<Napi::Object>();
        restoreJsInNode(env, body, blocks);
        node.Set("body", body);
    }

    if (node.Has("sublist") && node.Get("sublist").IsObject()) {
        Napi::Object sublist = node.Get("sublist").As<Napi::Object>();
        restoreJsInNode(env, sublist, blocks);
        node.Set("sublist", sublist);
    }

    if (node.Has("next") && node.Get("next").IsObject()) {
        Napi::Object next = node.Get("next").As<Napi::Object>();
        restoreJsInNode(env, next, blocks);
        node.Set("next", next);
    }

    if (node.Has("pipeline") && node.Get("pipeline").IsObject()) {
        Napi::Object pipeline = node.Get("pipeline").As<Napi::Object>();
        restoreJsInNode(env, pipeline, blocks);
        node.Set("pipeline", pipeline);
    }

    if (node.Has("commands") && node.Get("commands").IsArray()) {
        Napi::Array commands = node.Get("commands").As<Napi::Array>();
        for (uint32_t i = 0; i < commands.Length(); i++) {
            if (commands.Get(i).IsObject()) {
                Napi::Object cmd = commands.Get(i).As<Napi::Object>();
                restoreJsInNode(env, cmd, blocks);
                commands.Set(i, cmd);
            }
        }
        node.Set("commands", commands);
    }

    if (node.Has("command") && node.Get("command").IsObject()) {
        Napi::Object cmd = node.Get("command").As<Napi::Object>();
        restoreJsInNode(env, cmd, blocks);
        node.Set("command", cmd);
    }

    if (node.Has("condition") && node.Get("condition").IsObject()) {
        Napi::Object cond = node.Get("condition").As<Napi::Object>();
        restoreJsInNode(env, cond, blocks);
        node.Set("condition", cond);
    }

    if (node.Has("clauses") && node.Get("clauses").IsArray()) {
        Napi::Array clauses = node.Get("clauses").As<Napi::Array>();
        for (uint32_t i = 0; i < clauses.Length(); i++) {
            if (clauses.Get(i).IsObject()) {
                Napi::Object clause = clauses.Get(i).As<Napi::Object>();
                restoreJsInNode(env, clause, blocks);
                clauses.Set(i, clause);
            }
        }
        node.Set("clauses", clauses);
    }

    if (node.Has("cases") && node.Get("cases").IsArray()) {
        Napi::Array cases = node.Get("cases").As<Napi::Array>();
        for (uint32_t i = 0; i < cases.Length(); i++) {
            if (cases.Get(i).IsObject()) {
                Napi::Object c = cases.Get(i).As<Napi::Object>();
                restoreJsInNode(env, c, blocks);
                cases.Set(i, c);
            }
        }
        node.Set("cases", cases);
    }

    if (type == "Program" && node.Has("body") && node.Get("body").IsArray()) {
        Napi::Array body = node.Get("body").As<Napi::Array>();
        for (uint32_t i = 0; i < body.Length(); i++) {
            if (body.Get(i).IsObject()) {
                Napi::Object item = body.Get(i).As<Napi::Object>();
                restoreJsInNode(env, item, blocks);
                body.Set(i, item);
            }
        }
        node.Set("body", body);
    }
}

void restoreJsNodes(Napi::Object& ast, const std::vector<JsBlock>& blocks) {
    Napi::Env env = ast.Env();
    restoreJsInNode(env, ast, blocks);
}

Napi::Value Preprocess(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected string argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string input = info[0].As<Napi::String>().Utf8Value();

    PreprocessResult result = preprocessInput(input);

    Napi::Object output = Napi::Object::New(env);
    output.Set("processed", result.processed);

    Napi::Array jsBlocks = Napi::Array::New(env, result.jsBlocks.size());
    for (size_t i = 0; i < result.jsBlocks.size(); i++) {
        jsBlocks.Set(static_cast<uint32_t>(i), jsBlockToNapi(env, result.jsBlocks[i]));
    }
    output.Set("jsBlocks", jsBlocks);

    return output;
}

Napi::Value RestoreJs(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Expected AST and jsBlocks arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[0].IsObject()) {
        Napi::TypeError::New(env, "First argument must be an AST object").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[1].IsArray()) {
        Napi::TypeError::New(env, "Second argument must be an array of jsBlocks").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Deep clone the AST to avoid modifying the original
    // For simplicity, we'll work with the original and let JS handle cloning if needed
    Napi::Object ast = info[0].As<Napi::Object>();
    Napi::Array jsBlocksArray = info[1].As<Napi::Array>();

    // Convert jsBlocks to C++ vector
    std::vector<JsBlock> blocks;
    for (uint32_t i = 0; i < jsBlocksArray.Length(); i++) {
        if (jsBlocksArray.Get(i).IsObject()) {
            blocks.push_back(napiToJsBlock(jsBlocksArray.Get(i).As<Napi::Object>()));
        }
    }

    // Restore JS nodes
    restoreJsNodes(ast, blocks);

    return ast;
}

} // namespace node_libzsh
