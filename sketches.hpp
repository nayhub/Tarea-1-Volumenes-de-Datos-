// sketches.hpp
//
// Implementacion de Count-Min Sketch (CMS) y CountSketch (CS).

#ifndef SKETCHES_HPP
#define SKETCHES_HPP

#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

// Hash universal h(x) = ((a*x + b) mod p) mod w, con p = 2^61 - 1.
// Asume claves de hasta 32 bits (las IPv4 que se usan en toda la tarea).
class UniversalHash {
public:
    static constexpr uint64_t kMersennePrime = (1ULL << 61) - 1;

    UniversalHash() : a_(0), b_(0), w_(1) {}

    // rng debe venir ya inicializado con la semilla del sketch que la usa,
    // de modo que cada fila saque un (a, b) distinto de la misma secuencia.
    UniversalHash(std::mt19937_64 &rng, uint64_t w) : w_(w) {
        std::uniform_int_distribution<uint64_t> dist_a(1, kMersennePrime - 1);
        std::uniform_int_distribution<uint64_t> dist_b(0, kMersennePrime - 1);
        a_ = dist_a(rng);
        b_ = dist_b(rng);
    }

    inline uint64_t operator()(uint64_t x) const {
        __uint128_t prod = (__uint128_t)a_ * (__uint128_t)x + (__uint128_t)b_;
        uint64_t lo = (uint64_t)(prod & kMersennePrime);
        uint64_t hi = (uint64_t)(prod >> 61);
        uint64_t r = lo + hi;
        if (r >= kMersennePrime) r -= kMersennePrime;
        return r % w_;
    }

    uint64_t a() const { return a_; }
    uint64_t b() const { return b_; }
    uint64_t w() const { return w_; }

private:
    uint64_t a_, b_, w_;
};

// Combina una semilla base con un ancho w, para poder generar sketches
// independientes al comparar distintos w con la misma semilla base.
inline uint64_t derive_seed(uint64_t base_seed, uint64_t w) {
    uint64_t h = base_seed ^ (w * 0x9E3779B97F4A7C15ULL);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h;
}

// Mediana de un vector de enteros con signo.
inline int64_t median_of(std::vector<int64_t> vals) {
    std::sort(vals.begin(), vals.end());
    size_t n = vals.size();
    if (n % 2 == 1) return vals[n / 2];
    return (vals[n / 2 - 1] + vals[n / 2]) / 2;
}

// Count-Min Sketch: matriz de d filas por w columnas, una funcion hash
// independiente por fila. Los contadores son con signo (int64_t) porque
// el sketch se va a sumar y restar con otros al mantener la ventana
// deslizante.
class CountMinSketch {
public:
    CountMinSketch() = default;

    CountMinSketch(int d, int w, uint64_t seed) : d_(d), w_(w) {
        if (d <= 0 || w <= 0) {
            throw std::invalid_argument("CountMinSketch: d y w deben ser positivos");
        }
        std::mt19937_64 rng(seed);
        hashes_.reserve((size_t)d_);
        for (int j = 0; j < d_; ++j) {
            hashes_.emplace_back(rng, (uint64_t)w_);
        }
        counters_.assign((size_t)d_ * (size_t)w_, 0);
    }

    // Sketch nuevo con las mismas funciones hash que este pero contadores
    // en cero. Es la forma correcta de crear un sketch que despues se
    // pueda sumar o restar con este.
    CountMinSketch clone_empty() const {
        CountMinSketch other;
        other.d_ = d_;
        other.w_ = w_;
        other.hashes_ = hashes_;
        other.counters_.assign((size_t)d_ * (size_t)w_, 0);
        return other;
    }

    // Inserta una ocurrencia de la clave x con peso c.
    inline void update(uint64_t x, int64_t c = 1) {
        for (int j = 0; j < d_; ++j) {
            counters_[index(j, hashes_[j](x))] += c;
        }
    }

    // Estimador estandar: minimo de los d contadores.
    int64_t estimate(uint64_t x) const {
        int64_t best = INT64_MAX;
        for (int j = 0; j < d_; ++j) {
            int64_t v = counters_[index(j, hashes_[j](x))];
            if (v < best) best = v;
        }
        return best;
    }

    // Estimador alternativo con mediana en vez de minimo, pensado para
    // usarse sobre un sketch de diferencias (puede tener entradas
    // negativas, donde el minimo no tiene sentido).
    int64_t estimate_median(uint64_t x) const {
        std::vector<int64_t> vals((size_t)d_);
        for (int j = 0; j < d_; ++j) {
            vals[(size_t)j] = counters_[index(j, hashes_[j](x))];
        }
        return median_of(std::move(vals));
    }

    CountMinSketch &operator+=(const CountMinSketch &other) {
        check_compatible(other);
        for (size_t i = 0; i < counters_.size(); ++i) counters_[i] += other.counters_[i];
        return *this;
    }

    CountMinSketch &operator-=(const CountMinSketch &other) {
        check_compatible(other);
        for (size_t i = 0; i < counters_.size(); ++i) counters_[i] -= other.counters_[i];
        return *this;
    }

    void clear() { std::fill(counters_.begin(), counters_.end(), 0); }

