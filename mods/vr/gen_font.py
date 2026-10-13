#!/usr/bin/env python3
"""Gera mods/vr/vr_font.h — atlas bitmap da fonte do painel de debug do mod_vr.

O painel é desenhado direto em OpenGL pelo Mod (que não pode linkar contra o
engine), então a fonte precisa vir embutida. Fonte *bitmap* (não vetorial) =
textura 1:1 com GL_NEAREST, sem borrão e sem depender de FreeType no Mod.

Origem dos glifos: fonte de console PSF do sistema (kbd), 8x16, com tabela
Unicode — VGA-like, legível e de domínio público. ASCII imprimível (32..126)
vira uma grade 16x6 de células 8x16.

Uso:
    python3 mods/vr/gen_font.py            # escreve vr_font.h + vr_font_preview.png
    python3 mods/vr/gen_font.py --font /usr/lib/kbd/consolefonts/outra.psfu.gz

O .h é gerado; não editar à mão (regenerar é barato e auditável).
"""
import argparse
import gzip
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# PSF1/PSF2 magic
PSF1_MAGIC = b"\x36\x04"
PSF2_MAGIC = b"\x72\xb5\x4a\x86"

FIRST, LAST = 32, 126          # ASCII imprimível
GRID_COLS, GRID_ROWS = 16, 6   # 96 células >= 95 glifos


