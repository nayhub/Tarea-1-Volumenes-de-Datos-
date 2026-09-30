#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <cmath>
#include <limits>
#include <type_traits>
#include <cctype>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "sketches.hpp"
#include "sliding_window.hpp"

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
static_assert(sizeof(Record) == 24, "Record debe ocupar 24 bytes");

struct Trace {
    const Record *r = nullptr;
    size_t n = 0;
    void *addr = nullptr;
    size_t bytes = 0;
};

static Trace map_trace(const char *path) {
#ifdef _WIN32
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "error: no se pudo abrir %s\n", path);
        exit(1);
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size)) {
        fprintf(stderr, "error: no se pudo obtener el tamano de %s\n", path);
        CloseHandle(file); exit(1);
    }
    if (size.QuadPart % (LONGLONG)sizeof(Record)) {
        fprintf(stderr, "error: el archivo no es multiplo de 24 bytes\n");
        CloseHandle(file); exit(1);
    }
    Trace t;
    t.bytes = static_cast<size_t>(size.QuadPart);
    t.n = t.bytes / sizeof(Record);
    if (t.bytes) {
        HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) { fprintf(stderr, "error: CreateFileMapping fallo\n"); CloseHandle(file); exit(1); }
        t.addr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);
        if (!t.addr) { fprintf(stderr, "error: MapViewOfFile fallo\n"); CloseHandle(file); exit(1); }
        t.r = static_cast<const Record *>(t.addr);
    }
    CloseHandle(file);
    return t;
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); exit(1); }
    if (st.st_size % (off_t)sizeof(Record)) {
        fprintf(stderr, "error: el archivo no es multiplo de 24 bytes\n");
        close(fd); exit(1);
    }
    if (st.st_size == 0) {
        close(fd); return Trace{};
    }
    void *p = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); close(fd); exit(1); }
    close(fd);
    Trace t;
    t.addr = p;
    t.bytes = static_cast<size_t>(st.st_size);
    t.n = t.bytes / sizeof(Record);
    t.r = static_cast<const Record *>(p);
    return t;
#endif
}

static void unmap_trace(Trace &t) {
#ifdef _WIN32
    if (t.addr) UnmapViewOfFile(t.addr);
#else
    if (t.addr && t.bytes) munmap(t.addr, t.bytes);
#endif
    t = Trace{};
}

static std::string ip_to_string(uint32_t v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (v >> 24) & 255u, (v >> 16) & 255u,
             (v >> 8) & 255u, v & 255u);
    return std::string(buf);
}

