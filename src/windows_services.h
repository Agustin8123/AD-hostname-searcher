#pragma once
#include "core.h"
#include <atomic>
namespace ad {
std::vector<Computer> queryDirectory(std::atomic_bool& cancel);
void ping(Row& row);
}
