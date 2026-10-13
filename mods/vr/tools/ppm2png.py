#!/usr/bin/env python3
"""ppm2png.py — converte o .ppm (P6) que o mod grava para .png, sem dependencia.

O mod so sabe gravar PPM (VR_PANEL_DUMP / VR_STEREO_DUMP), e PPM nao abre em
visualizador nenhum. Este script usa SO a biblioteca padrao (zlib), entao roda em
qualquer lugar onde o jogo roda.

Uso: python3 ppm2png.py entrada.ppm [saida.png]
"""
import sys
import zlib
import struct


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()

    # cabecalho: P6 <espaco> largura <espaco> altura <espaco> maxval, com comentarios
    pos = 0
    toks = []
    while len(toks) < 4:
        while pos < len(data):
            c = data[pos:pos + 1]
            if c in b' \t\r\n':
                pos += 1
            elif c == b'#':
                while pos < len(data) and data[pos:pos + 1] not in b'\r\n':
                    pos += 1
            else:
                break
        start = pos
        while pos < len(data) and data[pos:pos + 1] not in b' \t\r\n':
            pos += 1
        if start == pos:
            raise SystemExit("ppm2png: cabecalho truncado em %s" % path)
        toks.append(data[start:pos])

    pos += 1  # o separador (1 byte) depois do maxval
    magic, w, h, maxv = toks[0], int(toks[1]), int(toks[2]), int(toks[3])
    if magic != b'P6':
        raise SystemExit("ppm2png: so P6 (binario RGB); veio %r" % magic)
    if maxv != 255:
        raise SystemExit("ppm2png: maxval %d (so 255 e suportado)" % maxv)

    px = data[pos:pos + w * h * 3]
    if len(px) < w * h * 3:
        raise SystemExit("ppm2png: faltam pixels (%d de %d)" % (len(px), w * h * 3))
    return w, h, px


def write_png(path, w, h, rgb):
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)  # filtro 0 (None) por linha
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(bytes(raw), 6))
    out += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(out)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else (src.rsplit('.', 1)[0] + '.png')
    w, h, px = read_ppm(src)
    write_png(dst, w, h, px)
    print("ppm2png: %s -> %s (%dx%d)" % (src, dst, w, h))


main()
