#pragma once

#include <string>
#include <vector>

#include "common/util/FileUtil.h"

#ifdef _WIN32
void win_print_last_error(const std::string& msg, const std::string& renderer);
void copy_texture_to_clipboard(int width,
                               int height,
                               const std::vector<u32>& texture_data,
                               const std::string& renderer);
#endif
