{
  "targets": [
    {
      "target_name": "node_libzsh",
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "cflags_cc": ["-std=c++17", "-fexceptions"],
      "sources": [
        "src/addon.cc",
        "src/init/libzsh_init.cc",
        "src/parser/parser.cc",
        "src/parser/ast_builder.cc",
        "src/parser/preprocess.cc",
        "src/zle/zle_session.cc",
        "src/zle/widget_registry.cc",
        "src/zle/completion.cc",
        "src/util/thread_safety.cc",
        "src/util/string_convert.cc"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "src",
        "3rdparty/libzsh/3rdparty/zsh/Src",
        "3rdparty/libzsh/3rdparty/zsh/Src/Zle",
        "3rdparty/libzsh/build/zsh-build/Src",
        "3rdparty/libzsh/build/zsh-build/Src/Zle",
        "3rdparty/libzsh/build/zsh-build",
        "3rdparty/libzsh/build/generated",
        "3rdparty/libzsh/build/generated/Zle"
      ],
      "libraries": [
        "../3rdparty/libzsh/build/libzsh.a",
        "-lncurses",
        "-ldl",
        "-lm"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS",
        "HAVE_CONFIG_H",
        "MODULE=zsh/main"
      ],
      "conditions": [
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LIBRARY": "libc++",
            "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
            "MACOSX_DEPLOYMENT_TARGET": "10.15"
          },
          "libraries": [
            "-L/opt/homebrew/lib",
            "-L/usr/local/lib"
          ],
          "include_dirs": [
            "/opt/homebrew/include",
            "/usr/local/include"
          ]
        }],
        ["OS=='linux'", {
          "libraries": [
            "-ltinfo",
            "-lrt"
          ]
        }]
      ]
    }
  ]
}
