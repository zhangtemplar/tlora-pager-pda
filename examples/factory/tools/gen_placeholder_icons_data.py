#!/usr/bin/env python3
"""
Generate placeholder LVGL icon C files (32x32 solid-color squares)
for both LVGL v8 and LVGL v9 formats.

Usage: python3 gen_placeholder_icons_data.py
Output: C files written to ../src/
"""

import os

W = 32
H = 32
TOTAL_PIXELS = W * H

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, '..', 'src')

# Icon definitions: (name, R, G, B)
ICONS = [
    ("calculator",  0x33, 0x66, 0xFF),
    ("dictionary",  0x33, 0xCC, 0x33),
    ("voice_ai",    0x99, 0x33, 0xFF),
    ("weather",     0xFF, 0x99, 0x33),
    ("inet_radio",  0xFF, 0x33, 0x33),
]

BYTES_PER_LINE = 16


def fmt_hex(data, per_line=BYTES_PER_LINE):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        line = '  ' + ', '.join('0x{:02x}'.format(b) for b in chunk) + ','
        lines.append(line)
    return '\n'.join(lines)


def rgb332(r, g, b):
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)


def rgb565(r, g, b):
    val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return val & 0xFF, (val >> 8) & 0xFF  # lo, hi


def gen_v8(name, r, g, b):
    upper = name.upper()
    mapn = 'img_{}_map'.format(name)
    varn = 'img_{}'.format(name)

    c8 = rgb332(r, g, b)
    d8 = []
    for _ in range(TOTAL_PIXELS):
        d8.extend([c8, 0xFF])

    lo, hi = rgb565(r, g, b)
    d16 = []
    for _ in range(TOTAL_PIXELS):
        d16.extend([lo, hi, 0xFF])

    d16s = []
    for _ in range(TOTAL_PIXELS):
        d16s.extend([hi, lo, 0xFF])

    d32 = []
    for _ in range(TOTAL_PIXELS):
        d32.extend([b, g, r, 0xFF])

    return """\

#include "lvgl.h"

#if LVGL_VERSION_MAJOR == 8

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_IMG_{upper}
#define LV_ATTRIBUTE_IMG_IMG_{upper}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_IMG_{upper} uint8_t {mapn}[] = {{
#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8
  /*Pixel format: Alpha 8 bit, Red: 3 bit, Green: 3 bit, Blue: 2 bit*/
{s8}
#endif
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit*/
{s16}
#endif
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP != 0
  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit  BUT the 2  color bytes are swapped*/
{s16s}
#endif
#if LV_COLOR_DEPTH == 32
{s32}
#endif
}};

const lv_img_dsc_t {varn} = {{
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {tp} * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .data = {mapn},
}};

#endif
""".format(
        upper=upper, mapn=mapn, varn=varn,
        s8=fmt_hex(d8), s16=fmt_hex(d16), s16s=fmt_hex(d16s), s32=fmt_hex(d32),
        w=W, h=H, tp=TOTAL_PIXELS
    )


def gen_v9(name, r, g, b):
    upper = name.upper()
    mapn = 'img_{}_map'.format(name)
    varn = 'img_{}'.format(name)
    stride = W * 2
    lo, hi = rgb565(r, g, b)

    data = []
    for _ in range(TOTAL_PIXELS):
        data.extend([lo, hi])
    for _ in range(TOTAL_PIXELS):
        data.append(0xFF)

    return """\

#include "lvgl.h"

#if LVGL_VERSION_MAJOR == 9
#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_{upper}
#define LV_ATTRIBUTE_IMG_{upper}
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_{upper}
uint8_t {mapn}[] = {{

{sdata}

}};

const lv_image_dsc_t {varn} = {{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_RGB565A8,
  .header.flags = 0,
  .header.w = {w},
  .header.h = {h},
  .header.stride = {stride},
  .data_size = sizeof({mapn}),
  .data = {mapn},
}};

#endif
""".format(
        upper=upper, mapn=mapn, varn=varn,
        sdata=fmt_hex(data),
        w=W, h=H, stride=stride
    )


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for name, r, g, b in ICONS:
        v8_path = os.path.join(OUTPUT_DIR, 'img_{}.c'.format(name))
        with open(v8_path, 'w') as f:
            f.write(gen_v8(name, r, g, b))
        print('Generated: {}'.format(v8_path))

        v9_path = os.path.join(OUTPUT_DIR, 'img_{}_v9.c'.format(name))
        with open(v9_path, 'w') as f:
            f.write(gen_v9(name, r, g, b))
        print('Generated: {}'.format(v9_path))

    print('\nDone! Generated {} files in {}'.format(len(ICONS) * 2, os.path.abspath(OUTPUT_DIR)))


if __name__ == '__main__':
    main()
