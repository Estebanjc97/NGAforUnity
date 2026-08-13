// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include "NGAforUnity.h"

static const char* SDK_ROOT = "C:/Work/Personal/NvidiaAceForUnity/NGAforUnity/native/external/game-agent-sdk";

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){printf("  [FAIL] %s\n",m);++fails;} else printf("  [ok]   %s\n",m);}while(0)

int main() {
    printf("== NGAforUnity F1 smoke test ==\n");

    uint32_t abi = Nga_AbiVersion();
    printf("ABI = %u.%u\n", abi >> 16, abi & 0xFFFF);
    CHECK(abi == NGA_ABI_VERSION, "Nga_AbiVersion header mathes");

    NgaRuntimeHandle rt = nullptr;
    CHECK(NgaRuntime_Create(nullptr, &rt) == NGA_ERR_INVALID_ARG, "config null -> INVALID_ARG");
    CHECK(NgaRuntime_Create("{ json error", &rt) == NGA_ERR_INVALID_JSON, "JSON error -> INVALID_JSON");
    CHECK(Nga_LastError()[0] != '\0', "Nga_LastError has error message");

    std::string slm = std::string(SDK_ROOT) + "/data/models/qwen3.5/Qwen3.5-4B-Q4_K_M.gguf";
    std::string cfg = "{\"slmPath\":\"" + slm + "\",\"maxContextSize\":4096,\"inferenceOutputBufferSize\":1024,\"logLevel\":1}";
    printf("Loading the SLM (The first time is slow: model -> GPU)...\n");

    NgaResult r = NgaRuntime_Create(cfg.c_str(), &rt);
    if (r != NGA_OK) { printf("  [FAIL] Create=%d: %s\n", r, Nga_LastError()); ++fails; }
    else {
        int32_t need = 0;
        CHECK(NgaRuntime_GetInfo(rt, nullptr, 0, &need) == NGA_ERR_BUFFER_SMALL && need > 0, "GetInfo size info");
        std::vector<char> buf(need); int32_t w = 0;
        CHECK(NgaRuntime_GetInfo(rt, buf.data(), need, &w) == NGA_OK, "GetInfo fill the buffer");
        printf("  info = %s\n", buf.data());
        NgaRuntime_Destroy(rt);
        printf("  [ok]   Destroy\n");
    }
    printf("== %s (%d fails) ==\n", fails == 0 ? "PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}