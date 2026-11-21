# Historical fusion World58

`fusion-world58.sav` is an actual World58 save emitted by the frozen pre-Temporal C++ sources in `review_out/temporal-20261117/before`, not a new save with its version byte changed. It contains no source scene and has a live classical Chrono unit with `chrono=17`.

- SHA-256: `bbd7aac3faccd0586a0ad452efb226264bafcac8d1d323c105187e389fc6344a`
- Size: 68,323 bytes; original World checksum: `2688246965`.
- Generator: `review_out/temporal-20261117/legacy-fixture.cpp`, linked against the complete frozen pre-change object set and unchanged dependency libraries.
- Generator executable SHA-256: `3af87d3eddabc496db35dce5c2d25f4597bd28c6ea7d72b529b433559b74fdcb`.

The permanent Temporal regression loads this file into both a fresh World and an already active Source35 World. Both must restore the same historical checksum, disable source Temporal controllers and retain the independent legacy chrono progress. Do not regenerate this fixture with the current writer.
