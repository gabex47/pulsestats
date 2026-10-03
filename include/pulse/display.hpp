#pragma once

#include "pulse/stats.hpp"

#include <string>

namespace pulse {

std::string render_stats(const SystemStats& stats);
std::string help_text();

}  // namespace pulse
