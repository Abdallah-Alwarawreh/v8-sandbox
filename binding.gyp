{
  "targets": [
    {
      "target_name": "sandbox",
      "sources": [ "src/addon.cc",
                   "src/baton.cc",
                   "src/sandbox.cc" ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")"
			],
      "defines": [
      ],
      'conditions': [
      ],
      "cflags!": ["/std:c++17"],
      "cflags!": ["/std:c++20"],
    }
  ],
}
