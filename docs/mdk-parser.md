# MDKParser plugin

The browser core implements the optional `MDKParser.dll` module. Linking it
registers the `MDKParser` TJS class. It is independent of `KAGParser`; MDK
scenario syntax produces its own tree of TJS `Dictionary` and `Array` objects.

```tjs
Plugins.link("MDKParser.dll");
var parser = new MDKParser();
var scenario = parser.loadScenario(storageName);
```

The storage uses the engine's normal text-stream decoding and archive lookup.
`loadScenario` requires a storage argument and returns a dictionary containing
`lines`. An empty source returns a null object. As in the supplied native plugin,
a call with its result discarded does not open or parse the storage.

The parser preserves physical line positions. Blank lines are integer zero,
comments are void, standalone directives are dictionaries, and text lines are
arrays containing strings and inline tag dictionaries. Supported syntax includes
labels, speaker aliases, jumps, choices, transitions, default tags, multiline
tags, attribute and parameter dictionaries, positional commands, symbolic
references, ruby, text styles, inline images and emoji. Tag names remain data
for the scenario framework to interpret.

This is a browser implementation of the separate MDK plugin contract, which
is not registered by the four KiriKiri 1.3.9 reference cores. It does not execute
Windows plugin binaries. Loading this parser does not provide other optional
plugins used by a scenario framework, such as a model renderer.
