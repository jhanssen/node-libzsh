#ifndef NODE_LIBZSH_COMPLETION_H
#define NODE_LIBZSH_COMPLETION_H

#include <napi.h>
#include <string>
#include <vector>
#include <functional>

namespace node_libzsh {

// Completion context
struct CompletionContext {
    std::string prefix;       // Text before cursor in current word
    std::string suffix;       // Text after cursor in current word
    std::vector<std::string> words;  // All words on line
    int current;              // Index of current word (0-based)
    std::string line;         // Full line
    int cursor;               // Cursor position in line
    std::string quote;        // Active quote character (empty if none)
};

// A single completion match
struct CompletionMatch {
    std::string value;        // Text to insert
    std::string display;      // Text to show in menu
    std::string description;  // Description
    std::string group;        // Group name
    std::string suffix;       // Auto-added suffix
    std::string removeSuffix; // Chars that remove the suffix
};

// Completion result
struct CompletionResult {
    std::vector<CompletionMatch> matches;
    bool exclusive = false;
};

// Completer options
struct CompleterOptions {
    std::string pattern;      // Regex pattern for command matching
    std::string position;     // "command", "argument", or "any"
    int priority = 0;         // Higher = runs first
};

// Completer function type
using CompleterFunction = std::function<CompletionResult(const CompletionContext&)>;

// Completion manager
class CompletionManager {
public:
    static CompletionManager& instance();

    // Register a completer
    void registerCompleter(const std::string& name,
                          Napi::FunctionReference&& func,
                          const CompleterOptions& options = {});

    // Unregister a completer
    bool unregisterCompleter(const std::string& name);

    // Get completion context from current ZLE state
    CompletionContext getContext() const;

    // Run completers and get matches
    CompletionResult complete(const CompletionContext& ctx);

    // Get list of registered completers
    std::vector<std::string> getCompleterNames() const;

private:
    CompletionManager() = default;
    ~CompletionManager() = default;

    struct CompleterEntry {
        Napi::FunctionReference func;
        CompleterOptions options;
    };

    std::vector<std::pair<std::string, CompleterEntry>> completers_;
};

// N-API wrappers
Napi::Value RegisterCompleter(const Napi::CallbackInfo& info);
Napi::Value UnregisterCompleter(const Napi::CallbackInfo& info);
Napi::Value GetCompletionContext(const Napi::CallbackInfo& info);
Napi::Value AddCompletions(const Napi::CallbackInfo& info);

} // namespace node_libzsh

#endif // NODE_LIBZSH_COMPLETION_H
