# JetBrains Mono MSDF fixture

The atlas was generated from JetBrains Mono Regular v2.304. The upstream font is licensed under the SIL Open Font License 1.1. The upstream license and copyright notice accompany these fixtures in [OFL.txt](OFL.txt). [Upstream release](https://github.com/JetBrains/JetBrainsMono/releases/tag/v2.304)

The atlas contains 95 printable ASCII glyphs. `atlas.rgb` is a tightly packed 236 × 236 RGB8 image with top-down rows. `layout.json` is its msdf-atlas-gen metadata. `atlas.gmd2` is the RGB8 archive produced by the sample CLI with Zstd level 22.

## Regenerate

The fixture was generated with msdf-atlas-gen 1.3.0, MSDFgen 1.13.0, and Skia. The source checkout was at [c76a323](https://github.com/Chlumsky/msdf-atlas-gen/commit/c76a32319934c39e51a8c4838240d7b2362b0882). The SHA-256 of the upstream TTF is `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f`.

Run these commands from the repository root with `msdf-atlas-gen` on your PATH.

```sh
font_dir=$(mktemp -d)
font="$font_dir/JetBrainsMono-Regular.ttf"
fixture=tests/fixtures/jetbrains-mono
curl -fL 'https://raw.githubusercontent.com/JetBrains/JetBrainsMono/v2.304/fonts/ttf/JetBrainsMono-Regular.ttf' -o "$font"
msdf-atlas-gen -font "$font" -fontname "JetBrains Mono Regular" \
  -chars '[0x20,0x7E]' -type msdf -size 32 -pxrange 4 -square4 -yorigin top \
  -threads 1 -seed 0 -format bin -imageout "$fixture/atlas.rgb" -json "$fixture/layout.json"
make gmd2tool
./gmd2tool encode "$fixture/atlas.rgb" "$fixture/layout.json" "$fixture/atlas.gmd2" 22
make check
```

## Full atlas

`full/atlas.rgb` is a tightly packed 1088 × 1088 RGB8 atlas with top-down rows. `full/layout.json` contains all 1,743 glyph records. Both use the same font and generator settings as the ASCII fixture, with `-allglyphs` selecting the glyphs.

With `font` and `fixture` set as above, run the following from the repository root.

```sh
mkdir -p "$fixture/full"
msdf-atlas-gen -font "$font" -fontname "JetBrains Mono Regular" \
  -allglyphs -type msdf -size 32 -pxrange 4 -square4 -yorigin top \
  -threads 1 -seed 0 -format bin \
  -imageout "$fixture/full/atlas.rgb" -json "$fixture/full/layout.json"
```

To regenerate `full/atlas.webp`, rerun the atlas command with `-format png` and `-imageout "$font_dir/atlas.png"`, then run the lossless WebP encoder.

```sh
cwebp -lossless -z 9 -exact "$font_dir/atlas.png" -o "$fixture/full/atlas.webp"
```
