#!/usr/bin/env python3
"""
重新生成中文字库 (font_noto_16.c)

用法：
  python scripts/regenerate_font.py [--add "新增的中文字符"]

  不加参数：用 font_symbols.txt 里已有的字符重新生成
  --add：在现有字符基础上追加新字符再生成
"""

import sys, os, subprocess

SYMBOLS_FILE = 'scripts/font_symbols.txt'
FONT_TTF     = 'scripts/NotoSansSC.ttf'
OUTPUT       = 'main/assets/font_noto_16.c'

def main():
    # 读取现有字符
    if os.path.exists(SYMBOLS_FILE):
        with open(SYMBOLS_FILE, 'r', encoding='utf-8') as f:
            chars = set(f.read().strip())
    else:
        chars = set()

    # 追加新字符
    if '--add' in sys.argv:
        idx = sys.argv.index('--add')
        new_chars = sys.argv[idx + 1] if idx + 1 < len(sys.argv) else ''
        for c in new_chars:
            chars.add(c)
        with open(SYMBOLS_FILE, 'w', encoding='utf-8') as f:
            f.write(''.join(sorted(chars)))
        print(f'Added {len(new_chars)} chars, total {len(chars)}')

    symbols = ''.join(sorted(chars))
    if not symbols:
        print('Error: no symbols')
        sys.exit(1)

    # 检查字体
    if not os.path.exists(FONT_TTF):
        print(f'Error: font not found at {FONT_TTF}')
        print('Download Noto Sans SC from Google Fonts and place it there')
        sys.exit(1)

    # 生成
    print(f'Generating font with {len(symbols)} symbols...')
    result = subprocess.run([
        'npx', 'lv_font_conv',
        '--font', FONT_TTF,
        '--size', '16',
        '--bpp', '4',
        '--format', 'lvgl',
        '--lv-include', 'lvgl.h',
        '-o', OUTPUT,
        '--symbols', symbols
    ], capture_output=True, text=True)

    if result.returncode == 0:
        size = os.path.getsize(OUTPUT)
        print(f'OK: {OUTPUT} ({size} bytes)')
    else:
        print('Failed:')
        print(result.stderr)

if __name__ == '__main__':
    main()
