#include "Sim/Network.h"
#include "Sim/Simulation.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <poll.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {
constexpr std::uint8_t StepOpcode = 1;
constexpr std::uint8_t CommandOpcode = 2;
constexpr std::uint8_t ForfeitOpcode = 3;
constexpr std::uint8_t SnapshotRequestOpcode = 4;
constexpr std::uint8_t ReadyOpcode = 128;
constexpr std::uint8_t SnapshotOpcode = 129;
constexpr std::uint8_t AcknowledgementOpcode = 130;
constexpr std::uint8_t ResultOpcode = 131;
constexpr std::size_t MaxInputFrame = cinder::net::MaxMessageBytes + 2;

std::uint32_t readU32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

bool writeAll(const std::uint8_t* bytes, std::size_t size) {
    while (size > 0) {
        const ssize_t written = ::write(STDOUT_FILENO, bytes, size);
        if (written > 0) {
            bytes += written;
            size -= static_cast<std::size_t>(written);
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool writeFrame(std::uint8_t opcode, const std::vector<std::uint8_t>& payload = {}) {
    if (payload.size() > std::numeric_limits<std::uint32_t>::max() - 1) return false;
    std::array<std::uint8_t, 5> header{};
    const auto length = static_cast<std::uint32_t>(payload.size() + 1);
    header[0] = static_cast<std::uint8_t>(length);
    header[1] = static_cast<std::uint8_t>(length >> 8);
    header[2] = static_cast<std::uint8_t>(length >> 16);
    header[3] = static_cast<std::uint8_t>(length >> 24);
    header[4] = opcode;
    return writeAll(header.data(), header.size()) &&
           (payload.empty() || writeAll(payload.data(), payload.size()));
}

bool parseUnsigned(std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

class MatchWorker {
public:
    MatchWorker(int map, std::uint32_t seed, int players) {
        simulation_.reset({map, seed, false, 1.0f, cinder::MatchLength::Standard, players});
    }

    bool start() {
        return writeFrame(ReadyOpcode) && sendSnapshots();
    }

    bool handle(std::uint8_t opcode, const std::uint8_t* data, std::size_t size) {
        if (opcode == StepOpcode) {
            if (size != 4) return protocolError("step body must be four bytes");
            const std::uint32_t count = readU32(data);
            if (count < 1 || count > 5) return protocolError("step count is outside 1..5");
            for (std::uint32_t index = 0; index < count && simulation_.winner() == -1; ++index) {
                simulation_.update(cinder::Simulation::Step);
            }
            return sendResultIfEnded();
        }
        if (opcode == CommandOpcode) {
            if (size < 2 || size - 1 > cinder::net::MaxMessageBytes) {
                return protocolError("command body is outside bounds");
            }
            const int seat = data[0];
            if (seat < 0 || seat >= simulation_.playerCount()) return protocolError("command seat is outside bounds");
            cinder::Command command;
            std::uint32_t sequence = commandSequence(data + 1, size - 1);
            std::string error;
            bool accepted = cinder::net::decodeCommand(data + 1, size - 1, command, sequence, error);
            if (accepted) accepted = cinder::net::translateCommand(simulation_, seat, views_[seat], command, error);
            cinder::CommandResult result;
            if (accepted) {
                if (command.queueMode == cinder::CommandQueueMode::Append ||
                    command.type == cinder::CommandType::Patrol ||
                    command.type == cinder::CommandType::Escort) {
                    // Appends and sustained orders can grow private tactical
                    // snapshots. Test the exact transition on copies, including
                    // every recipient's remembered resources and opaque handles.
                    // Do not install the candidate simulation after admission:
                    // its Navigation copy intentionally has isolated caches, and
                    // replacing the authority would discard the original's warm
                    // terrain caches on every admitted tactical command.
                    cinder::Simulation candidate = simulation_;
                    auto candidateViews = views_;
                    result = candidate.command(command);
                    if (result.accepted) {
                        for (int viewer = 0; viewer < candidate.playerCount(); ++viewer) {
                            auto snapshot = cinder::net::snapshotFor(candidate, viewer, &candidateViews[viewer]);
                            auto encoded = cinder::net::encodeSnapshot(snapshot);
                            if (encoded.empty() || encoded.size() > cinder::net::MaxMessageBytes) {
                                result = {false, "Queued orders would exceed the network snapshot limit."};
                                break;
                            }
                        }
                    }
                    // The worker is single-threaded and no authoritative state
                    // changes between the successful preflight and this call.
                    if (result.accepted) result = simulation_.command(command);
                } else {
                    result = simulation_.command(command);
                }
                // Local feedback may name authoritative entity IDs. Network
                // clients identify assignments from their opaque snapshot handles.
                if (result.accepted && command.type == cinder::CommandType::AutoBuild)
                    result.message = std::string(cinder::definition(command.kind).name)
                        + " assigned to a Drudge. Open JOBS to view it.";
                else if (result.accepted && command.type == cinder::CommandType::AutoResearch)
                    result.message = "Research assigned to a Resonator. Open JOBS to view it.";
            } else {
                result = {false, error.empty() ? "Malformed command." : error};
            }
            return sendAcknowledgement(seat, sequence, result) && sendResultIfEnded();
        }
        if (opcode == ForfeitOpcode) {
            if (size != 1 || data[0] >= simulation_.playerCount()) return protocolError("forfeit body is invalid");
            simulation_.forfeit(data[0]);
            return simulation_.winner() == -1 ? sendSnapshots() : sendResultIfEnded(data[0]);
        }
        if (opcode == SnapshotRequestOpcode) {
            if (size != 0) return protocolError("snapshot request body must be empty");
            return sendSnapshots();
        }
        return protocolError("unknown input opcode");
    }

private:
    cinder::Simulation simulation_;
    std::array<cinder::net::ViewMemory, cinder::Simulation::MaxPlayers> views_;
    bool resultSent_ = false;

    static bool protocolError(const char* message) {
        std::cerr << "Protocol error: " << message << '\n';
        return true;
    }

    static std::uint32_t commandSequence(const std::uint8_t* data, std::size_t size) {
        if (size < 12 || std::memcmp(data, "CCMD", 4) != 0) return 0;
        return readU32(data + 8);
    }

    bool sendSnapshots() {
        for (int seat = 0; seat < simulation_.playerCount(); ++seat) {
            auto snapshot = cinder::net::snapshotFor(simulation_, seat, &views_[seat]);
            auto encoded = cinder::net::encodeSnapshot(snapshot);
            if (encoded.empty() || encoded.size() > cinder::net::MaxMessageBytes) {
                std::cerr << "Could not encode a bounded snapshot\n";
                return false;
            }
            std::vector<std::uint8_t> payload;
            payload.reserve(encoded.size() + 1);
            payload.push_back(static_cast<std::uint8_t>(seat));
            payload.insert(payload.end(), encoded.begin(), encoded.end());
            if (!writeFrame(SnapshotOpcode, payload)) return false;
        }
        return true;
    }

    bool sendAcknowledgement(int seat, std::uint32_t sequence, const cinder::CommandResult& result) {
        const std::size_t messageSize = std::min<std::size_t>(result.message.size(), 65535);
        std::vector<std::uint8_t> payload;
        payload.reserve(8 + messageSize);
        payload.push_back(static_cast<std::uint8_t>(seat));
        appendU32(payload, sequence);
        payload.push_back(result.accepted ? 1 : 0);
        appendU16(payload, static_cast<std::uint16_t>(messageSize));
        payload.insert(payload.end(), result.message.begin(), result.message.begin() + messageSize);
        return writeFrame(AcknowledgementOpcode, payload);
    }

    bool sendResultIfEnded(std::uint8_t causeSeat = 255) {
        if (simulation_.winner() == -1 || resultSent_) return true;
        if (!sendSnapshots()) return false;
        resultSent_ = true;
        const auto winner = simulation_.winner() == -2 ? std::uint8_t{255} : static_cast<std::uint8_t>(simulation_.winner());
        return writeFrame(ResultOpcode, {winner, causeSeat});
    }
};

void usage() {
    std::cerr << "Usage: CinderlineMatchWorker --map 0..2 --seed UINT32 [--players 2|4]\n";
}
} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    int map = 0;
    std::uint32_t seed = 0;
    int players = 2;
    bool haveMap = false;
    bool haveSeed = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if ((argument == "--map" || argument == "--seed" || argument == "--players") && index + 1 < argc) {
            std::uint32_t value = 0;
            if (!parseUnsigned(argv[++index], value)) {
                usage();
                return 2;
            }
            if (argument == "--map") {
                if (value > 2) {
                    usage();
                    return 2;
                }
                map = static_cast<int>(value);
                haveMap = true;
            } else if (argument == "--seed") {
                seed = value;
                haveSeed = true;
            } else {
                if (value != 2 && value != 4) {
                    usage();
                    return 2;
                }
                players = static_cast<int>(value);
            }
        } else {
            usage();
            return 2;
        }
    }
    if (!haveMap || !haveSeed) {
        usage();
        return 2;
    }

    MatchWorker worker(map, seed, players);
    if (!worker.start()) return 1;

    std::vector<std::uint8_t> input;
    input.reserve(64 * 1024);
    std::array<std::uint8_t, 64 * 1024> chunk{};
    while (true) {
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        int ready = ::poll(&descriptor, 1, -1);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) {
            std::cerr << "stdin poll failed: " << std::strerror(errno) << '\n';
            return 1;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            std::cerr << "stdin poll reported an error\n";
            return 1;
        }
        if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) continue;
        const ssize_t count = ::read(STDIN_FILENO, chunk.data(), chunk.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            std::cerr << "stdin read failed: " << std::strerror(errno) << '\n';
            return 1;
        }
        if (count == 0) {
            if (!input.empty()) std::cerr << "stdin ended with a partial frame\n";
            return input.empty() ? 0 : 1;
        }
        input.insert(input.end(), chunk.begin(), chunk.begin() + count);
        std::size_t consumed = 0;
        while (input.size() - consumed >= 4) {
            const std::uint32_t length = readU32(input.data() + consumed);
            if (length < 1 || length > MaxInputFrame) {
                std::cerr << "Invalid input frame length\n";
                return 1;
            }
            if (input.size() - consumed < 4 + length) break;
            const std::uint8_t* frame = input.data() + consumed + 4;
            if (!worker.handle(frame[0], frame + 1, length - 1)) return 1;
            consumed += 4 + length;
        }
        if (consumed > 0) input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(consumed));
        if (input.size() > MaxInputFrame + 4) {
            std::cerr << "Input buffer exceeded the protocol limit\n";
            return 1;
        }
    }
}
