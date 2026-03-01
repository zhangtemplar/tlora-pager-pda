#!/usr/bin/env python3
"""
Generate placeholder LVGL icon C files (32x32 solid-color squares)
for both LVGL v8 and LVGL v9 formats.

v8: img_<name>.c  with LV_IMG_CF_TRUE_COLOR_ALPHA, 4 color-depth sections
v9: img_<name>_v9.c with LV_COLOR_FORMAT_RGB565A8
"""

import os
import struct

W = 32
H = 32
TOTAL_PIXELS = W * H

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')

# Icon definitions: (name, R, G, B)
ICONS = [
    ("calculator",  0x33, 0x66, 0xFF),   # blue
    ("dictionary",  0x33, 0xCC, 0x33),   # green
    ("voice_ai",    0x99, 0x33, 0xFF),   # purple
    ("weather",     0xFF, 0x99, 0x33),   # orange
    ("inet_radio",  0xFF, 0x33, 0x33),   # red
]

BYTES_PER_LINE = 16  # hex bytes per output line


def fmt_hex_line(data, per_line=BYTES_PER_LINE):
    """Format a list of byte values as hex lines like '  0x26, 0x26, ...'"""
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        line = '  ' + ', '.join(f'0x{b:02x}' for b in chunk) + ','
        lines.append(line)
    return '\n'.join(lines)


def rgb_to_rgb332(r, g, b):
    """Convert 8-bit RGB to RGB332 (1 byte)."""
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)


def rgb_to_rgb565(r, g, b):
    """Convert 8-bit RGB to RGB565 (2 bytes, little-endian: low byte first)."""
    val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    lo = val & 0xFF
    hi = (val >> 8) & 0xFF
    return lo, hi


def generate_v8(name, r, g, b):
    """Generate LVGL v8 icon C file content."""
    upper_name = name.upper()
    map_name = f'img_{name}_map'
    var_name = f'img_{name}'

    # --- Section 1: LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8 ---
    # Format: color332, alpha pairs (2 bytes per pixel)
    c332 = rgb_to_rgb332(r, g, b)
    data_8bit = []
    for _ in range(TOTAL_PIXELS):
        data_8bit.extend([c332, 0xFF])

    # --- Section 2: LV_COLOR_DEPTH == 16, no swap ---
    # Format: colorL, colorH, alpha (3 bytes per pixel)
    lo, hi = rgb_to_rgb565(r, g, b)
    data_16bit = []
    for _ in range(TOTAL_PIXELS):
        data_16bit.extend([lo, hi, 0xFF])

    # --- Section 3: LV_COLOR_DEPTH == 16, swap ---
    # Format: colorH, colorL, alpha (3 bytes per pixel)
    data_16bit_swap = []
    for _ in range(TOTAL_PIXELS):
        data_16bit_swap.extend([hi, lo, 0xFF])

    # --- Section 4: LV_COLOR_DEPTH == 32 ---
    # Format: B, G, R, A (4 bytes per pixel, BGRA order)
    data_32bit = []
    for _ in range(TOTAL_PIXELS):
        data_32bit.extend([b, g, r, 0xFF])

    content = f"""
#include "lvgl.h"

#if LVGL_VERSION_MAJOR == 8

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_IMG_{upper_name}
#define LV_ATTRIBUTE_IMG_IMG_{upper_name}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_IMG_{upper_name} uint8_t {map_name}[] = {{
#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8
  /*Pixel format: Alpha 8 bit, Red: 3 bit, Green: 3 bit, Blue: 2 bit*/
{fmt_hex_line(data_8bit)}
#endif
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit*/
{fmt_hex_line(data_16bit)}
#endif
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP != 0
  /*Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit  BUT the 2  color bytes are swapped*/
{fmt_hex_line(data_16bit_swap)}
#endif
#if LV_COLOR_DEPTH == 32
{fmt_hex_line(data_32bit)}
#endif
}};

const lv_img_dsc_t {var_name} = {{
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {W},
  .header.h = {H},
  .data_size = {TOTAL_PIXELS} * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .data = {map_name},
}};

#endif
"""
    return content


def generate_v9(name, r, g, b):
    """Generate LVGL v9 icon C file content."""
    upper_name = name.upper()
    map_name = f'img_{name}_map'
    var_name = f'img_{name}'

    # RGB565A8 format: first all RGB565 pixels (2 bytes each), then all alpha bytes (1 byte each)
    # stride = W * 2 = 64
    stride = W * 2
    lo, hi = rgb_to_rgb565(r, g, b)

    data = []
    # RGB565 pixel data
    for _ in range(TOTAL_PIXELS):
        data.extend([lo, hi])
    # Alpha data
    for _ in range(TOTAL_PIXELS):
        data.append(0xFF)

    content = f"""
#include "lvgl.h"

#if LVGL_VERSION_MAJOR == 9
#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_{upper_name}
#define LV_ATTRIBUTE_IMG_{upper_name}
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_{upper_name}
uint8_t {map_name}[] = {{

{fmt_hex_line(data)}

}};

const lv_image_dsc_t {var_name} = {{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_RGB565A8,
  .header.flags = 0,
  .header.w = {W},
  .header.h = {H},
  .header.stride = {stride},
  .data_size = sizeof({map_name}),
  .data = {map_name},
}};

#endif
"""
    return content


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for name, r, g, b in ICONS:
        # Generate v8 file
        v8_path = os.path.join(OUTPUT_DIR, f'img_{name}.c')
        v8_content = generate_v8(name, r, g, b)
        with open(v8_path, 'w') as f:
            f.write(v8_content)
        print(f"Generated: {v8_path}")

        # Generate v9 file
        v9_path = os.path.join(OUTPUT_DIR, f'img_{name}_v9.c')
        v9_content = generate_v9(name, r, g, b)
        with open(v9_path, 'w') as f:
            f.write(v9_content)
        print(f"Generated: {v9_path}")

    print(f"\nDone! Generated {len(ICONS) * 2} files in {os.path.abspath(OUTPUT_DIR)}")


if __name__ == '__main__':
    main()
