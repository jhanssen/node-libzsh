#include "completion.h"
#include "../util/thread_safety.h"
#include "../util/string_convert.h"
#include "../zsh_wrapper.h"
#include <algorithm>
#include <cstring>

extern "C" {
// ZLE globals for completion context
extern ZLE_STRING_T zleline;
extern int zlecs;
extern int zlell;

extern char *zlelineasstring(ZLE_STRING_T instr, int inll, int incs,
                             int *outllp, int *outcsp, int useheap);
}

namespace node_libzsh {

CompletionManager& CompletionManager::instance() {
    static CompletionManager manager;
    return manager;
}

void CompletionManager::registerCompleter(const std::string& name,
                                         Napi::FunctionReference&& func,
                                         const CompleterOptions& options) {
    LIBZSH_LOCK();

    // Remove existing completer with same name
    unregisterCompleter(name);

    CompleterEntry entry;
    entry.func = std::move(func);
    entry.options = options;

    // Insert sorted by priority (higher first)
    auto it = std::find_if(completers_.begin(), completers_.end(),
        [&options](const auto& pair) {
            return pair.second.options.priority < options.priority;
        });

    completers_.insert(it, std::make_pair(name, std::move(entry)));
}

bool CompletionManager::unregisterCompleter(const std::string& name) {
    LIBZSH_LOCK();

    auto it = std::find_if(completers_.begin(), completers_.end(),
        [&name](const auto& pair) { return pair.first == name; });

    if (it != completers_.end()) {
        completers_.erase(it);
        return true;
    }
    return false;
}

CompletionContext CompletionManager::getContext() const {
    CompletionContext ctx;

    LIBZSH_LOCK();

    if (!zleline || zlell == 0) {
        ctx.current = 0;
        ctx.cursor = 0;
        return ctx;
    }

    // Get line as string
    int outll, outcs;
    char* lineStr = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    if (!lineStr) {
        return ctx;
    }

    ctx.line = std::string(lineStr, outll);
    ctx.cursor = outcs;

    // Parse words from line
    std::string currentWord;
    bool inQuote = false;
    char quoteChar = 0;
    int wordStart = 0;
    int currentWordIdx = -1;

    for (int i = 0; i <= outll; i++) {
        char c = (i < outll) ? ctx.line[i] : '\0';

        if (i == outcs) {
            currentWordIdx = static_cast<int>(ctx.words.size());
            if (!currentWord.empty()) {
                ctx.prefix = currentWord;
            }
        }

        if (!inQuote && (c == '"' || c == '\'')) {
            inQuote = true;
            quoteChar = c;
            currentWord += c;
        } else if (inQuote && c == quoteChar) {
            inQuote = false;
            currentWord += c;
        } else if (!inQuote && (c == ' ' || c == '\t' || c == '\0')) {
            if (!currentWord.empty()) {
                ctx.words.push_back(currentWord);
                currentWord.clear();
            }
            wordStart = i + 1;
        } else {
            currentWord += c;
        }

        if (inQuote && i == outcs) {
            ctx.quote = std::string(1, quoteChar);
        }
    }

    // Set current word index
    ctx.current = (currentWordIdx >= 0) ? currentWordIdx : static_cast<int>(ctx.words.size());

    // Extract prefix/suffix from current word
    if (ctx.current < static_cast<int>(ctx.words.size())) {
        const std::string& word = ctx.words[ctx.current];
        // Find cursor position within the word
        int posInWord = ctx.cursor - wordStart;
        if (posInWord >= 0 && posInWord <= static_cast<int>(word.size())) {
            ctx.prefix = word.substr(0, posInWord);
            ctx.suffix = word.substr(posInWord);
        }
    }

    return ctx;
}

CompletionResult CompletionManager::complete(const CompletionContext& ctx) {
    CompletionResult result;

    // Run each completer
    for (auto& pair : completers_) {
        try {
            // Convert context to JS object
            Napi::Env env = pair.second.func.Env();
            Napi::Object jsCtx = Napi::Object::New(env);
            jsCtx.Set("prefix", ctx.prefix);
            jsCtx.Set("suffix", ctx.suffix);

            Napi::Array words = Napi::Array::New(env, ctx.words.size());
            for (size_t i = 0; i < ctx.words.size(); i++) {
                words.Set(static_cast<uint32_t>(i), ctx.words[i]);
            }
            jsCtx.Set("words", words);
            jsCtx.Set("current", ctx.current);
            jsCtx.Set("line", ctx.line);
            jsCtx.Set("cursor", ctx.cursor);
            if (!ctx.quote.empty()) {
                jsCtx.Set("quote", ctx.quote);
            } else {
                jsCtx.Set("quote", env.Null());
            }

            // Call the completer
            Napi::Value jsResult = pair.second.func.Call({ jsCtx });

            // Parse result
            if (jsResult.IsObject()) {
                Napi::Object resultObj = jsResult.As<Napi::Object>();

                if (resultObj.Has("matches") && resultObj.Get("matches").IsArray()) {
                    Napi::Array matches = resultObj.Get("matches").As<Napi::Array>();

                    for (uint32_t i = 0; i < matches.Length(); i++) {
                        if (matches.Get(i).IsObject()) {
                            Napi::Object m = matches.Get(i).As<Napi::Object>();
                            CompletionMatch match;

                            if (m.Has("value")) {
                                match.value = m.Get("value").As<Napi::String>().Utf8Value();
                            }
                            if (m.Has("display")) {
                                match.display = m.Get("display").As<Napi::String>().Utf8Value();
                            }
                            if (m.Has("description")) {
                                match.description = m.Get("description").As<Napi::String>().Utf8Value();
                            }
                            if (m.Has("group")) {
                                match.group = m.Get("group").As<Napi::String>().Utf8Value();
                            }
                            if (m.Has("suffix")) {
                                match.suffix = m.Get("suffix").As<Napi::String>().Utf8Value();
                            }
                            if (m.Has("removeSuffix")) {
                                match.removeSuffix = m.Get("removeSuffix").As<Napi::String>().Utf8Value();
                            }

                            result.matches.push_back(match);
                        }
                    }
                }

                if (resultObj.Has("exclusive")) {
                    result.exclusive = resultObj.Get("exclusive").As<Napi::Boolean>().Value();
                }

                // If exclusive, stop running more completers
                if (result.exclusive) {
                    break;
                }
            }
        } catch (...) {
            // Completer threw an exception, continue with next
        }
    }

    return result;
}

std::vector<std::string> CompletionManager::getCompleterNames() const {
    std::vector<std::string> names;
    for (const auto& pair : completers_) {
        names.push_back(pair.first);
    }
    return names;
}

Napi::Value RegisterCompleter(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Expected (name, function, [options]) arguments").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Completer name must be a string").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (!info[1].IsFunction()) {
        Napi::TypeError::New(env, "Completer handler must be a function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string name = info[0].As<Napi::String>().Utf8Value();
    Napi::FunctionReference func = Napi::Persistent(info[1].As<Napi::Function>());

    CompleterOptions options;
    if (info.Length() > 2 && info[2].IsObject()) {
        Napi::Object opts = info[2].As<Napi::Object>();

        if (opts.Has("pattern")) {
            options.pattern = opts.Get("pattern").As<Napi::String>().Utf8Value();
        }
        if (opts.Has("position")) {
            options.position = opts.Get("position").As<Napi::String>().Utf8Value();
        }
        if (opts.Has("priority")) {
            options.priority = opts.Get("priority").As<Napi::Number>().Int32Value();
        }
    }

    CompletionManager::instance().registerCompleter(name, std::move(func), options);

    return env.Undefined();
}

Napi::Value UnregisterCompleter(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected completer name string").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string name = info[0].As<Napi::String>().Utf8Value();
    bool success = CompletionManager::instance().unregisterCompleter(name);

    return Napi::Boolean::New(env, success);
}

Napi::Value GetCompletionContext(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    CompletionContext ctx = CompletionManager::instance().getContext();

    Napi::Object result = Napi::Object::New(env);
    result.Set("prefix", ctx.prefix);
    result.Set("suffix", ctx.suffix);

    Napi::Array words = Napi::Array::New(env, ctx.words.size());
    for (size_t i = 0; i < ctx.words.size(); i++) {
        words.Set(static_cast<uint32_t>(i), ctx.words[i]);
    }
    result.Set("words", words);
    result.Set("current", ctx.current);
    result.Set("line", ctx.line);
    result.Set("cursor", ctx.cursor);

    if (!ctx.quote.empty()) {
        result.Set("quote", ctx.quote);
    } else {
        result.Set("quote", env.Null());
    }

    return result;
}

Napi::Value AddCompletions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected array of completion matches").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // This function would typically integrate with zsh's completion system
    // For now, we just validate the input format

    Napi::Array matches = info[0].As<Napi::Array>();
    int validCount = 0;

    for (uint32_t i = 0; i < matches.Length(); i++) {
        if (matches.Get(i).IsObject()) {
            Napi::Object match = matches.Get(i).As<Napi::Object>();
            if (match.Has("value")) {
                validCount++;
            }
        }
    }

    return Napi::Number::New(env, validCount);
}

} // namespace node_libzsh
