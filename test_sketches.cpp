// test_sketches.cpp
//
// Valida sketches.hpp contra conteo exacto, sin ventana deslizante: recorre
// una traza completa y compara la frecuencia exacta de un conjunto de
// claves contra la frecuencia estimada por Count-Min Sketch y CountSketch,
// para distintos anchos w.
//
// Uso:
//     ./test_sketches TRAZA.bin [opciones]
//
// Opciones:
//     --key {src|dst}   campo a usar como clave (por defecto dst)
//     --d N             filas del sketch (por defecto 5)
//     --w LISTA         anchos separados por coma (por defecto 256,1024,4096)
//     --top N           cuantas de las claves mas frecuentes reportar (def 20)
//     --random N        ademas, N claves elegidas al azar entre las vistas (def 0)
//     --seed N          semilla base de los sketches (por defecto 12345)
//     --out ARCHIVO     si se entrega, ademas escribe todo en un CSV

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <random>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sketches.hpp"

// Mismo registro de 24 bytes que usan pcap2bin.cpp y exact_hh.cpp.
#pragma pack(push, 1)
struct Record {
    uint64_t ts_us;
    uint32_t src;
    uint32_t dst;
    uint16_t sport;
    uint16_t dport;
    uint16_t len;
    uint8_t proto;
    uint8_t flags;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 24, "el registro debe ocupar 24 bytes");

struct Trace {
    const Record *r = nullptr;
    size_t n = 0;
    void *addr = nullptr;
    size_t bytes = 0;
};

static Trace map_trace(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); exit(1); }
    if (st.st_size % (off_t)sizeof(Record)) {
        fprintf(stderr, "error: el tamaño de %s no es multiplo de 24 bytes\n", path);
        exit(1);
    }
    void *p = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    close(fd);
    Trace t;
    t.addr = p;
    t.bytes = (size_t)st.st_size;
    t.r = (const Record *)p;
    t.n = t.bytes / sizeof(Record);
    return t;
}

