{
  "targets": [
    {
      "target_name": "sandbox",
      "sources": [ "src/addon.cc",
                   "src/baton.cc",
                   "src/sandbox-wrap.cc" ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")"
			],
      "defines": [
      ],
      'conditions': [
      ]
    }
  ],
}
