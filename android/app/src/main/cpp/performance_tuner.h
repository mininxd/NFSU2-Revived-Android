#pragma once
#include <cstdint>
#include <string>

namespace nfs_perf {

enum class ThreadRole {
    RENDER_MAIN,
    GUEST_AUDIO,
    BACKGROUND_WORKER
};

// Probe SoC CPU topology (identifying prime/performance cores vs efficiency cores).
void initTopology();

// Pin affinity and configure niceness/priority according to the thread's operational role.
void tuneThread(ThreadRole role, const char* name = nullptr);

// Report detected CPU cluster configuration for diagnostics.
std::string getTopologySummary();

} // namespace nfs_perf
