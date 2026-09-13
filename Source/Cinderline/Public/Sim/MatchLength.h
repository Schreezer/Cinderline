#pragma once

namespace cinder {

enum class MatchLength : int { Short, Standard, Long, Count };

struct MatchLengthProfile {
    float worldSize;
    float homeNodeOre;
    float expansionNodeOre;
    float productionTimeScale;
};

constexpr MatchLength matchLengthAt(int index) {
    return index >= 0 && index < static_cast<int>(MatchLength::Count)
        ? static_cast<MatchLength>(index)
        : MatchLength::Standard;
}

constexpr const char* matchLengthName(MatchLength length) {
    switch (length) {
        case MatchLength::Short: return "Short";
        case MatchLength::Long: return "Long";
        case MatchLength::Standard:
        case MatchLength::Count: return "Standard";
    }
    return "Standard";
}

constexpr const char* matchLengthDescription(MatchLength length) {
    switch (length) {
        case MatchLength::Short: return "Compact battlefield, leaner ore, and faster production.";
        case MatchLength::Long: return "Expanded battlefield with deeper finite ore reserves.";
        case MatchLength::Standard:
        case MatchLength::Count: return "The original battlefield, economy, and production pace.";
    }
    return "The original battlefield, economy, and production pace.";
}

constexpr MatchLengthProfile matchLengthProfile(MatchLength length) {
    switch (length) {
        case MatchLength::Short: return {3600.0f, 1500.0f, 2500.0f, 0.8f};
        case MatchLength::Long: return {6000.0f, 3750.0f, 6300.0f, 1.0f};
        case MatchLength::Standard:
        case MatchLength::Count: return {4800.0f, 2500.0f, 4200.0f, 1.0f};
    }
    return {4800.0f, 2500.0f, 4200.0f, 1.0f};
}

} // namespace cinder
