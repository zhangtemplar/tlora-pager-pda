#!/usr/bin/env python3
"""Generate unified black-silhouette app icons (80x80) matching original LilyGo style.
Draws at 4x (320x320) then downscales for anti-aliasing.
Outputs both PNGs and LVGL 9 RGB565A8 C arrays."""

from PIL import Image, ImageDraw, ImageFont
import struct, os, math

SIZE = 80
SCALE = 4
S = SIZE * SCALE  # 320
DIR = os.path.dirname(os.path.abspath(__file__))
IMG_DIR = os.path.join(DIR, "images")
SRC_DIR = os.path.join(DIR, "src")

def new_canvas():
    return Image.new("RGBA", (S, S), (0, 0, 0, 0))

def finalize(img):
    return img.resize((SIZE, SIZE), Image.LANCZOS)

# --- Voice AI: microphone + sparkles (AI) ---
def draw_voice_ai():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    cx, cy = S // 2, S // 2
    # Mic body (rounded rect)
    mw, mh = 80, 120
    mx = cx - mw // 2
    my = cy - mh // 2 - 20
    d.rounded_rectangle([mx, my, mx + mw, my + mh], radius=mw // 2, fill="black")
    # Mic arc (U shape around mic)
    arc_w = 130
    arc_l = cx - arc_w // 2
    arc_t = my + 20
    d.arc([arc_l, arc_t, arc_l + arc_w * 2, arc_t + arc_w * 2], 180, 0, fill="black", width=12)
    # Fix: only draw the bottom half arc
    d.rectangle([arc_l - 10, arc_t, arc_l + arc_w * 2 + 10, arc_t + 10], fill=(0, 0, 0, 0))
    img2 = new_canvas()
    d2 = ImageDraw.Draw(img2)
    # Redraw properly
    # Mic head (pill shape)
    mw, mh = 76, 110
    mx = cx - mw // 2
    my = 45
    d2.rounded_rectangle([mx, my, mx + mw, my + mh], radius=mw // 2, fill="black")
    # Mic grill lines
    for i in range(3):
        ly = my + 30 + i * 24
        d2.line([mx + 14, ly, mx + mw - 14, ly], fill=(80, 80, 80), width=4)
    # Arc around mic
    arc_pad = 20
    arc_box = [mx - arc_pad, my + 30, mx + mw + arc_pad, my + mh + arc_pad * 3]
    d2.arc(arc_box, 0, 180, fill="black", width=12)
    # Stem
    stem_x = cx
    stem_top = my + mh + arc_pad
    stem_bot = stem_top + 40
    d2.line([stem_x, stem_top, stem_x, stem_bot], fill="black", width=12)
    # Base
    base_w = 70
    d2.line([stem_x - base_w // 2, stem_bot, stem_x + base_w // 2, stem_bot], fill="black", width=12)
    # AI sparkles (4-pointed stars)
    def draw_star(cx, cy, r):
        pts = []
        for i in range(8):
            angle = i * math.pi / 4
            rr = r if i % 2 == 0 else r * 0.3
            pts.append((cx + rr * math.cos(angle - math.pi/2),
                        cy + rr * math.sin(angle - math.pi/2)))
        d2.polygon(pts, fill="black")
    draw_star(cx + 85, 60, 30)
    draw_star(cx + 110, 120, 18)
    draw_star(cx + 65, 30, 14)
    return finalize(img2)

# --- Calculator: outline with display + button grid ---
def draw_calculator():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    pad = 40
    r = 24
    # Body
    d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=r, outline="black", width=14)
    # Display area
    dp = 22
    d.rounded_rectangle([pad + dp, pad + dp, S - pad - dp, pad + 80], radius=10, fill="black")
    # Button grid 4x3
    bx0 = pad + dp + 5
    by0 = pad + 95
    bw = (S - 2 * pad - 2 * dp - 30) // 3
    bh = (S - pad - by0 - dp - 10) // 4
    gap = 8
    for row in range(4):
        for col in range(3):
            x = bx0 + col * (bw + gap)
            y = by0 + row * (bh + gap)
            d.rounded_rectangle([x, y, x + bw, y + bh], radius=6, fill="black")
    return finalize(img)

# --- Dictionary: book with "Aa" ---
def draw_dictionary():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    # Book outline
    bx, by = 50, 30
    bw, bh = S - 100, S - 60
    d.rounded_rectangle([bx, by, bx + bw, by + bh], radius=12, outline="black", width=14)
    # Spine
    spine_x = bx + 30
    d.line([spine_x, by + 10, spine_x, by + bh - 10], fill="black", width=10)
    # "Aa" text - use a large font if available, otherwise draw manually
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 100)
        d.text((spine_x + 35, by + bh // 2 - 55), "Aa", fill="black", font=font)
    except:
        # Manual A drawing
        ax, ay = cx, cy
        # Just draw two A shapes
        d.polygon([(150, 200), (180, 100), (210, 200)], outline="black", width=8)
        d.line([160, 170, 200, 170], fill="black", width=6)
    return finalize(img)

# --- Weather: sun + cloud ---
def draw_weather():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    # Sun (upper-left area)
    sun_cx, sun_cy, sun_r = 120, 110, 45
    d.ellipse([sun_cx - sun_r, sun_cy - sun_r, sun_cx + sun_r, sun_cy + sun_r], fill="black")
    # Sun rays
    ray_len = 28
    ray_gap = 8
    for i in range(8):
        angle = i * math.pi / 4
        x1 = sun_cx + (sun_r + ray_gap) * math.cos(angle)
        y1 = sun_cy + (sun_r + ray_gap) * math.sin(angle)
        x2 = sun_cx + (sun_r + ray_gap + ray_len) * math.cos(angle)
        y2 = sun_cy + (sun_r + ray_gap + ray_len) * math.sin(angle)
        d.line([x1, y1, x2, y2], fill="black", width=12)
    # Cloud (overlapping ellipses, covers lower-right of sun)
    cloud_y = 190
    # Main cloud body
    d.ellipse([100, cloud_y - 40, 230, cloud_y + 50], fill="black")
    d.ellipse([150, cloud_y - 70, 270, cloud_y + 20], fill="black")
    d.ellipse([60, cloud_y - 10, 160, cloud_y + 60], fill="black")
    d.ellipse([190, cloud_y - 30, 290, cloud_y + 40], fill="black")
    # Flat bottom
    d.rectangle([65, cloud_y + 30, 285, cloud_y + 55], fill="black")
    return finalize(img)

# --- Internet Radio: antenna tower with signal waves ---
def draw_inet_radio():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    cx = S // 2
    # Antenna mast
    d.line([cx, 80, cx, 240], fill="black", width=14)
    # Antenna top circle
    d.ellipse([cx - 14, 60, cx + 14, 88], fill="black")
    # Signal arcs (left and right)
    for side in [-1, 1]:
        for i, r in enumerate([40, 70, 100]):
            arc_cx = cx + side * 5
            box = [arc_cx - r, 80 - r, arc_cx + r, 80 + r]
            if side == -1:
                d.arc(box, 180, 270, fill="black", width=12)
            else:
                d.arc(box, 270, 360, fill="black", width=12)
    # Base / tripod
    base_w = 90
    d.line([cx, 240, cx - base_w, S - 40], fill="black", width=12)
    d.line([cx, 240, cx + base_w, S - 40], fill="black", width=12)
    d.line([cx - base_w + 10, S - 40, cx + base_w - 10, S - 40], fill="black", width=10)
    return finalize(img)

# --- Calendar: calendar page with date ---
def draw_calendar():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    pad = 40
    # Body
    d.rounded_rectangle([pad, pad + 30, S - pad, S - pad], radius=16, outline="black", width=14)
    # Header bar (filled)
    d.rounded_rectangle([pad, pad + 30, S - pad, pad + 90], radius=16, fill="black")
    d.rectangle([pad, pad + 60, S - pad, pad + 90], fill="black")
    # Ring hooks
    for x_off in [100, 220]:
        d.rounded_rectangle([x_off - 10, pad, x_off + 10, pad + 50], radius=6, fill="black")
    # Date number
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 120)
        bbox = d.textbbox((0, 0), "8", font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        tx = (S - tw) // 2
        ty = pad + 95 + (S - pad - pad - 95 - 30 - th) // 2
        d.text((tx, ty), "8", fill="black", font=font)
    except:
        pass
    return finalize(img)

# --- PNG to LVGL 9 RGB565A8 C array ---
def rgb_to_565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def png_to_lvgl_c(img, name):
    """Convert 80x80 RGBA PIL Image to LVGL 9 RGB565A8 C source."""
    w, h = img.size
    pixels = list(img.getdata())

    # RGB565 data (2 bytes per pixel)
    rgb_data = []
    alpha_data = []
    for r, g, b, a in pixels:
        c565 = rgb_to_565(r, g, b)
        rgb_data.append(struct.pack("<H", c565))
        alpha_data.append(bytes([a]))

    NAME_UPPER = name.upper()

    lines = []
    lines.append(f"\n#include \"lvgl.h\"\n")
    lines.append(f"#if LVGL_VERSION_MAJOR == 9")
    lines.append(f"#ifndef LV_ATTRIBUTE_MEM_ALIGN")
    lines.append(f"#define LV_ATTRIBUTE_MEM_ALIGN")
    lines.append(f"#endif\n")
    lines.append(f"#ifndef LV_ATTRIBUTE_IMG_{NAME_UPPER}")
    lines.append(f"#define LV_ATTRIBUTE_IMG_{NAME_UPPER}")
    lines.append(f"#endif\n")
    lines.append(f"static const")
    lines.append(f"LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_{NAME_UPPER}")
    lines.append(f"uint8_t {name}_map[] = {{\n")

    # Write RGB565 data row by row
    for y in range(h):
        row_bytes = b"".join(rgb_data[y * w:(y + 1) * w])
        hex_vals = ",".join(f"0x{b:02x}" for b in row_bytes)
        lines.append(f"    {hex_vals},")

    # Write alpha data row by row
    lines.append("")  # blank line separator
    for y in range(h):
        row_bytes = b"".join(alpha_data[y * w:(y + 1) * w])
        hex_vals = ",".join(f"0x{b:02x}" for b in row_bytes)
        lines.append(f"    {hex_vals},")

    lines.append(f"\n}};\n")
    lines.append(f"const lv_image_dsc_t {name} = {{")
    lines.append(f"  .header.magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append(f"  .header.cf = LV_COLOR_FORMAT_RGB565A8,")
    lines.append(f"  .header.flags = 0,")
    lines.append(f"  .header.w = {w},")
    lines.append(f"  .header.h = {h},")
    lines.append(f"  .header.stride = {w * 2},")
    lines.append(f"  .data_size = sizeof({name}_map),")
    lines.append(f"  .data = {name}_map,")
    lines.append(f"}};\n")
    lines.append(f"#endif")

    return "\n".join(lines) + "\n"


def main():
    icons = {
        "img_voice_ai": draw_voice_ai,
        "img_calculator": draw_calculator,
        "img_dictionary": draw_dictionary,
        "img_weather": draw_weather,
        "img_inet_radio": draw_inet_radio,
        "img_calendar": draw_calendar,
    }

    for name, draw_fn in icons.items():
        print(f"Generating {name}...")
        img = draw_fn()

        # Save PNG
        png_path = os.path.join(IMG_DIR, f"{name}.png")
        img.save(png_path)
        print(f"  Saved {png_path}")

        # Generate LVGL v9 C array
        c_path = os.path.join(SRC_DIR, f"{name}_v9.c")
        c_src = png_to_lvgl_c(img, name)
        with open(c_path, "w") as f:
            f.write(c_src)
        print(f"  Saved {c_path}")

    print("\nDone! All icons regenerated.")

if __name__ == "__main__":
    main()
