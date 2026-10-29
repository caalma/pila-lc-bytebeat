#!/usr/bin/env python3
import argparse
import re
import sys
import time

def main():
    parser = argparse.ArgumentParser(
        description="Envia bloques de codigo a un archivo destino con delay."
    )
    parser.add_argument("source", help="Archivo fuente con bloques separados por '...'")
    parser.add_argument("target", help="Archivo destino")
    parser.add_argument("delay", type=float, help="Delay en segundos entre bloques")
    parser.add_argument("--index-start", type=int, default=None,
                        help="Indice inicial (1-based, inclusivo). Default: 1")
    parser.add_argument("--index-end", type=int, default=None,
                        help="Indice final (1-based, inclusivo). Default: ultimo bloque")
    parser.add_argument("--only", type=int, default=None,
                        help="Enviar unicamente este bloque (1-based). No puede combinarse con --index-start/--index-end")
    args = parser.parse_args()

    with open(args.source, "r", encoding="utf-8") as f:
        content = f.read()

    raw_blocks = re.split(r"^\s*\.\.\.\s*$", content, flags=re.MULTILINE)
    blocks = [b.strip() for b in raw_blocks]
    total = len(blocks)

    if args.only is not None:
        if args.index_start is not None or args.index_end is not None:
            print("Error: --only no puede combinarse con --index-start ni --index-end")
            sys.exit(1)

        if args.only < 1 or args.only > total:
            print("Error: bloque invalido. Total de bloques disponibles: %d" % total)
            sys.exit(1)

        idx_start = args.only - 1
        idx_end = args.only
    else:
        idx_start = (args.index_start - 1) if args.index_start is not None else 0
        idx_end = args.index_end if args.index_end is not None else total

    if idx_start < 0 or idx_end > total or idx_start >= idx_end:
        print("Error: rango invalido. Total de bloques disponibles: %d" % total)
        sys.exit(1)

    for i in range(idx_start, idx_end):
        block = blocks[i]
        block_num = i + 1

        if not block:
            print("sleep=%.2fs | bloque=%d/%d | (vacio, se omite)" % (args.delay, block_num, total))
            continue

        first_line = block.split("\n")[0].strip()

        with open(args.target, "w", encoding="utf-8") as f:
            f.write(block + "\n")

        print("sleep=%.2fs | bloque=%d/%d | %s" % (args.delay, block_num, total, first_line))

        if i < idx_end - 1:
            time.sleep(args.delay)

if __name__ == "__main__":
    main()
