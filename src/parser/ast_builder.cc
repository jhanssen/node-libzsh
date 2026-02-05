#include "ast_builder.h"
#include "../util/string_convert.h"
#include "../zsh_wrapper.h"
#include <cstring>

extern "C" {
// String extraction from wordcode
extern char *ecgetstr(struct estate *state, int dup, int *tokflag);
}

namespace node_libzsh {

AstBuilder::AstBuilder(Napi::Env env, const AstOptions& options)
    : env_(env), options_(options), prog_(nullptr), pc_(nullptr), strs_(nullptr) {
}

Napi::Object AstBuilder::build(Eprog prog) {
    return build(prog, prog->prog);
}

Napi::Object AstBuilder::build(Eprog prog, Wordcode pc) {
    prog_ = prog;
    pc_ = pc;
    strs_ = prog->strs;
    return buildProgram();
}

Napi::Object AstBuilder::createNode(const std::string& type) {
    Napi::Object node = Napi::Object::New(env_);
    node.Set("type", type);
    return node;
}

wordcode AstBuilder::nextCode() {
    return *pc_++;
}

wordcode AstBuilder::peekCode() {
    return *pc_;
}

void AstBuilder::skipCode(int count) {
    pc_ += count;
}

std::string AstBuilder::getString() {
    wordcode code = nextCode();

    // String encoding in zsh wordcode:
    // - Bit 0: contains tokens if set
    // - Bits 1-0 == 11x: empty string
    // - Bits 1-0 == 01x: short string (1-3 chars in upper bits)
    // - Otherwise: offset into strs array

    if ((code & 3) == 3) {
        // Empty string marker
        return "";
    }

    if ((code & 3) == 1) {
        // Short string - extract characters from upper bits
        char buf[4];
        int len = 0;
        unsigned int data = code >> 2;
        while (data && len < 3) {
            buf[len++] = static_cast<char>(data & 0xFF);
            data >>= 8;
        }
        buf[len] = '\0';
        return std::string(buf, len);
    }

    // Long string - offset into string table
    int offset = static_cast<int>(code >> 2);
    const char* str = strs_ + offset;
    return unmeta(str);
}

Napi::Object AstBuilder::buildProgram() {
    Napi::Object program = createNode("Program");
    Napi::Array body = Napi::Array::New(env_);

    int bodyIndex = 0;

    // Process all lists in the program
    while (wc_code(peekCode()) == WC_LIST) {
        body.Set(bodyIndex++, buildList());
    }

    program.Set("body", body);

    // Optionally include raw wordcode
    if (options_.includeWordcode && prog_) {
        int len = prog_->len;
        Napi::Uint32Array wordcode = Napi::Uint32Array::New(env_, len);
        for (int i = 0; i < len; i++) {
            wordcode[i] = prog_->prog[i];
        }
        program.Set("wordcode", wordcode);
    }

    return program;
}

Napi::Object AstBuilder::buildList() {
    wordcode code = nextCode();

    if (wc_code(code) != WC_LIST) {
        // Unexpected - return empty node
        return createNode("List");
    }

    Napi::Object list = createNode("List");

    int type = WC_LIST_TYPE(code);
    bool isAsync = (type & Z_ASYNC) != 0;
    bool isEnd = (type & Z_END) != 0;
    bool isSimple = (type & Z_SIMPLE) != 0;

    list.Set("async", isAsync);

    if (isSimple) {
        // Simple list - just a line number follows, then the sublist
        skipCode(1); // Skip line number
    }

    // Build the sublist
    list.Set("sublist", buildSublist());

    // Check for next list
    if (!isEnd && wc_code(peekCode()) == WC_LIST) {
        list.Set("next", buildList());
    }

    return list;
}

Napi::Object AstBuilder::buildSublist() {
    wordcode code = nextCode();

    if (wc_code(code) != WC_SUBLIST) {
        return createNode("Sublist");
    }

    Napi::Object sublist = createNode("Sublist");

    int type = WC_SUBLIST_TYPE(code);
    int flags = WC_SUBLIST_FLAGS(code);

    // Conjunction type
    switch (type) {
        case WC_SUBLIST_END:
            sublist.Set("conjunction", "end");
            break;
        case WC_SUBLIST_AND:
            sublist.Set("conjunction", "and");
            break;
        case WC_SUBLIST_OR:
            sublist.Set("conjunction", "or");
            break;
    }

    // Flags
    bool negate = (flags & WC_SUBLIST_NOT) != 0;
    bool coproc = (flags & WC_SUBLIST_COPROC) != 0;
    bool isSimple = (flags & WC_SUBLIST_SIMPLE) != 0;

    sublist.Set("negate", negate);
    if (coproc) {
        sublist.Set("coproc", true);
    }

    if (isSimple) {
        // Simple sublist - line number follows
        skipCode(1);
    }

    // Build the pipeline
    sublist.Set("pipeline", buildPipeline());

    // Check for next sublist
    if (type != WC_SUBLIST_END && wc_code(peekCode()) == WC_SUBLIST) {
        sublist.Set("next", buildSublist());
    }

    return sublist;
}

Napi::Object AstBuilder::buildPipeline() {
    Napi::Object pipeline = createNode("Pipeline");
    Napi::Array commands = Napi::Array::New(env_);

    int cmdIndex = 0;
    bool isFirst = true;
    bool hasMore = true;

    while (hasMore) {
        wordcode code = peekCode();

        if (wc_code(code) == WC_PIPE) {
            code = nextCode();
            int type = WC_PIPE_TYPE(code);
            bool isEnd = (type == WC_PIPE_END);

            // Skip offset to next pipe if not at end
            if (!isEnd) {
                skipCode(1);
            }

            // Build the command
            Napi::Object pipeCmd = buildPipelineCommand(isFirst, isEnd);
            commands.Set(cmdIndex++, pipeCmd);

            isFirst = false;
            hasMore = !isEnd;
        } else {
            // Not a pipe - single command
            Napi::Object pipeCmd = buildPipelineCommand(true, true);
            commands.Set(cmdIndex++, pipeCmd);
            hasMore = false;
        }
    }

    pipeline.Set("commands", commands);
    return pipeline;
}

Napi::Object AstBuilder::buildPipelineCommand(bool isFirst, bool isLast) {
    Napi::Object pipeCmd = createNode("PipelineCommand");

    // Determine position
    if (isFirst && isLast) {
        pipeCmd.Set("position", "only");
    } else if (isFirst) {
        pipeCmd.Set("position", "first");
    } else if (isLast) {
        pipeCmd.Set("position", "last");
    } else {
        pipeCmd.Set("position", "middle");
    }

    // Build the actual command
    pipeCmd.Set("command", buildCommand());

    return pipeCmd;
}

Napi::Object AstBuilder::buildCommand() {
    // First, collect any redirections that precede the command
    Napi::Array redirections = buildRedirections();

    wordcode code = nextCode();
    int wcType = wc_code(code);

    Napi::Object cmd;

    switch (wcType) {
        case WC_SIMPLE:
            cmd = buildSimpleCommand(code);
            break;
        case WC_TYPESET:
            cmd = buildTypeset(code);
            break;
        case WC_SUBSH:
            cmd = buildSubshell(code);
            break;
        case WC_CURSH:
            cmd = buildBraceGroup(code);
            break;
        case WC_FUNCDEF:
            cmd = buildFuncdef(code);
            break;
        case WC_FOR:
            cmd = buildFor(code);
            break;
        case WC_SELECT:
            cmd = buildSelect(code);
            break;
        case WC_WHILE:
            cmd = buildWhile(code);
            break;
        case WC_REPEAT:
            cmd = buildRepeat(code);
            break;
        case WC_CASE:
            cmd = buildCase(code);
            break;
        case WC_IF:
            cmd = buildIf(code);
            break;
        case WC_COND:
            cmd = buildCond(code);
            break;
        case WC_ARITH:
            cmd = buildArith(code);
            break;
        case WC_TIMED:
            cmd = buildTimed(code);
            break;
        case WC_TRY:
            cmd = buildTry(code);
            break;
        default:
            // Unknown command type
            cmd = createNode("Unknown");
            cmd.Set("code", static_cast<int>(wcType));
            break;
    }

    // Attach redirections if any
    if (redirections.Length() > 0) {
        cmd.Set("redirections", redirections);
    }

    return cmd;
}

Napi::Array AstBuilder::buildRedirections() {
    Napi::Array redirs = Napi::Array::New(env_);
    int index = 0;

    while (wc_code(peekCode()) == WC_REDIR) {
        wordcode code = nextCode();
        redirs.Set(index++, buildRedirection(code));
    }

    return redirs;
}

Napi::Object AstBuilder::buildRedirection(wordcode code) {
    Napi::Object redir = createNode("Redirection");

    int type = WC_REDIR_TYPE(code);
    bool hasVarId = WC_REDIR_VARID(code) != 0;
    bool fromHeredoc = WC_REDIR_FROM_HEREDOC(code) != 0;

    // Map redirection type to string
    static const char* redirTypes[] = {
        ">", ">|", ">>", ">>|", "&>", ">&|", ">>&", ">>&|",
        "<>", "<", "<<", "<<-", "<<<", "<&", ">&", ">&-", "<(", ">("
    };

    if (type >= 0 && type < static_cast<int>(sizeof(redirTypes) / sizeof(redirTypes[0]))) {
        redir.Set("op", redirTypes[type]);
    } else {
        redir.Set("op", "unknown");
    }

    // Get fd
    wordcode fd = nextCode();
    redir.Set("fd", static_cast<int>(fd));

    // Get name/target
    std::string name = getString();
    redir.Set("target", name);

    // Variable assignment form {var}>
    if (hasVarId) {
        std::string varId = getString();
        redir.Set("varId", varId);
    }

    // Here-document
    if (fromHeredoc) {
        std::string terminator = getString();
        std::string munged = getString();
        redir.Set("heredocTerminator", terminator);
    }

    return redir;
}

Napi::Object AstBuilder::buildSimpleCommand(wordcode code) {
    Napi::Object cmd = createNode("SimpleCommand");

    int argc = WC_SIMPLE_ARGC(code);

    // Collect any assignments first
    Napi::Array assignments = Napi::Array::New(env_);
    int assignIdx = 0;

    while (wc_code(peekCode()) == WC_ASSIGN) {
        wordcode assignCode = nextCode();
        assignments.Set(assignIdx++, buildAssignment(assignCode));
    }

    if (assignIdx > 0) {
        cmd.Set("assignments", assignments);
    }

    // Build words array
    Napi::Array words = Napi::Array::New(env_);
    for (int i = 0; i < argc; i++) {
        words.Set(i, getString());
    }
    cmd.Set("words", words);

    return cmd;
}

Napi::Object AstBuilder::buildAssignment(wordcode code) {
    Napi::Object assign = createNode("Assignment");

    int type = WC_ASSIGN_TYPE(code);
    int type2 = WC_ASSIGN_TYPE2(code);
    int numElements = WC_ASSIGN_NUM(code);

    bool isArray = (type == WC_ASSIGN_ARRAY);
    bool isAppend = (type2 == WC_ASSIGN_INC);

    // Get name
    std::string name = getString();
    assign.Set("name", name);

    assign.Set("append", isAppend);

    if (isArray) {
        assign.Set("isArray", true);
        Napi::Array elements = Napi::Array::New(env_);
        for (int i = 0; i < numElements; i++) {
            elements.Set(i, getString());
        }
        assign.Set("value", elements);
    } else {
        // Scalar
        std::string value = getString();
        assign.Set("value", value);
    }

    return assign;
}

Napi::Object AstBuilder::buildTypeset(wordcode code) {
    Napi::Object cmd = createNode("Typeset");

    int argc = WC_TYPESET_ARGC(code);

    // Build words array (command + arguments)
    Napi::Array words = Napi::Array::New(env_);
    for (int i = 0; i < argc; i++) {
        words.Set(i, getString());
    }
    cmd.Set("words", words);

    // Get assignment count
    wordcode assignCount = nextCode();
    if (assignCount > 0) {
        cmd.Set("assignments", buildAssignments(assignCount));
    }

    return cmd;
}

Napi::Array AstBuilder::buildAssignments(int count) {
    Napi::Array assignments = Napi::Array::New(env_);
    for (int i = 0; i < count; i++) {
        wordcode code = nextCode();
        assignments.Set(i, buildAssignment(code));
    }
    return assignments;
}

Napi::Object AstBuilder::buildSubshell(wordcode code) {
    Napi::Object cmd = createNode("Subshell");

    int skip = WC_SUBSH_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    // Build the body (list inside subshell)
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("body", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildBraceGroup(wordcode code) {
    Napi::Object cmd = createNode("BraceGroup");

    int skip = WC_CURSH_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    // Build the body
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("body", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildFuncdef(wordcode code) {
    Napi::Object cmd = createNode("FunctionDef");

    int skip = WC_FUNCDEF_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    // Get number of names
    wordcode numNames = nextCode();

    // Get function names
    Napi::Array names = Napi::Array::New(env_);
    for (unsigned int i = 0; i < numNames; i++) {
        names.Set(i, getString());
    }
    cmd.Set("names", names);

    // Skip some metadata
    skipCode(4); // offset, strs length, npats, tracing

    // Build function body
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("body", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildFor(wordcode code) {
    Napi::Object cmd = createNode("ForLoop");

    int type = WC_FOR_TYPE(code);
    int skip = WC_FOR_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    if (type == WC_FOR_COND) {
        // C-style for loop: for ((init; cond; incr))
        cmd.Set("style", "cstyle");
        cmd.Set("init", getString());
        cmd.Set("condition", getString());
        cmd.Set("update", getString());
    } else if (type == WC_FOR_PPARAM) {
        // for x in "$@"
        cmd.Set("style", "in");
        cmd.Set("variable", getString());
    } else {
        // for x in list
        cmd.Set("style", "in");
        cmd.Set("variable", getString());
        wordcode numElements = nextCode();
        Napi::Array list = Napi::Array::New(env_);
        for (unsigned int i = 0; i < numElements; i++) {
            list.Set(i, getString());
        }
        cmd.Set("list", list);
    }

    // Build body
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("body", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildSelect(wordcode code) {
    Napi::Object cmd = createNode("SelectStatement");

    int type = WC_SELECT_TYPE(code);
    int skip = WC_SELECT_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    cmd.Set("variable", getString());

    if (type == WC_SELECT_LIST) {
        wordcode numElements = nextCode();
        Napi::Array list = Napi::Array::New(env_);
        for (unsigned int i = 0; i < numElements; i++) {
            list.Set(i, getString());
        }
        cmd.Set("list", list);
    }

    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("body", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildWhile(wordcode code) {
    Napi::Object cmd = createNode("WhileLoop");

    int type = WC_WHILE_TYPE(code);
    int skip = WC_WHILE_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    cmd.Set("until", type == WC_WHILE_UNTIL);

    // Build condition
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("condition", buildList());
    }

    // Build body
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("body", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildRepeat(wordcode code) {
    Napi::Object cmd = createNode("RepeatLoop");

    int skip = WC_REPEAT_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    cmd.Set("count", getString());

    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("body", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildCase(wordcode code) {
    Napi::Object cmd = createNode("CaseStatement");

    int type = WC_CASE_TYPE(code);
    int skip = WC_CASE_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    if (type == WC_CASE_HEAD) {
        cmd.Set("word", getString());
    }

    Napi::Array cases = Napi::Array::New(env_);
    int caseIdx = 0;

    while (pc_ < endPos && wc_code(peekCode()) == WC_CASE) {
        wordcode caseCode = nextCode();
        int caseType = WC_CASE_TYPE(caseCode);
        int caseSkip = WC_CASE_SKIP(caseCode);

        if (caseType == WC_CASE_HEAD) continue;

        Napi::Object caseItem = Napi::Object::New(env_);

        // Get pattern
        caseItem.Set("pattern", getString());

        // Get pattern count
        skipCode(1); // pattern number

        // Terminator type
        switch (caseType) {
            case WC_CASE_OR:
                caseItem.Set("terminator", ";;");
                break;
            case WC_CASE_AND:
                caseItem.Set("terminator", ";&");
                break;
            case WC_CASE_TESTAND:
                caseItem.Set("terminator", ";|");
                break;
        }

        // Build case body
        if (wc_code(peekCode()) == WC_LIST) {
            caseItem.Set("body", buildList());
        }

        cases.Set(caseIdx++, caseItem);
    }

    cmd.Set("cases", cases);

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildIf(wordcode code) {
    Napi::Object cmd = createNode("IfStatement");

    int type = WC_IF_TYPE(code);
    int skip = WC_IF_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    Napi::Array clauses = Napi::Array::New(env_);
    int clauseIdx = 0;

    // Build the first if clause
    if (type == WC_IF_HEAD || type == WC_IF_IF) {
        // Skip to first IF clause if we're at HEAD
        if (type == WC_IF_HEAD) {
            code = nextCode();
            type = WC_IF_TYPE(code);
            skip = WC_IF_SKIP(code);
        }

        while (pc_ < endPos) {
            Napi::Object clause = Napi::Object::New(env_);

            switch (type) {
                case WC_IF_IF:
                    clause.Set("type", "if");
                    if (wc_code(peekCode()) == WC_LIST) {
                        clause.Set("condition", buildList());
                    }
                    if (wc_code(peekCode()) == WC_LIST) {
                        clause.Set("body", buildList());
                    }
                    break;
                case WC_IF_ELIF:
                    clause.Set("type", "elif");
                    if (wc_code(peekCode()) == WC_LIST) {
                        clause.Set("condition", buildList());
                    }
                    if (wc_code(peekCode()) == WC_LIST) {
                        clause.Set("body", buildList());
                    }
                    break;
                case WC_IF_ELSE:
                    clause.Set("type", "else");
                    if (wc_code(peekCode()) == WC_LIST) {
                        clause.Set("body", buildList());
                    }
                    break;
            }

            clauses.Set(clauseIdx++, clause);

            if (wc_code(peekCode()) == WC_IF) {
                code = nextCode();
                type = WC_IF_TYPE(code);
            } else {
                break;
            }
        }
    }

    cmd.Set("clauses", clauses);

    pc_ = endPos;
    return cmd;
}

Napi::Object AstBuilder::buildCond(wordcode code) {
    Napi::Object cmd = createNode("ConditionalExpression");

    int type = WC_COND_TYPE(code);
    int skip = WC_COND_SKIP(code);

    Wordcode savedPc = pc_;

    switch (type) {
        case COND_NOT:
            cmd.Set("op", "!");
            if (wc_code(peekCode()) == WC_COND) {
                code = nextCode();
                cmd.Set("operand", buildCond(code));
            }
            break;
        case COND_AND:
            cmd.Set("op", "&&");
            if (wc_code(peekCode()) == WC_COND) {
                code = nextCode();
                cmd.Set("left", buildCond(code));
            }
            if (wc_code(peekCode()) == WC_COND) {
                code = nextCode();
                cmd.Set("right", buildCond(code));
            }
            break;
        case COND_OR:
            cmd.Set("op", "||");
            if (wc_code(peekCode()) == WC_COND) {
                code = nextCode();
                cmd.Set("left", buildCond(code));
            }
            if (wc_code(peekCode()) == WC_COND) {
                code = nextCode();
                cmd.Set("right", buildCond(code));
            }
            break;
        case COND_STREQ:
        case COND_STRDEQ:
            cmd.Set("op", type == COND_STREQ ? "=" : "==");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            skipCode(1); // pattern number
            break;
        case COND_STRNEQ:
            cmd.Set("op", "!=");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            skipCode(1);
            break;
        case COND_STRLT:
            cmd.Set("op", "<");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_STRGTR:
            cmd.Set("op", ">");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_NT:
            cmd.Set("op", "-nt");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_OT:
            cmd.Set("op", "-ot");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_EF:
            cmd.Set("op", "-ef");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_EQ:
            cmd.Set("op", "-eq");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_NE:
            cmd.Set("op", "-ne");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_LT:
            cmd.Set("op", "-lt");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_GT:
            cmd.Set("op", "-gt");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_LE:
            cmd.Set("op", "-le");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_GE:
            cmd.Set("op", "-ge");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_REGEX:
            cmd.Set("op", "=~");
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        case COND_MOD:
            cmd.Set("op", "mod");
            cmd.Set("name", getString());
            // Additional strings for module condition
            break;
        case COND_MODI:
            cmd.Set("op", "modi");
            cmd.Set("name", getString());
            cmd.Set("left", getString());
            cmd.Set("right", getString());
            break;
        default:
            // Unary conditions - just operand
            cmd.Set("opType", static_cast<int>(type));
            cmd.Set("operand", getString());
            break;
    }

    if (skip > 0) {
        pc_ = savedPc + skip;
    }

    return cmd;
}

Napi::Object AstBuilder::buildArith(wordcode code) {
    Napi::Object cmd = createNode("ArithmeticCommand");
    cmd.Set("expression", getString());
    return cmd;
}

Napi::Object AstBuilder::buildTimed(wordcode code) {
    Napi::Object cmd = createNode("TimedCommand");

    int type = WC_TIMED_TYPE(code);

    if (type == WC_TIMED_PIPE) {
        cmd.Set("pipeline", buildPipeline());
    }

    return cmd;
}

Napi::Object AstBuilder::buildTry(wordcode code) {
    Napi::Object cmd = createNode("TryBlock");

    int skip = WC_TRY_SKIP(code);
    Wordcode endPos = pc_ + skip - 1;

    // Build try body
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("try", buildList());
    }

    // Build always body
    if (wc_code(peekCode()) == WC_LIST) {
        cmd.Set("always", buildList());
    }

    pc_ = endPos;
    return cmd;
}

Napi::Array AstBuilder::buildWords(int count) {
    Napi::Array words = Napi::Array::New(env_);
    for (int i = 0; i < count; i++) {
        words.Set(i, getString());
    }
    return words;
}

} // namespace node_libzsh
