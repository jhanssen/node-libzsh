#include "parser.h"
#include "ast_builder.h"
#include "../init/libzsh_init.h"
#include "../util/thread_safety.h"
#include "../util/string_convert.h"
#include "../zsh_wrapper.h"
#include <cstring>
#include <cstdlib>

// Additional declarations not in standard headers
extern "C" {
extern Eprog parse_list(void);
extern void freeeprog(Eprog p);
extern void useeprog(Eprog p);
extern Eprog dupeprog(Eprog p, int heap);
extern char *getpermtext(Eprog prog, Wordcode c, int start_indent);
extern void pushheap(void);
extern void popheap(void);
extern void inpop(void);
extern char *zshlextext;
extern void untokenize(char *s);
}

namespace node_libzsh {

// Internal parse function
static Eprog parseString(const std::string& input, std::string& error) {
    // Append newline if not present (zsh parser expects terminated input)
    std::string inputWithNl = input;
    if (inputWithNl.empty() || inputWithNl.back() != '\n') {
        inputWithNl += '\n';
    }

    // Make a mutable copy using zsh's allocator
    char* inputCopy = strdup(inputWithNl.c_str());
    if (!inputCopy) {
        error = "Memory allocation failed";
        return nullptr;
    }

    // Clear any previous error state
    errflag = 0;

    // Suppress zsh's stderr error output (zwarn checks noerrs).
    // We'll reconstruct the error message from parser state instead.
    noerrs = 1;

    // Use strinbeg before inpush — this order works correctly for all constructs.
    // strinbeg initializes lexer state (hbegin, lexinit, init_parse_status).
    pushheap();
    strinbeg(0);
    inpush(inputCopy, 0, NULL);

    // Parse the input
    Eprog prog = parse_list();

    // parse_list returns a heap-allocated eprog; make a permanent copy
    // before popheap() frees the heap
    Eprog result = nullptr;
    if (prog && !errflag) {
        result = dupeprog(prog, 0);
    }

    // Capture error info before cleanup destroys parser state
    std::string errorMsg;
    if (!result && zshlextext) {
        char* t = dupstring(zshlextext);
        if (t) {
            untokenize(t);
            errorMsg = std::string("parse error near `") + t + "'";
        }
    }

    strinend();
    inpop();
    popheap();
    free(inputCopy);
    noerrs = 0;

    if (!result) {
        error = errorMsg.empty() ? "Parse error" : errorMsg;
        errflag = 0;
        return nullptr;
    }

    return result;
}

Napi::Value Parse(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Check arguments
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected string argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Check initialization
    if (!isInitialized()) {
        Napi::Error::New(env, "libzsh not initialized. Call initialize() first.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string input = info[0].As<Napi::String>().Utf8Value();

    // Parse options
    AstOptions options;
    if (info.Length() > 1 && info[1].IsObject()) {
        Napi::Object opts = info[1].As<Napi::Object>();
        if (opts.Has("includeWordcode")) {
            options.includeWordcode = opts.Get("includeWordcode").ToBoolean().Value();
        }
        if (opts.Has("includeLocations")) {
            options.includeLocations = opts.Get("includeLocations").ToBoolean().Value();
        }
    }

    LIBZSH_LOCK();

    std::string error;
    Eprog prog = parseString(input, error);

    if (!prog) {
        // Return error result
        Napi::Object result = Napi::Object::New(env);
        result.Set("ast", env.Null());
        Napi::Array errors = Napi::Array::New(env);
        Napi::Object errObj = Napi::Object::New(env);
        errObj.Set("message", error);
        errors.Set(static_cast<uint32_t>(0), errObj);
        result.Set("errors", errors);
        return result;
    }

    // Build AST
    AstBuilder builder(env, options);
    Napi::Object ast = builder.build(prog);

    // Clean up
    freeeprog(prog);

    // Build result object
    Napi::Object result = Napi::Object::New(env);
    result.Set("ast", ast);

    return result;
}

Napi::Value Validate(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected string argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!isInitialized()) {
        Napi::Error::New(env, "libzsh not initialized. Call initialize() first.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string input = info[0].As<Napi::String>().Utf8Value();

    LIBZSH_LOCK();

    std::string error;
    Eprog prog = parseString(input, error);

    Napi::Object result = Napi::Object::New(env);

    if (prog) {
        result.Set("valid", true);
        freeeprog(prog);
    } else {
        result.Set("valid", false);
        Napi::Object errObj = Napi::Object::New(env);
        errObj.Set("message", error);
        result.Set("error", errObj);
    }

    return result;
}

Napi::Value Generate(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected AST object argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!isInitialized()) {
        Napi::Error::New(env, "libzsh not initialized. Call initialize() first.").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // For now, we need to re-parse from a string representation
    // A full implementation would walk the AST and generate code
    // This is a placeholder that requires the input to have been stored

    Napi::Object ast = info[0].As<Napi::Object>();

    // Check if we have the original source stored
    if (!ast.Has("_source")) {
        Napi::Error::New(env, "Generate requires original source in _source property or a parsed Eprog").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Return the stored source
    return ast.Get("_source");
}

Napi::Value ExtractPipelines(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected AST object argument").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Object ast = info[0].As<Napi::Object>();
    Napi::Array pipelines = Napi::Array::New(env);
    int pipelineIdx = 0;

    // Recursively extract pipelines from the AST
    std::function<void(const Napi::Object&)> extractFromNode;
    extractFromNode = [&](const Napi::Object& node) {
        if (!node.Has("type")) return;

        std::string type = node.Get("type").As<Napi::String>().Utf8Value();

        if (type == "Pipeline") {
            Napi::Object pipelineInfo = Napi::Object::New(env);
            pipelineInfo.Set("pipeline", node);

            if (node.Has("commands")) {
                Napi::Array commands = node.Get("commands").As<Napi::Array>();
                pipelineInfo.Set("commandCount", commands.Length());
            }

            pipelines.Set(pipelineIdx++, pipelineInfo);
        }

        // Recurse into children
        if (type == "Program" && node.Has("body")) {
            Napi::Array body = node.Get("body").As<Napi::Array>();
            for (uint32_t i = 0; i < body.Length(); i++) {
                if (body.Get(i).IsObject()) {
                    extractFromNode(body.Get(i).As<Napi::Object>());
                }
            }
        }

        if (type == "List" && node.Has("sublist")) {
            if (node.Get("sublist").IsObject()) {
                extractFromNode(node.Get("sublist").As<Napi::Object>());
            }
            if (node.Has("next") && node.Get("next").IsObject()) {
                extractFromNode(node.Get("next").As<Napi::Object>());
            }
        }

        if (type == "Sublist") {
            if (node.Has("pipeline") && node.Get("pipeline").IsObject()) {
                extractFromNode(node.Get("pipeline").As<Napi::Object>());
            }
            if (node.Has("next") && node.Get("next").IsObject()) {
                extractFromNode(node.Get("next").As<Napi::Object>());
            }
        }

        // Handle control structures
        if (node.Has("body") && node.Get("body").IsObject()) {
            extractFromNode(node.Get("body").As<Napi::Object>());
        }
        if (node.Has("condition") && node.Get("condition").IsObject()) {
            extractFromNode(node.Get("condition").As<Napi::Object>());
        }
        if (node.Has("clauses") && node.Get("clauses").IsArray()) {
            Napi::Array clauses = node.Get("clauses").As<Napi::Array>();
            for (uint32_t i = 0; i < clauses.Length(); i++) {
                if (clauses.Get(i).IsObject()) {
                    extractFromNode(clauses.Get(i).As<Napi::Object>());
                }
            }
        }
        if (node.Has("cases") && node.Get("cases").IsArray()) {
            Napi::Array cases = node.Get("cases").As<Napi::Array>();
            for (uint32_t i = 0; i < cases.Length(); i++) {
                if (cases.Get(i).IsObject()) {
                    extractFromNode(cases.Get(i).As<Napi::Object>());
                }
            }
        }
    };

    extractFromNode(ast);

    return pipelines;
}

} // namespace node_libzsh
