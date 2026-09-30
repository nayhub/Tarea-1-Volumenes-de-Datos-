# Actividad 1 — Ventana deslizante con CMS y CountSketch

## Archivos

- `sketches.hpp`: CMS y CountSketch originales, con una corrección de portabilidad para MSVC (`_umul128`).
- `sliding_window.hpp`: anillo de 6 subventanas de 10 s y sketch agregado.
- `activity1.cpp`: programa principal, lectura de `traza.bin`, validación, CSV y memoria.
- `Makefile`: compilación en WSL/Linux.

No se necesita `sliding_window.cpp`: la clase es un template y está definida completamente en `sliding_window.hpp`.

## Parámetros de la tarea

- Ventana: `W = 60 s`
- Paso: `p = 10 s`
- Subventanas: `m = 6`
- `d` configurable, por defecto `5`
- `w` configurable, por defecto `256,1024,4096`
- Las subventanas siguen `(t0 + (q-1)p, t0 + qp]`.
- La ranura usada es `(q-1) mod 6`.

## Compilar en WSL

```bash
make
```

o:

```bash
g++ -O2 -std=c++17 -Wall -Wextra -pedantic -o activity1 activity1.cpp
```

## Uso básico

```bash
./activity1 traza.bin --key dst --d 5 --w 256,1024,4096 \
  --out resultados_{sketch}_{w}.csv
```

Para DDoS se usa normalmente:

```bash
--key dst
```

Para Scan:

```bash
--key src
```

## Consultar claves concretas

Se pueden indicar una o varias IP separadas por coma:

```bash
./activity1 traza.bin --key dst --d 5 --w 256 \
  --queries 192.0.2.10,192.0.2.11 \
  --out resultados_{sketch}_{w}.csv
```

Si `--queries` se omite, el programa selecciona las 10 claves globalmente más frecuentes como conjunto de validación.

## CSV

El archivo contiene:

```text
timestamp_us,time_s,N,key,exact,estimate,abs_error,rel_error
```

`exact` es el conteo exacto de la clave dentro de la ventana y `estimate` es:

- CMS: mínimo de las filas.
- CountSketch: mediana con truncamiento a cero.

## Memoria

La salida informa la memoria de los contadores de **un sketch agregado**:

```text
d * w * sizeof(int64_t)
```

El anillo mantiene 6 sub-sketches más el agregado, por lo que la memoria de contadores de toda la ventana es:

```text
7 * d * w * sizeof(int64_t)
```

Si el informe pide la memoria total de la estructura de ventana, multiplica la memoria mostrada por 7.

## Validación con exact_hh

El programa puede leer un CSV producido por `exact_hh.cpp` mediante:

```bash
./activity1 traza.bin --key dst --d 5 --w 256 \
  --exact-csv exact_ddos.csv
```

El CSV de `exact_hh` debe contener columnas de tiempo (`timestamp`, `time`, `ts` o `ts_us`) y una columna `N`/`count`/`packets`. El tiempo debe estar en microsegundos, igual que `ts_us` de `traza.bin`.

La autoverificación principal también compara el `N` del anillo contra un conteo exacto construido sobre la misma ventana. Una discrepancia invalida los resultados.

## Importante sobre `--out`

Use `{sketch}` y `{w}` para generar archivos separados:

```bash
--out resultados_{sketch}_{w}.csv
```

produce, por ejemplo:

```text
resultados_cms_256.csv
resultados_cs_256.csv
resultados_cms_1024.csv
resultados_cs_1024.csv
resultados_cms_4096.csv
resultados_cs_4096.csv
```
