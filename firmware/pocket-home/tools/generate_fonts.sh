#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
regular="${SOURCE_HAN_REGULAR:-/tmp/SourceHanSansSC-Regular.otf}"
bold="${SOURCE_HAN_BOLD:-/tmp/SourceHanSansSC-Bold.otf}"
for weight in Regular Bold; do
    font_path="/tmp/SourceHanSansSC-${weight}.otf"
    if [[ ! -f "$font_path" ]]; then
        curl -L --fail "https://raw.githubusercontent.com/adobe-fonts/source-han-sans/release/OTF/SimplifiedChinese/SourceHanSansSC-${weight}.otf" -o "$font_path"
    fi
done
symbols="$(python3 - "$project_dir/main" <<'PYFONT'
from pathlib import Path
import sys
print(''.join(sorted({c for p in Path(sys.argv[1]).rglob('*.cpp') for c in p.read_text() if ord(c)>127})))
PYFONT
)"
npx --yes lv_font_conv@1.5.3 --font "$regular" --range '0x20-0x7F,0xA0-0xFF,0x2000-0x206F,0x3000-0x303F,0xFF00-0xFFEF,0x4E00-0x9FFF' --symbols "$symbols" --size 16 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name clock_cjk_16 -o "$project_dir/main/fonts/clock_cjk_16.c"
npx --yes lv_font_conv@1.5.3 --font "$regular" --range 0x20-0x7F --symbols "$symbols" --size 24 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name pocket_text_24 -o "$project_dir/main/fonts/pocket_text_24.c"
python3 "$project_dir/tools/generate_flip_tiles.py" --font "$bold"
python3 - "$project_dir" <<'PYGLYPHS'
from pathlib import Path
import sys,re
root=Path(sys.argv[1]);chars=set()
for p in (root/'main').rglob('*.cpp'):
    for value in re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"',p.read_text()):
        chars.update(ord(c) for c in value if ord(c)>127)
(root/'host_tests/ui_codepoints.h').write_text('// Generated from firmware UI strings.\nstatic const uint32_t ui_codepoints[] = {'+','.join(hex(c) for c in sorted(chars))+'};\n')
PYGLYPHS