static uint32_t parse_ip(const std::string &s) {
    unsigned a, b, c, d;
    char extra;
    std::stringstream ss(s);
    if (!(ss >> a >> extra >> b >> extra >> c >> extra >> d) || a > 255 || b > 255 || c > 255 || d > 255) {
        throw std::invalid_argument("IP invalida: " + s);
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

static std::vector<uint32_t> parse_queries(const std::string &s) {
    std::vector<uint32_t> out;
    size_t p = 0;
    while (p <= s.size()) {
        size_t comma = s.find(',', p);
        std::string token = comma == std::string::npos ? s.substr(p) : s.substr(p, comma - p);
        if (!token.empty()) out.push_back(parse_ip(token));
        if (comma == std::string::npos) break;
        p = comma + 1;
    }
    return out;
}

static std::vector<int> parse_widths(const std::string &s) {
    std::vector<int> out;
    size_t p = 0;
    while (p <= s.size()) {
        size_t comma = s.find(',', p);
        std::string token = comma == std::string::npos ? s.substr(p) : s.substr(p, comma - p);
        if (!token.empty()) out.push_back(std::atoi(token.c_str()));
        if (comma == std::string::npos) break;
        p = comma + 1;
    }
    return out;
}

static double abs_error(int64_t est, uint64_t exact) {
    double d = static_cast<double>(est) - static_cast<double>(exact);
    return std::fabs(d);
}

static double rel_error(int64_t est, uint64_t exact) {
    if (exact == 0) return 0.0;
    return abs_error(est, exact) / static_cast<double>(exact);
}

// Mapa N esperado desde un CSV producido por exact_hh.cpp.
// Se busca una columna llamada N (case-insensitive) y una columna de tiempo
// llamada timestamp/time/ts/ts_us. Las filas se asocian por timestamp.
static bool load_exact_hh_N(const std::string &path,
                            std::unordered_map<uint64_t, uint64_t> &expected,
                            std::string &error) {
    std::ifstream in(path);
    if (!in) { error = "no se pudo abrir exact-csv: " + path; return false; }
    std::string line;
    if (!std::getline(in, line)) { error = "exact-csv vacio"; return false; }

    std::vector<std::string> headers;
    std::stringstream hs(line);
    std::string cell;
    while (std::getline(hs, cell, ',')) headers.push_back(cell);

    auto norm = [](std::string s) {
        for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    int time_col = -1, n_col = -1;
    for (int i = 0; i < (int)headers.size(); ++i) {
        std::string h = norm(headers[i]);
        if (h == "n" || h == "count" || h == "packets") n_col = i;
        if (h == "timestamp" || h == "time" || h == "ts" || h == "ts_us") time_col = i;
    }
    if (time_col < 0 || n_col < 0) {
        error = "exact-csv debe contener columnas timestamp/ts_us y N";
        return false;
    }

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> v;
        std::stringstream ss(line);
        while (std::getline(ss, cell, ',')) v.push_back(cell);
        if (time_col >= (int)v.size() || n_col >= (int)v.size()) continue;
        uint64_t ts = std::strtoull(v[time_col].c_str(), nullptr, 10);
        uint64_t n = std::strtoull(v[n_col].c_str(), nullptr, 10);
        expected[ts] = n;
    }
    return true;
}

template <typename Sketch>
static int64_t sketch_estimate(const SlidingWindow<Sketch> &sw, uint32_t key);

template <>
int64_t sketch_estimate(const SlidingWindow<CountMinSketch> &sw, uint32_t key) {
    return sw.aggregate().estimate(key);
}

template <>
int64_t sketch_estimate(const SlidingWindow<CountSketch> &sw, uint32_t key) {
    return sw.aggregate().estimate_nonnegative(key);
}

template <typename Sketch>
static void process_width(const Trace &t, const std::string &key_name, int d, int w,
                          uint64_t seed, const std::vector<uint32_t> &queries,
                          const std::string &out_path,
                          const std::unordered_map<uint64_t, uint64_t> *hh_N,
                          bool &global_ok) {
    uint64_t sketch_seed = derive_seed(seed, static_cast<uint64_t>(w));
    if constexpr (std::is_same<Sketch, CountMinSketch>::value) sketch_seed ^= 0x1ULL;
    else sketch_seed ^= 0x2ULL;

    SlidingWindow<Sketch> sw(d, w, sketch_seed);

    std::string filename;
    FILE *fo = nullptr;
    if (!out_path.empty()) {
        filename = out_path;
        size_t p = filename.find("{w}");
        if (p != std::string::npos) filename.replace(p, 3, std::to_string(w));
        p = filename.find("{sketch}");
        if (p != std::string::npos) {
            const char *name = std::is_same<Sketch, CountMinSketch>::value ? "cms" : "cs";
            filename.replace(p, 8, name);
        }
        fo = std::fopen(filename.c_str(), "w");
        if (!fo) throw std::runtime_error("no se pudo abrir salida: " + filename);
        fprintf(fo, "timestamp_us,time_s,N,key,exact,estimate,abs_error,rel_error\n");
    }

    std::vector<uint32_t> use_queries = queries;
    if (use_queries.empty()) {
        std::unordered_map<uint32_t, uint64_t> global;
        global.reserve(1 << 20);
        for (size_t i = 0; i < t.n; ++i) {
            uint32_t key = key_name == "src" ? t.r[i].src : t.r[i].dst;
            ++global[key];
        }
        std::vector<std::pair<uint64_t, uint32_t>> top;
        top.reserve(global.size());
        for (const auto &kv : global) top.push_back({kv.second, kv.first});
        size_t limit = std::min<size_t>(10, top.size());
        std::partial_sort(top.begin(), top.begin() + limit, top.end(),
                          [](const auto &a, const auto &b) {
                              if (a.first != b.first) return a.first > b.first;
                              return a.second < b.second;
                          });
        for (size_t i = 0; i < limit; ++i) use_queries.push_back(top[i].second);
    }

    std::unordered_map<uint32_t, bool> query_set;
    for (uint32_t q : use_queries) query_set[q] = true;

    // Estado exacto de la ventana. Solo se guardan indices de paquetes dentro
    // de los 60 s y frecuencias de las claves consultadas.
    std::deque<size_t> window_indices;
    std::unordered_map<uint32_t, uint64_t> exact_freq;
    exact_freq.reserve(use_queries.size() * 2 + 1);
    uint64_t exact_N = 0;
    bool first = true;
    uint64_t next_eval = 0;

    auto add_exact_packet = [&](size_t idx) {
        window_indices.push_back(idx);
        ++exact_N;
        uint32_t k = key_name == "src" ? t.r[idx].src : t.r[idx].dst;
        if (query_set.find(k) != query_set.end()) ++exact_freq[k];
    };

    auto remove_expired = [&](uint64_t eval) {
        uint64_t left_time = eval - SlidingWindow<Sketch>::WINDOW_US;
        while (!window_indices.empty() && t.r[window_indices.front()].ts_us <= left_time) {
            size_t idx = window_indices.front();
            window_indices.pop_front();
            --exact_N;
            uint32_t k = key_name == "src" ? t.r[idx].src : t.r[idx].dst;
            auto it = exact_freq.find(k);
            if (it != exact_freq.end()) {
                if (--it->second == 0) exact_freq.erase(it);
            }
        }
    };

    auto evaluate = [&](uint64_t eval) {
        remove_expired(eval);

        if (sw.total_count() != exact_N) {
            fprintf(stderr,
                    "ERROR alineacion %s w=%d eval=%" PRIu64 ": N_sketch=%" PRIu64 " N_exact=%" PRIu64 "\n",
                    std::is_same<Sketch, CountMinSketch>::value ? "CMS" : "CS",
                    w, eval, sw.total_count(), exact_N);
            global_ok = false;
        }

        if (hh_N) {
            auto it = hh_N->find(eval);
            if (it != hh_N->end() && it->second != sw.total_count()) {
                fprintf(stderr,
                        "ERROR exact_hh %s w=%d eval=%" PRIu64 ": N_sketch=%" PRIu64 " N_exact_hh=%" PRIu64 "\n",
                        std::is_same<Sketch, CountMinSketch>::value ? "CMS" : "CS",
                        w, eval, sw.total_count(), it->second);
                global_ok = false;
            }
        }

        for (uint32_t qkey : use_queries) {
            uint64_t exact = 0;
            auto it = exact_freq.find(qkey);
            if (it != exact_freq.end()) exact = it->second;
            int64_t est = sketch_estimate(sw, qkey);
            if (fo) {
                fprintf(fo, "%" PRIu64 ",%.6f,%" PRIu64 ",%s,%" PRIu64 ",%" PRId64 ",%.6f,%.9f\n",
                        eval, eval / 1000000.0, exact_N, ip_to_string(qkey).c_str(),
                        exact, est, abs_error(est, exact), rel_error(est, exact));
            }
        }
    };

    for (size_t i = 0; i < t.n; ++i) {
        const Record &r = t.r[i];
        uint32_t key = key_name == "src" ? r.src : r.dst;

        if (first) {
            first = false;
            next_eval = r.ts_us + SlidingWindow<Sketch>::WINDOW_US;
            sw.add(r.ts_us, key); // t0: ignorado por SlidingWindow segun la convencion.
            continue;
        }

        // Evaluaciones anteriores al timestamp actual no deben ver el paquete actual.
        while (r.ts_us > next_eval) {
            sw.advance_to(next_eval);
            evaluate(next_eval);
            next_eval += SlidingWindow<Sketch>::SUBWINDOW_US;
        }

        // Ahora agregamos el paquete. Si cae exactamente en el borde, pertenece
        // a la ventana que termina en ese borde y se evalua inmediatamente despues.
        sw.add(r.ts_us, key);
        add_exact_packet(i);

        if (r.ts_us == next_eval) {
            sw.advance_to(next_eval);
            evaluate(next_eval);
            next_eval += SlidingWindow<Sketch>::SUBWINDOW_US;
        }
    }

    if (fo) std::fclose(fo);

    std::printf("%-4s w=%-5d d=%-2d memoria_contadores=%zu bytes (%.2f KB)\n",
                std::is_same<Sketch, CountMinSketch>::value ? "CMS" : "CS",
                w, d, sw.sketch_memory_bytes(), sw.sketch_memory_bytes() / 1024.0);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s TRAZA.bin [opciones]\n\n"
        "Opciones:\n"
        "  --key src|dst       campo usado como clave (default dst)\n"
        "  --d N               filas (default 5)\n"
        "  --w N               ancho; puede ser 256,1024,4096 o lista\n"
        "  --queries IP[,IP]   claves a validar; si se omite usa top-10 global\n"
        "  --seed N             semilla base (default 12345)\n"
        "  --out PREFIJO       CSV; puede usar {sketch} y {w}\n"
        "  --exact-csv FILE    CSV de exact_hh.cpp para validar columna N\n"
        "\nEjemplo:\n"
        "  %s traza.bin --key dst --d 5 --w 256,1024,4096 --out resultados_{sketch}_{w}.csv\n",
        prog, prog);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }

    std::string path = argv[1];
    std::string key_name = "dst";
    int d = 5;
    std::string width_arg = "256,1024,4096";
    std::string query_arg;
    uint64_t seed = 12345;
    std::string out_prefix;
    std::string exact_csv;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "falta valor para %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--key") key_name = next();
        else if (a == "--d") d = std::atoi(next().c_str());
        else if (a == "--w") width_arg = next();
        else if (a == "--queries") query_arg = next();
        else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--out") out_prefix = next();
        else if (a == "--exact-csv") exact_csv = next();
        else { fprintf(stderr, "opcion no reconocida: %s\n", a.c_str()); usage(argv[0]); return 2; }
    }

    if (key_name != "src" && key_name != "dst") {
        fprintf(stderr, "error: --key debe ser src o dst\n"); return 2;
    }
    if (d <= 0) { fprintf(stderr, "error: d debe ser > 0\n"); return 2; }

    std::vector<int> widths = parse_widths(width_arg);
    if (widths.empty()) { fprintf(stderr, "error: lista --w vacia\n"); return 2; }
    for (int w : widths) if (w <= 0) { fprintf(stderr, "error: w debe ser > 0\n"); return 2; }

    std::vector<uint32_t> queries;
    try { if (!query_arg.empty()) queries = parse_queries(query_arg); }
    catch (const std::exception &e) { fprintf(stderr, "error: %s\n", e.what()); return 2; }

    std::unordered_map<uint64_t, uint64_t> hh_N;
    const std::unordered_map<uint64_t, uint64_t> *hh_ptr = nullptr;
    if (!exact_csv.empty()) {
        std::string err;
        if (!load_exact_hh_N(exact_csv, hh_N, err)) {
            fprintf(stderr, "advertencia: %s\n", err.c_str());
        } else {
            hh_ptr = &hh_N;
            fprintf(stderr, "exact_hh: cargadas %zu ventanas para validar N\n", hh_N.size());
        }
    }

    Trace t = map_trace(path.c_str());
    if (t.n == 0) { fprintf(stderr, "error: traza vacia\n"); return 1; }
    fprintf(stderr, "traza: %zu registros, key=%s, t0=%" PRIu64 " us\n",
            t.n, key_name.c_str(), t.r[0].ts_us);
    fprintf(stderr, "ventana=60 s, subventana=10 s, m=6\n");

    bool ok = true;
    try {
        for (int w : widths) {
            process_width<CountMinSketch>(t, key_name, d, w, seed, queries,
                                          out_prefix, hh_ptr, ok);
            process_width<CountSketch>(t, key_name, d, w, seed, queries,
                                       out_prefix, hh_ptr, ok);
        }
    } catch (const std::exception &e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        unmap_trace(t);
        return 1;
    }

    unmap_trace(t);
    if (!ok) {
        fprintf(stderr, "RESULTADO: FALLA en la validacion de la alineacion/N.\n");
        return 3;
    }
    fprintf(stderr, "RESULTADO: validacion N correcta.\n");
    return 0;
}
