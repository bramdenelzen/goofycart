#pragma once
#include <string>

namespace gk::sim {

// Finds a file under the game's data directory: $GK_DATA, ./data, the
// executable's directory, then the source tree (development builds).
std::string data_path(const std::string& name);

} // namespace gk::sim
