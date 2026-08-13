// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include <cstring>

NgaResult nga_copy_utf8(const std::string& src, char* buf, int32_t len, int32_t* written) 
{
    const int32_t required = (int32_t)src.size() + 1;

    if (written) *written = required;

    if (!buf || len < required) {
        nga_set_last_error("output buffer too small (need " + std::to_string(required) + ")");
        return NGA_ERR_BUFFER_SMALL;
    }

    std::memcpy(buf, src.data(), src.size());
    buf[src.size()] = '\0';

    return NGA_OK;
}