    int d() const { return d_; }
    int w() const { return w_; }

    size_t memory_bytes() const { return counters_.size() * sizeof(int64_t); }

private:
    inline size_t index(int row, uint64_t col) const {
        return (size_t)row * (size_t)w_ + (size_t)col;
    }

    void check_compatible(const CountMinSketch &other) const {
        if (d_ != other.d_ || w_ != other.w_) {
            throw std::logic_error(
                "CountMinSketch: no se pueden combinar sketches con distinto d o w");
        }
        for (int j = 0; j < d_; ++j) {
            if (hashes_[(size_t)j].a() != other.hashes_[(size_t)j].a() ||
                hashes_[(size_t)j].b() != other.hashes_[(size_t)j].b()) {
                throw std::logic_error(
                    "CountMinSketch: los sketches tienen funciones hash distintas");
            }
        }
    }

    int d_ = 0, w_ = 0;
    std::vector<UniversalHash> hashes_;
    std::vector<int64_t> counters_;
};

// CountSketch: igual estructura que CMS, pero cada fila ademas tiene una
// funcion de signo s_j(x) en {-1, +1}. La estimacion es la mediana de
// s_j(x) * C[j, h_j(x)] sobre las d filas.
class CountSketch {
public:
    CountSketch() = default;

    CountSketch(int d, int w, uint64_t seed) : d_(d), w_(w) {
        if (d <= 0 || w <= 0) {
            throw std::invalid_argument("CountSketch: d y w deben ser positivos");
        }
        std::mt19937_64 rng(seed);
        pos_hashes_.reserve((size_t)d_);
        sign_hashes_.reserve((size_t)d_);
        for (int j = 0; j < d_; ++j) {
            pos_hashes_.emplace_back(rng, (uint64_t)w_);
            // Hash de signo: sale hacia {0, 1}; 0 se interpreta como -1.
            sign_hashes_.emplace_back(rng, 2);
        }
        counters_.assign((size_t)d_ * (size_t)w_, 0);
    }

    CountSketch clone_empty() const {
        CountSketch other;
        other.d_ = d_;
        other.w_ = w_;
        other.pos_hashes_ = pos_hashes_;
        other.sign_hashes_ = sign_hashes_;
        other.counters_.assign((size_t)d_ * (size_t)w_, 0);
        return other;
    }

    inline void update(uint64_t x, int64_t c = 1) {
        for (int j = 0; j < d_; ++j) {
            counters_[index(j, pos_hashes_[j](x))] += sign_of(j, x) * c;
        }
    }

    // Estimador estandar, sin truncar a no negativo (se necesita asi para
    // poder usarlo tambien sobre diferencias, que pueden ser negativas).
    int64_t estimate(uint64_t x) const {
        std::vector<int64_t> vals((size_t)d_);
        for (int j = 0; j < d_; ++j) {
            vals[(size_t)j] = sign_of(j, x) * counters_[index(j, pos_hashes_[j](x))];
        }
        return median_of(std::move(vals));
    }

    // Igual que estimate(), truncado a cero. Para reportar frecuencias
    // absolutas, que nunca son negativas.
    int64_t estimate_nonnegative(uint64_t x) const {
        int64_t v = estimate(x);
        return v < 0 ? 0 : v;
    }

    CountSketch &operator+=(const CountSketch &other) {
        check_compatible(other);
        for (size_t i = 0; i < counters_.size(); ++i) counters_[i] += other.counters_[i];
        return *this;
    }

    CountSketch &operator-=(const CountSketch &other) {
        check_compatible(other);
        for (size_t i = 0; i < counters_.size(); ++i) counters_[i] -= other.counters_[i];
        return *this;
    }

    void clear() { std::fill(counters_.begin(), counters_.end(), 0); }

    int d() const { return d_; }
    int w() const { return w_; }

    size_t memory_bytes() const { return counters_.size() * sizeof(int64_t); }

private:
    inline size_t index(int row, uint64_t col) const {
        return (size_t)row * (size_t)w_ + (size_t)col;
    }

    inline int64_t sign_of(int row, uint64_t x) const {
        return sign_hashes_[(size_t)row](x) == 0 ? -1 : 1;
    }

    void check_compatible(const CountSketch &other) const {
        if (d_ != other.d_ || w_ != other.w_) {
            throw std::logic_error(
                "CountSketch: no se pueden combinar sketches con distinto d o w");
        }
        for (int j = 0; j < d_; ++j) {
            const auto &ph = pos_hashes_[(size_t)j];
            const auto &oph = other.pos_hashes_[(size_t)j];
            const auto &sh = sign_hashes_[(size_t)j];
            const auto &osh = other.sign_hashes_[(size_t)j];
            if (ph.a() != oph.a() || ph.b() != oph.b() ||
                sh.a() != osh.a() || sh.b() != osh.b()) {
                throw std::logic_error(
                    "CountSketch: los sketches tienen funciones hash distintas");
            }
        }
    }

    int d_ = 0, w_ = 0;
    std::vector<UniversalHash> pos_hashes_;
    std::vector<UniversalHash> sign_hashes_;
    std::vector<int64_t> counters_;
};

#endif // SKETCHES_HPP
