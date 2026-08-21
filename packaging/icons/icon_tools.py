#!/usr/bin/env python3
"""OpenTIE icon asset pipeline.

Regenerates every committed, derived icon asset from the master image using
only the Python standard library, so it runs identically on any platform:

    icon_tools.py

Master input:
    packaging/icons/opentie-icon.png       1024x1024 RGBA PNG

Derived outputs (committed to the repository):
    cmake/windows/tie.ico                  Windows executable icon
    cmake/macos/TIE.icns                   macOS bundle icon
    src/tie_app/window_icon.h              embedded BMP passed to Aeron for
                                           SDL_SetWindowIcon

The PNG codec supports 8-bit RGB/RGBA non-interlaced images. Resizing is an
alpha-weighted box filter over integer factors, which is exact for the
power-of-two sizes derived from a 1024px master.
"""

import os
import struct
import sys
import zlib

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
MASTER = os.path.join(REPO_ROOT, "packaging", "icons", "opentie-icon.png")
ICO_OUT = os.path.join(REPO_ROOT, "cmake", "windows", "tie.ico")
ICNS_OUT = os.path.join(REPO_ROOT, "cmake", "macos", "TIE.icns")
HEADER_OUT = os.path.join(REPO_ROOT, "src", "tie_app", "window_icon.h")

MASTER_SIZE = 1024
ICO_SIZES = [16, 32, 64, 128, 256]
ICNS_TYPES = [
    (b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128),
    (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024), (b"ic11", 32),
    (b"ic12", 64), (b"ic13", 256), (b"ic14", 512),
]
WINDOW_ICON_SIZE = 64


def png_encode(width, height, rgba):
    def chunk(tag, data):
        payload = tag + data
        return struct.pack(">I", len(data)) + payload + struct.pack(
            ">I", zlib.crc32(payload) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def png_decode(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos = 8
    width = height = color_type = None
    idat = bytearray()
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if depth != 8 or color_type not in (2, 6) or interlace:
                raise ValueError(
                    "master must be an 8-bit RGB or RGBA non-interlaced PNG")
        elif tag == b"IDAT":
            idat.extend(payload)
        elif tag == b"IEND":
            break
    channels = 4 if color_type == 6 else 3
    stride = width * channels
    raw = zlib.decompress(bytes(idat))

    out = bytearray(height * stride)
    previous = bytearray(stride)
    pos = 0
    for y in range(height):
        filter_type = raw[pos]
        row = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if filter_type == 1:  # Sub
            for i in range(channels, stride):
                row[i] = (row[i] + row[i - channels]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                up = previous[i]
                up_left = previous[i - channels] if i >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                if pa <= pb and pa <= pc:
                    predictor = left
                elif pb <= pc:
                    predictor = up
                else:
                    predictor = up_left
                row[i] = (row[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"unsupported PNG filter {filter_type}")
        out[y * stride:(y + 1) * stride] = row
        previous = row

    if channels == 3:
        rgba = bytearray(width * height * 4)
        for i in range(width * height):
            rgba[i * 4:i * 4 + 3] = out[i * 3:i * 3 + 3]
            rgba[i * 4 + 3] = 255
        out = rgba
    return width, height, bytes(out)


def resize_box(rgba, src_size, dst_size):
    if src_size == dst_size:
        return rgba
    if src_size % dst_size:
        raise ValueError(f"{src_size} is not an integer multiple of {dst_size}")
    factor = src_size // dst_size
    area = factor * factor
    out = bytearray(dst_size * dst_size * 4)
    for dy in range(dst_size):
        for dx in range(dst_size):
            red = green = blue = alpha = 0
            for sy in range(dy * factor, (dy + 1) * factor):
                base = (sy * src_size + dx * factor) * 4
                for _ in range(factor):
                    pixel_alpha = rgba[base + 3]
                    red += rgba[base] * pixel_alpha
                    green += rgba[base + 1] * pixel_alpha
                    blue += rgba[base + 2] * pixel_alpha
                    alpha += pixel_alpha
                    base += 4
            offset = (dy * dst_size + dx) * 4
            if alpha:
                out[offset] = red // alpha
                out[offset + 1] = green // alpha
                out[offset + 2] = blue // alpha
            out[offset + 3] = alpha // area
    return bytes(out)


def build_ico(images):
    header = struct.pack("<HHH", 0, 1, len(images))
    entries = b""
    blobs = b""
    offset = 6 + 16 * len(images)
    for size, png in images:
        entries += struct.pack(
            "<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset)
        blobs += png
        offset += len(png)
    return header + entries + blobs


def build_icns(entries):
    body = b""
    for tag, png in entries:
        body += tag + struct.pack(">I", 8 + len(png)) + png
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def build_bmp(size, rgba):
    # BITMAPV4HEADER with BI_BITFIELDS alpha masks, which SDL_LoadBMP honors.
    row_stride = size * 4
    pixels = bytearray()
    for y in range(size - 1, -1, -1):  # bottom-up
        row = rgba[y * row_stride:(y + 1) * row_stride]
        for x in range(size):
            r, g, b, a = row[x * 4:x * 4 + 4]
            pixels += bytes((b, g, r, a))
    info = struct.pack(
        "<IiiHHIIiiII4I4s36xIII",
        108, size, size, 1, 32, 3, len(pixels), 2835, 2835, 0, 0,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, b"BGRs", 0, 0, 0)
    file_header = struct.pack("<2sIHHI", b"BM", 14 + len(info) + len(pixels),
                              0, 0, 14 + len(info))
    return file_header + info + pixels


def emit_header(bmp):
    lines = [
        "/* Generated by packaging/icons/icon_tools.py - do not edit.",
        " * Window/taskbar icon embedded as a 32-bit BMP, passed to Aeron",
        " * through AeronConfig; regenerate after changing the master image. */",
        "",
        f"static const unsigned char tie_window_icon_bmp[{len(bmp)}] = {{",
    ]
    for i in range(0, len(bmp), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in bmp[i:i + 12])
        lines.append(f"\t{chunk},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def command_generate():
    with open(MASTER, "rb") as f:
        width, height, rgba = png_decode(f.read())
    if width != MASTER_SIZE or height != MASTER_SIZE:
        raise SystemExit(f"master image must be {MASTER_SIZE}x{MASTER_SIZE}")

    resized = {}
    for size in sorted({s for s in ICO_SIZES}
                       | {s for _, s in ICNS_TYPES}
                       | {WINDOW_ICON_SIZE, MASTER_SIZE}):
        resized[size] = png_encode(size, size, resize_box(rgba, MASTER_SIZE, size)) \
            if size != MASTER_SIZE else png_encode(size, size, rgba)

    with open(ICO_OUT, "wb") as f:
        f.write(build_ico([(s, resized[s]) for s in ICO_SIZES]))
    with open(ICNS_OUT, "wb") as f:
        f.write(build_icns([(tag, resized[s]) for tag, s in ICNS_TYPES]))
    with open(HEADER_OUT, "w") as f:
        f.write(emit_header(build_bmp(
            WINDOW_ICON_SIZE,
            resize_box(rgba, MASTER_SIZE, WINDOW_ICON_SIZE))))
    print(f"wrote {ICO_OUT}")
    print(f"wrote {ICNS_OUT}")
    print(f"wrote {HEADER_OUT}")


def main():
    if len(sys.argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    command_generate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
