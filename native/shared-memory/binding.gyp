{
  "targets": [
    {
      "target_name": "shared_memory",
      "sources": [ "shared_memory.cpp" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "conditions": [
        ["OS=='linux'", {
          "libraries": [ "-lrt" ]
        }]
      ]
    }
  ]
}
