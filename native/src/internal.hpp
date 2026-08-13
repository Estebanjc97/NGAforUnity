// SPDX-License-Identifier: Apache-2.0
#ifndef NGA_INTERNAL_HPP
    #define NGA_INTERNAL_HPP

#include <string>
#include <vector>
#include "NGAforUnity.h"
#include "ace.h"

struct NgaRuntime {
    ACEContext* ctx = nullptr;
    ACEModel* model = nullptr;
    std::string infoJson;
};

struct NgaKnowledgeBase {
    NgaRuntime* rt = nullptr;
    ACEDatabase* semantic = nullptr;
    ACEDatabase* lexical = nullptr;
};

struct NgaAgent {
    NgaRuntime* rt = nullptr;
    ACEAgent* agent = nullptr;
};

void        nga_set_last_error(const std::string& msg);
void        nga_clear_last_error();
const char* nga_get_last_error();
NgaResult   nga_translate_result(ACEResult aceSDKResult, const char* context);
NgaResult   nga_copy_utf8(const std::string& src, char* buf, int32_t len, int32_t* written);

#define NGA_TRY_BEGIN nga_clear_last_error(); try {
#define NGA_TRY_END \
    } catch (const std::exception& e) { nga_set_last_error(e.what()); return NGA_ERR_INTERNAL; } \
      catch (...) { nga_set_last_error("unknown non-std exception at ABI boundary"); return NGA_ERR_INTERNAL; }
#endif