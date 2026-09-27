#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_synthetic.py -- Traza sintetica pequeña para probar sketches.hpp rapido.

Esto NO es parte de los entregables de la tarea. Es solo una utilidad para
poder correr test_sketches.cpp sin tener que bajar y convertir la traza real
de MAWI (que pesa varios cientos de MB) mientras se esta debuggeando la
implementacion de los sketches. Genera un archivo con el mismo formato de 24
bytes por registro que usa pcap2bin.cpp, con una distribucion de IP destino
tipo Zipf para que existan algunas IP claramente mas frecuentes que el resto
(heavy hitters de juguete) y una cola larga de IP poco frecuentes.

Uso:
    python3 gen_synthetic.py [n_registros] [archivo_salida]

Por defecto genera 300000 registros en synthetic.bin.
"""

import sys
import numpy as np

REC = np.dtype([
    ("ts", "<u8"),
    ("src", "<u4"),
    ("dst", "<u4"),
    ("sport", "<u2"),
    ("dport", "<u2"),
    ("len", "<u2"),
    ("proto", "u1"),
    ("flags", "u1"),
])
assert REC.itemsize == 24


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300000
    out_path = sys.argv[2] if len(sys.argv) > 2 else "synthetic.bin"

    rng = np.random.default_rng(1)
    r = np.zeros(n, dtype=REC)

    # marcas de tiempo crecientes repartidas en una ventana de 60 segundos
    r["ts"] = np.sort(rng.integers(0, 60_000_000, size=n)).astype("<u8")

    # distribucion Zipf sobre el destino: unas pocas IP muy frecuentes y una
    # cola larga de IP con frecuencia baja, que es el caso interesante para
    # ver como se comporta el error de los sketches segun la frecuencia.
    ranks = np.clip(rng.zipf(1.3, size=n), 1, 50000)
    r["dst"] = (3232235520 + ranks).astype("<u4")  # a partir de 192.168.0.0
    r["src"] = rng.integers(1, 2**32 - 1, size=n).astype("<u4")
    r["sport"] = rng.integers(1024, 65535, size=n).astype("<u2")
    r["dport"] = 80
    r["len"] = 64
    r["proto"] = 6
    r["flags"] = 0

    r.tofile(out_path)
    print("registros escritos: %d -> %s" % (n, out_path))


if __name__ == "__main__":
    main()
