# Tarea 1 2026 - Count-Min Sketch y CountSketch en ventanas deslizantes

Curso: Topicos en Manejo de Grandes Volumenes de Datos.
Integrantes: Gabriela Muñoz, Matías Barriga, Antonia Guajardo.

## Estructura

```
.
├── Makefile
├── requirements.txt
├── pcap2bin.cpp
├── exact_hh.cpp
├── inject_attack.py
├── infotrazas.txt
├── sketches.hpp          Count-Min Sketch y CountSketch
├── test_sketches.cpp     validacion de sketches.hpp contra conteo exacto
├── scripts/
│   └── gen_synthetic.py  traza de juguete para probar sketches.hpp
├── window.hpp / window.cpp
├── analysis/
├── informe/
└── results/
```

## Compilacion

Requiere g++ con soporte C++17.

```
make
```

Genera `pcap2bin`, `exact_hh` y `test_sketches`.

## Validacion de los sketches

```
./test_sketches traza.bin --key dst --d 5 --w 256,1024,4096 --top 30 --out results/validacion_dst.csv
./test_sketches traza.bin --key src --d 5 --w 256,1024,4096 --top 30 --out results/validacion_src.csv
```

Para probar sin una traza real:

```
python3 scripts/gen_synthetic.py 300000 synthetic.bin
./test_sketches synthetic.bin --key dst
```

## Semilla utilizada

Semilla usada por el grupo para `inject_attack.py`: `<completar>`.
