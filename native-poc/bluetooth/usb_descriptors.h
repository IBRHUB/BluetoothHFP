#pragma once
#include <cstdint>
#include <ostream>
#include <vector>
namespace ax201 {
struct Layout { bool hci = false; bool sco = false; };
// Validates lengths before inspecting descriptors; never changes alternate settings.
Layout describe_configuration(const std::vector<uint8_t>& bytes, std::ostream& log);
}