static std::string ip_to_string(uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%u.%u.%u.%u",
              (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    return std::string(buf);
}

static std::vector<int> parse_width_list(const std::string &s) {
    std::vector<int> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        std::string piece = (comma == std::string::npos) ? s.substr(start)
                                                          : s.substr(start, comma - start);
        if (!piece.empty()) out.push_back(atoi(piece.c_str()));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

struct ReportRow {
    int w;
    uint32_t key;
    uint64_t exact;
    int64_t cms_est;
    int64_t cs_est;
};

static double rel_error(int64_t est, uint64_t exact) {
    if (exact == 0) return 0.0;
    double diff = (double)est - (double)exact;
    if (diff < 0) diff = -diff;
    return diff / (double)exact;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "uso: %s TRAZA.bin [--key src|dst] [--d N] [--w L1,L2,...] "
            "[--top N] [--random N] [--seed N] [--out ARCHIVO]\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    std::string key_name = "dst";
    int d = 5;
    std::string width_list = "256,1024,4096";
    int top_n = 20;
    int random_n = 0;
    uint64_t seed = 12345;
    const char *out_path = nullptr;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char * {
            if (i + 1 >= argc) { fprintf(stderr, "falta valor para %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--key") key_name = next();
        else if (a == "--d") d = atoi(next());
        else if (a == "--w") width_list = next();
        else if (a == "--top") top_n = atoi(next());
        else if (a == "--random") random_n = atoi(next());
        else if (a == "--seed") seed = (uint64_t)strtoull(next(), nullptr, 10);
        else if (a == "--out") out_path = next();
        else { fprintf(stderr, "opcion no reconocida: %s\n", argv[i]); return 2; }
    }
    if (key_name != "src" && key_name != "dst") {
        fprintf(stderr, "error: --key debe ser 'src' o 'dst'\n");
        return 2;
    }
    std::vector<int> widths = parse_width_list(width_list);
    if (widths.empty()) {
        fprintf(stderr, "error: --w no entrego ningun ancho valido\n");
        return 2;
    }

    Trace t = map_trace(path);
    if (t.n == 0) {
        fprintf(stderr, "error: la traza esta vacia\n");
        return 1;
    }
    fprintf(stderr, "traza cargada: %zu registros, clave = %s\n", t.n, key_name.c_str());

    // Conteo exacto con una tabla hash estandar, usado como referencia.
    std::unordered_map<uint32_t, uint64_t> exact;
    exact.reserve(1 << 20);
    for (size_t i = 0; i < t.n; ++i) {
        uint32_t key = (key_name == "src") ? t.r[i].src : t.r[i].dst;
        exact[key]++;
    }
    fprintf(stderr, "claves distintas observadas: %zu\n", exact.size());

    // Un CMS y un CS nuevos por cada ancho w, recorriendo la traza una vez
    // por ancho.
    std::vector<ReportRow> rows;
    std::vector<std::pair<int, size_t>> memory_per_width;

    for (int w : widths) {
        uint64_t seed_cms = derive_seed(seed, (uint64_t)w) ^ 0x1ULL;
        uint64_t seed_cs = derive_seed(seed, (uint64_t)w) ^ 0x2ULL;
        CountMinSketch cms(d, w, seed_cms);
        CountSketch cs(d, w, seed_cs);

        for (size_t i = 0; i < t.n; ++i) {
            uint32_t key = (key_name == "src") ? t.r[i].src : t.r[i].dst;
            cms.update(key);
            cs.update(key);
        }

        memory_per_width.push_back({w, cms.memory_bytes()});

        // Claves a reportar: las top_n mas frecuentes, mas random_n claves
        // elegidas al azar entre todas las vistas.
        std::vector<std::pair<uint64_t, uint32_t>> by_freq;
        by_freq.reserve(exact.size());
        for (const auto &kv : exact) by_freq.push_back({kv.second, kv.first});
        std::sort(by_freq.begin(), by_freq.end(), std::greater<>());

        std::vector<uint32_t> keys_to_check;
        for (int i = 0; i < top_n && i < (int)by_freq.size(); ++i) {
            keys_to_check.push_back(by_freq[(size_t)i].second);
        }
        if (random_n > 0) {
            std::mt19937_64 rng(seed + 777);
            std::uniform_int_distribution<size_t> dist(0, by_freq.size() - 1);
            for (int i = 0; i < random_n; ++i) {
                keys_to_check.push_back(by_freq[dist(rng)].second);
            }
        }

        for (uint32_t key : keys_to_check) {
            ReportRow row;
            row.w = w;
            row.key = key;
            row.exact = exact[key];
            row.cms_est = cms.estimate(key);
            row.cs_est = cs.estimate_nonnegative(key);
            rows.push_back(row);
        }
    }

    // Reporte por pantalla, agrupado por w: filas individuales mas el
    // error absoluto y relativo promedio de cada sketch.
    printf("clave                w      exacto   CMS_est  CMS_err_abs  CMS_err_rel   "
           "CS_est  CS_err_abs  CS_err_rel\n");
    for (int w : widths) {
        double sum_abs_cms = 0, sum_rel_cms = 0;
        double sum_abs_cs = 0, sum_rel_cs = 0;
        int count = 0;
        for (const auto &row : rows) {
            if (row.w != w) continue;
            int64_t abs_cms = row.cms_est - (int64_t)row.exact;
            int64_t abs_cs = row.cs_est - (int64_t)row.exact;
            double rel_cms = rel_error(row.cms_est, row.exact);
            double rel_cs = rel_error(row.cs_est, row.exact);
            printf("%-16s %6d %10" PRIu64 " %9" PRId64 " %12" PRId64 " %12.4f "
                   "%8" PRId64 " %11" PRId64 " %11.4f\n",
                   ip_to_string(row.key).c_str(), row.w, row.exact, row.cms_est,
                   abs_cms, rel_cms, row.cs_est, abs_cs, rel_cs);
            sum_abs_cms += (abs_cms < 0 ? -abs_cms : abs_cms);
            sum_rel_cms += rel_cms;
            sum_abs_cs += (abs_cs < 0 ? -abs_cs : abs_cs);
            sum_rel_cs += rel_cs;
            count++;
        }
        if (count == 0) continue;
        printf("-- w = %d: error absoluto medio  CMS = %.2f   CS = %.2f\n", w,
               sum_abs_cms / count, sum_abs_cs / count);
        printf("-- w = %d: error relativo medio  CMS = %.4f   CS = %.4f\n\n", w,
               sum_rel_cms / count, sum_rel_cs / count);
    }

    // Memoria: tabla exacta (una entrada por clave distinta) contra el
    // costo fijo de cada sketch (d * w contadores).
    double exact_bytes = (double)exact.size() * (sizeof(uint32_t) + sizeof(uint64_t) + sizeof(void *));
    printf("memoria tabla exacta (estimada): %.1f KB para %zu claves distintas\n",
           exact_bytes / 1024.0, exact.size());
    for (const auto &pw : memory_per_width) {
        printf("memoria por sketch (CMS o CS) con w = %-5d : %.1f KB (d = %d)\n",
               pw.first, (double)pw.second / 1024.0, d);
    }

    // CSV opcional con una fila por (clave, w).
    if (out_path) {
        FILE *fo = fopen(out_path, "w");
        if (!fo) { perror("fopen --out"); return 1; }
        fprintf(fo, "key,w,exact,cms_est,cms_abs_err,cms_rel_err,cs_est,cs_abs_err,cs_rel_err\n");
        for (const auto &row : rows) {
            int64_t abs_cms = row.cms_est - (int64_t)row.exact;
            int64_t abs_cs = row.cs_est - (int64_t)row.exact;
            fprintf(fo, "%s,%d,%" PRIu64 ",%" PRId64 ",%" PRId64 ",%.6f,%" PRId64 ",%" PRId64 ",%.6f\n",
                    ip_to_string(row.key).c_str(), row.w, row.exact, row.cms_est, abs_cms,
                    rel_error(row.cms_est, row.exact), row.cs_est, abs_cs,
                    rel_error(row.cs_est, row.exact));
        }
        fclose(fo);
        fprintf(stderr, "reporte completo escrito en %s\n", out_path);
    }

    munmap(t.addr, t.bytes);
    return 0;
}
