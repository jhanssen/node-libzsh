#ifndef NODE_LIBZSH_AST_BUILDER_H
#define NODE_LIBZSH_AST_BUILDER_H

#include <napi.h>
#include <string>
#include <vector>

// Forward declare zsh types
extern "C" {
    struct eprog;
    typedef struct eprog *Eprog;
    typedef unsigned int wordcode;
    typedef wordcode *Wordcode;
}

namespace node_libzsh {

// Options for AST building
struct AstOptions {
    bool includeWordcode = false;
    bool includeLocations = false;
};

// AST Builder - converts zsh wordcode to JavaScript AST
class AstBuilder {
public:
    AstBuilder(Napi::Env env, const AstOptions& options = {});

    // Build AST from Eprog
    Napi::Object build(Eprog prog);

    // Build AST from specific wordcode position
    Napi::Object build(Eprog prog, Wordcode pc);

private:
    Napi::Env env_;
    AstOptions options_;
    Eprog prog_;
    Wordcode pc_;
    char* strs_;

    // Internal traversal methods
    Napi::Object buildProgram();
    Napi::Object buildList();
    Napi::Object buildSublist();
    Napi::Object buildPipeline();
    Napi::Object buildPipelineCommand(bool isFirst, bool isLast);
    Napi::Object buildCommand();
    Napi::Object buildSimpleCommand(wordcode code);
    Napi::Object buildTypeset(wordcode code);
    Napi::Object buildSubshell(wordcode code);
    Napi::Object buildBraceGroup(wordcode code);
    Napi::Object buildFuncdef(wordcode code);
    Napi::Object buildFor(wordcode code);
    Napi::Object buildSelect(wordcode code);
    Napi::Object buildWhile(wordcode code);
    Napi::Object buildRepeat(wordcode code);
    Napi::Object buildCase(wordcode code);
    Napi::Object buildIf(wordcode code);
    Napi::Object buildCond(wordcode code);
    Napi::Object buildArith(wordcode code);
    Napi::Object buildTimed(wordcode code);
    Napi::Object buildTry(wordcode code);

    // Helper methods
    Napi::Array buildRedirections();
    Napi::Object buildRedirection(wordcode code);
    Napi::Array buildAssignments(int count);
    Napi::Object buildAssignment(wordcode code);
    Napi::Array buildWords(int count);
    std::string getString();

    // Utility
    Napi::Object createNode(const std::string& type);
    wordcode nextCode();
    wordcode peekCode();
    void skipCode(int count = 1);
};

} // namespace node_libzsh

#endif // NODE_LIBZSH_AST_BUILDER_H
