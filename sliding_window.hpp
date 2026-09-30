#ifndef SLIDING_WINDOW_HPP
#define SLIDING_WINDOW_HPP

#include <cstdint>
#include <stdexcept>
#include <vector>
#include "sketches.hpp"

// Ventana deslizante fija de 60 s, evaluada cada 10 s.
// Mantiene 6 sub-sketches y un sketch agregado A.
// La clase usa la interfaz comun de CountMinSketch y CountSketch.
template <typename Sketch>
class SlidingWindow {
public:
    static constexpr uint64_t SUBWINDOW_US = 10ULL * 1000000ULL;
    static constexpr int NUM_SUBWINDOWS = 6;
    static constexpr uint64_t WINDOW_US = NUM_SUBWINDOWS * SUBWINDOW_US;

    SlidingWindow(int d, int w, uint64_t seed)
        : aggregate_(d, w, seed),
          initialized_(false),
          current_q_(0),
          t0_us_(0),
          total_n_(0),
          seed_(seed) {
        for (int i = 0; i < NUM_SUBWINDOWS; ++i) {
            ring_[i] = aggregate_.clone_empty();
            counts_[i] = 0;
            q_in_slot_[i] = 0;
        }
    }

    // Agrega un paquete cuya marca de tiempo esta en la traza.
    // La primera llamada fija t0. Los paquetes con timestamp == t0 no
    // pertenecen a (t0, t0 + 10], de acuerdo con la convencion del enunciado.
    void add(uint64_t timestamp_us, uint64_t key) {
        if (!initialized_) {
            initialized_ = true;
            t0_us_ = timestamp_us;
            return;
        }

        if (timestamp_us < t0_us_) {
            throw std::runtime_error("SlidingWindow requiere timestamps no decrecientes");
        }
        if (timestamp_us == t0_us_) {
            return;
        }

        uint64_t elapsed = timestamp_us - t0_us_;
        uint64_t q = (elapsed + SUBWINDOW_US - 1) / SUBWINDOW_US; // ceil(elapsed/p)
        if (q == 0) return;

        // Si la nueva subventana es mas de una posicion posterior, hay que
        // rotar las posiciones intermedias aunque no tengan paquetes.
        if (current_q_ == 0) {
            current_q_ = q;
            if (q > 1) {
                // No deberia ocurrir en una traza normal antes de llenar
                // la primera ventana, pero dejamos los slots vacios.
                current_q_ = q;
            }
        } else if (q > current_q_) {
            for (uint64_t next_q = current_q_ + 1; next_q <= q; ++next_q) {
                rotate_to(next_q);
            }
            current_q_ = q;
        }

        int slot = slot_for_q(q);
        ensure_slot_q(slot, q);
        ring_[slot].update(key, 1);
        aggregate_.update(key, 1);
        ++counts_[slot];
        ++total_n_;
    }

    // Avanza la ventana hasta el instante indicado y devuelve true cuando
    // ese instante corresponde a una evaluacion de 10 s. El llamador puede
    // consultar inmediatamente el agregado.
    bool advance_to(uint64_t evaluation_time_us) {
        if (!initialized_ || evaluation_time_us < t0_us_ + WINDOW_US) {
            return false;
        }
        uint64_t q = (evaluation_time_us - t0_us_) / SUBWINDOW_US;
        if (q < NUM_SUBWINDOWS) return false;

        while (current_q_ < q) {
            rotate_to(current_q_ + 1);
            current_q_++;
        }
        return true;
    }

    // Inicializa el anillo y el agregado para la primera evaluacion
    // tau_0 = t0 + 60 s. Se utiliza cuando se procesa una traza en orden.
    void finalize_initial_window() {
        if (!initialized_) return;
        if (current_q_ < NUM_SUBWINDOWS) {
            while (current_q_ < NUM_SUBWINDOWS) {
                rotate_to(current_q_ + 1);
                current_q_++;
            }
        }
    }

    // Estimacion estandar del sketch agregado.
    int64_t estimate(uint64_t key) const {
        return aggregate_.estimate(key);
    }

    uint64_t total_count() const { return total_n_; }
    uint64_t t0_us() const { return t0_us_; }
    uint64_t current_q() const { return current_q_; }
    bool initialized() const { return initialized_; }
    const Sketch &aggregate() const { return aggregate_; }
    size_t sketch_memory_bytes() const { return aggregate_.memory_bytes(); }

    // N de cada una de las 6 subventanas. Util para autoverificacion.
    uint64_t subwindow_count(int slot) const {
        if (slot < 0 || slot >= NUM_SUBWINDOWS) throw std::out_of_range("slot");
        return counts_[slot];
    }

private:
    int slot_for_q(uint64_t q) const {
        return static_cast<int>((q - 1) % NUM_SUBWINDOWS);
    }

    void ensure_slot_q(int slot, uint64_t q) {
        if (q_in_slot_[slot] == q) return;
        if (q_in_slot_[slot] != 0 && q_in_slot_[slot] != q) {
            // El slot solo se puede reutilizar despues de retirarlo del agregado.
            aggregate_ -= ring_[slot];
            ring_[slot].clear();
            total_n_ -= counts_[slot];
            counts_[slot] = 0;
        }
        q_in_slot_[slot] = q;
    }

    // Hace que el anillo pase a representar la subventana q.
    // La subventana que entra y la que expira comparten slot.
    void rotate_to(uint64_t q) {
        int slot = slot_for_q(q);
        ensure_slot_q(slot, q);
        // Si ensure_slot_q retiro un sketch viejo, el slot esta limpio.
        // No agregamos nada aqui: los paquetes de q se incorporan en add().
    }

    Sketch ring_[NUM_SUBWINDOWS];
    Sketch aggregate_;
    uint64_t counts_[NUM_SUBWINDOWS];
    uint64_t q_in_slot_[NUM_SUBWINDOWS];
    bool initialized_;
    uint64_t current_q_;
    uint64_t t0_us_;
    uint64_t total_n_;
    uint64_t seed_;
};

#endif
