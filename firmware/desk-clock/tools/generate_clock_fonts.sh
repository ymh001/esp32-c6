#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONT_PATH="${SOURCE_HAN_SANS_FONT:-/tmp/SourceHanSansSC-Regular.otf}"
FONT_URL="https://github.com/adobe-fonts/source-han-sans/raw/release/OTF/SimplifiedChinese/SourceHanSansSC-Regular.otf"
FONT_DIR="$ROOT_DIR/components/clock_ui/fonts"
GLYPHS_PATH="$FONT_DIR/clock_glyphs.txt"

if [[ ! -f "$FONT_PATH" ]]; then
    curl -L --fail --retry 3 "$FONT_URL" -o "$FONT_PATH"
fi
SOURCE_FILES=()
while IFS= read -r source_file; do
    SOURCE_FILES+=("$source_file")
done < <(find "$ROOT_DIR/main" "$ROOT_DIR/components" -type f \
    \( -name '*.cpp' -o -name '*.h' \) | sort)

ruby -e '
  chars = {}
  ARGV.each do |path|
    File.read(path, encoding: "UTF-8").scan(/[^\x00-\x7F]/) { |char| chars[char] = true }
  end
  print chars.keys.sort.join
' "${SOURCE_FILES[@]}" > "$GLYPHS_PATH"

SYMBOLS="$(cat "$GLYPHS_PATH")"
for size in 16 24 32 96; do
    npx --yes lv_font_conv \
        --font "$FONT_PATH" \
        --range 0x20-0x7F \
        --symbols "$SYMBOLS" \
        --size "$size" \
        --bpp 4 \
        --format lvgl \
        --no-compress \
        --lv-include lvgl.h \
        --lv-font-name "clock_cjk_$size" \
        -o "$FONT_DIR/clock_cjk_$size.c"
done
