#pragma once
#include "Sim/Network.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace cinder::baseline {

// Diagnostics owned by the benchmark. No observer state enters the simulation.
class SnapshotMetrics {
public:
    // Call once per two 20 Hz ticks to match the server's default 10 Hz cadence.
    void sample(const Simulation& simulation,
                std::array<net::ViewMemory, Simulation::MaxPlayers>& views) {
        const auto batchStarted = Clock::now();
        std::size_t batchBytes = 0;
        for (int seat = 0; seat < simulation.playerCount(); ++seat) {
            const auto started = Clock::now();
            const auto snapshot = net::snapshotFor(simulation, seat, &views[seat]);
            const auto built = Clock::now();
            const auto encoded = net::encodeSnapshot(snapshot);
            const auto finished = Clock::now();
            if (encoded.empty() || encoded.size() > net::MaxMessageBytes)
                throw std::runtime_error("legal match produced an invalid or oversized snapshot");
            buildMs_.push_back(milliseconds(built - started));
            encodeMs_.push_back(milliseconds(finished - built));
            messageMs_.push_back(milliseconds(finished - started));
            bytes_.push_back(static_cast<double>(encoded.size()));
            entities_.push_back(static_cast<double>(snapshot.entities.size()));
            effects_.push_back(static_cast<double>(snapshot.effects.size()));
            batchBytes += encoded.size();
        }
        batchMs_.push_back(milliseconds(Clock::now() - batchStarted));
        batchBytes_.push_back(static_cast<double>(batchBytes));
    }

    void writeJson(std::ostream& out) const {
        out << "{\"scope\":\"fog_filtered_opaque_snapshots_cpu_no_transport\","
               "\"cadence_hz\":10,\"protocol\":" << net::ProtocolVersion
            << ",\"message_byte_limit\":" << net::MaxMessageBytes
            << ",\"batches\":" << batchMs_.size() << ",\"messages\":" << bytes_.size();
        writeMetric(out, "build_ms", buildMs_);
        writeMetric(out, "encode_ms", encodeMs_);
        writeMetric(out, "message_cpu_ms", messageMs_);
        writeMetric(out, "all_seats_batch_cpu_ms", batchMs_);
        writeMetric(out, "message_bytes", bytes_);
        writeMetric(out, "all_seats_batch_bytes", batchBytes_);
        writeMetric(out, "visible_entities", entities_);
        writeMetric(out, "visible_effects", effects_);
        out << '}';
    }

private:
    using Clock = std::chrono::steady_clock;
    std::vector<double> buildMs_, encodeMs_, messageMs_, batchMs_;
    std::vector<double> bytes_, batchBytes_, entities_, effects_;

    static double milliseconds(Clock::duration duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    }
    static void writeMetric(std::ostream& out, const char* name, std::vector<double> values) {
        out << ",\"" << name << "\":";
        if (values.empty()) { out << "null"; return; }
        std::sort(values.begin(), values.end());
        double total = 0;
        for (double value : values) total += value;
        const auto percentile = [&](double fraction) {
            return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
        };
        out << "{\"mean\":" << total / values.size() << ",\"p50\":" << percentile(0.50)
            << ",\"p95\":" << percentile(0.95) << ",\"p99\":" << percentile(0.99)
            << ",\"max\":" << values.back() << '}';
    }
};
}