def _read_bytes(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        return gzip.decompress(raw)
    return raw


def parse_psf(data):
    """→ (glyphs, width, height, unicode_map)

    glyphs: lista de bytes (1 por linha, bit mais alto = pixel mais à esquerda)
    unicode_map: dict codepoint → índice do glifo
    """
    width = height = None
    glyphs = []
    unicode_map = {}

    if data[:4] == PSF2_MAGIC:
        (magic, version, headersize, flags, length, charsize, height, width) = (
            data[0:4], int.from_bytes(data[4:8], "little"), int.from_bytes(data[8:12], "little"),
            int.from_bytes(data[12:16], "little"), int.from_bytes(data[16:20], "little"),
            int.from_bytes(data[20:24], "little"), int.from_bytes(data[24:28], "little"),
            int.from_bytes(data[28:32], "little"),
        )
        rowbytes = (width + 7) // 8
        base = headersize
        for i in range(length):
            g = data[base + i * charsize: base + (i + 1) * charsize]
            glyphs.append([g[r * rowbytes] for r in range(height)])
        if flags & 1:
            # tabela Unicode do PSF2: por glifo, uma sequência UTF-8 de
            # codepoints terminada em 0xFF (formato do kbd/Linux, NÃO são
            # u16 — ler como u16 dá lixo)
            p = base + length * charsize
            for i in range(length):
                buf = bytearray()
                while p < len(data):
                    b = data[p]
                    p += 1
                    if b == 0xFF:
                        break
                    buf.append(b)
                s = bytes(buf).decode("utf-8", "replace")
                if s:
                    unicode_map.setdefault(ord(s[0]), i)
    elif data[:2] == PSF1_MAGIC:
        mode = data[2]
        charsize = data[3]
        length = 512 if (mode & 1) else 256
        width, height = 8, charsize
        base = 4
        for i in range(length):
            g = data[base + i * charsize: base + (i + 1) * charsize]
            glyphs.append(list(g))
        if mode & 2:  # tabela Unicode
            ubase = base + length * charsize
            for i in range(length):
                cp = data[ubase + i * 2] | (data[ubase + i * 2 + 1] << 8)
                if cp != 0xFFFF:
                    unicode_map.setdefault(cp, i)
    else:
        raise SystemExit("fonte não é PSF1 nem PSF2")

    if not glyphs:
        raise SystemExit("fonte sem glifos")
    return glyphs, width, height, unicode_map


def build_atlas(font_path):
    glyphs, w, h, umap = parse_psf(_read_bytes(font_path))
    question = umap.get(ord("?"), 0)

    atlas_w, atlas_h = w * GRID_COLS, h * GRID_ROWS
    atlas = bytearray(atlas_w * atlas_h)  # 0/255 = alpha

    missing = []
    for cp in range(FIRST, LAST + 1):
        idx = umap.get(cp)
        if idx is None and cp < len(glyphs):
            idx = cp  # fontes de console (cp437/cp850/latin1) usam ASCII na ordem
        if idx is None:
            idx = question
            if cp != ord("?"):
                missing.append(chr(cp))
        g = glyphs[idx]
        cell = cp - FIRST
        ox, oy = (cell % GRID_COLS) * w, (cell // GRID_COLS) * h
        for y in range(h):
            row = g[y] if y < len(g) else 0
            for x in range(w):
                if row & (0x80 >> x):
                    atlas[(oy + y) * atlas_w + (ox + x)] = 255

    return atlas, atlas_w, atlas_h, w, h, missing


def emit_header(atlas, aw, ah, w, h, font_path, out_path):
    name = os.path.basename(font_path)
    lines = []
    lines.append("/* gerado por mods/vr/gen_font.py — NÃO EDITAR À MÃO.")
    lines.append(" * Fonte bitmap 8x16 do painel de debug do mod_vr.")
    lines.append(" * origem: %s (ASCII %d..%d, grade %dx%d)" % (name, FIRST, LAST, GRID_COLS, GRID_ROWS))
    lines.append(" */")
    lines.append("#ifndef VR_FONT_GENERATED_H")
    lines.append("#define VR_FONT_GENERATED_H")
    lines.append("")
    lines.append("#define VR_FONT_FIRST   %d" % FIRST)
    lines.append("#define VR_FONT_LAST    %d" % LAST)
    lines.append("#define VR_FONT_W       %d" % w)
    lines.append("#define VR_FONT_H       %d" % h)
    lines.append("#define VR_FONT_COLS    %d" % GRID_COLS)
    lines.append("#define VR_FONT_ROWS    %d" % GRID_ROWS)
    lines.append("#define VR_FONT_ATLAS_W %d" % aw)
    lines.append("#define VR_FONT_ATLAS_H %d" % ah)
    lines.append("")
    lines.append("/* um byte por texel: 0 = transparente, 255 = opaco */")
    lines.append("static const unsigned char vr_font_atlas[VR_FONT_ATLAS_W * VR_FONT_ATLAS_H] = {")
    for y in range(ah):
        row = atlas[y * aw:(y + 1) * aw]
        lines.append("\t" + ",".join(("%3d" % v) for v in row) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* VR_FONT_GENERATED_H */")
    lines.append("")
    open(out_path, "w").write("\n".join(lines))


def emit_preview(atlas, aw, ah, w, h, out_path):
    """PNG de conferência: atlas ampliado + uma amostra de texto do painel."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("(PIL ausente: preview não gerado)")
        return

    scale = 3
    pad = 8
    img = Image.new("RGB", (aw * scale + pad * 2, ah * scale + pad * 2 + 40 * 3), (16, 26, 36))
    px = img.load()
    for y in range(ah):
        for x in range(aw):
            if atlas[y * aw + x]:
                for dy in range(scale):
                    for dx in range(scale):
                        px[pad + x * scale + dx, pad + y * scale + dy] = (235, 240, 245)
    d = ImageDraw.Draw(img)
    for c in range(1, GRID_COLS):
        d.line([(pad + c * w * scale, pad), (pad + c * w * scale, pad + ah * scale)], fill=(60, 80, 100))
    for r in range(1, GRID_ROWS):
        d.line([(pad, pad + r * h * scale), (pad + aw * scale, pad + r * h * scale)], fill=(60, 80, 100))

    sample = "FPS 89.7 (11.1 ms)  ERRO xrCreateInstance=-2  ok: A-Z a-z 0-9 /:_"
    sx, sy = pad, pad + ah * scale + 12
    for i, ch in enumerate(sample):
        cp = ord(ch)
        if not (FIRST <= cp <= LAST):
            continue
        cell = cp - FIRST
        ox, oy = (cell % GRID_COLS) * w, (cell // GRID_COLS) * h
        for y in range(h):
            for x in range(w):
                if atlas[(oy + y) * aw + (ox + x)]:
                    d.point((sx + i * w + x, sy + y), fill=(245, 166, 35))
    img.save(out_path)
    print("preview: %s" % out_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/usr/lib/kbd/consolefonts/default8x16.psfu.gz")
    ap.add_argument("--out", default=os.path.join(HERE, "vr_font.h"))
    ap.add_argument("--preview", default=os.path.join(HERE, "vr_font_preview.png"))
    args = ap.parse_args()

    if not os.path.exists(args.font):
        sys.exit("fonte não encontrada: %s" % args.font)

    atlas, aw, ah, w, h, missing = build_atlas(args.font)
    if (w, h) != (8, 16):
        print("aviso: fonte %dx%d (o painel assume 8x16)" % (w, h))
    if missing:
        print("aviso: glifos ausentes na fonte, usando '?': %r" % "".join(missing))

    emit_header(atlas, aw, ah, w, h, args.font, args.out)
    print("gerado: %s (%dx%d, célula %dx%d)" % (args.out, aw, ah, w, h))
    emit_preview(atlas, aw, ah, w, h, args.preview)


if __name__ == "__main__":
    main()
