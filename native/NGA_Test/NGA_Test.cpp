// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include "NGAforUnity.h"

static const char* SDK_ROOT = "C:/Work/Personal/NvidiaAceForUnity/NGAforUnity/native/external/game-agent-sdk";

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);++fails;} else printf("  [ok]   %s\n",m);}while(0)


static void __cdecl onToken(const char* tok, void* user) {
    printf("%s", tok);
    fflush(stdout);
}

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
    if (r != NGA_OK) 
    { 
        printf("  [FAIL] Create=%d: %s\n", r, Nga_LastError()); 
        ++fails; 
    }
    else {
        int32_t need = 0;
        CHECK(NgaRuntime_GetInfo(rt, nullptr, 0, &need) == NGA_ERR_BUFFER_SMALL && need > 0, "GetInfo size info");
        std::vector<char> buf(need); int32_t w = 0;
        CHECK(NgaRuntime_GetInfo(rt, buf.data(), need, &w) == NGA_OK, "GetInfo fill the buffer");
        printf("  info = %s\n", buf.data());
        
        // --- API Chat ---
        printf("\nGenerating model response...\n");
        const char* messages = "[{\"role\":\"user\",\"content\":\"Say hi and intrudece yourself.\"}]";
        std::vector<char> reply(8192);   // Response buffer
        int32_t w2 = 0;
        NgaResult cr = NgaChat_Generate(rt, messages, nullptr, reply.data(), (int32_t)reply.size(), &w2);
        CHECK(cr == NGA_OK, "NgaChat_Generate result OK");
        if (cr == NGA_OK) printf("Model response = %s\n", reply.data());
        else              printf("error: %s\n", Nga_LastError());

        printf("\nGenerating model response via streamming (token per token):\n");
        const char* messages2 = "[{\"role\":\"user\",\"content\":\"What is the difference between chat and streamming?.\"}]";
        NgaResult sr = NgaChat_GenerateStream(rt, messages2, nullptr, onToken, nullptr);
        printf("\n");
        CHECK(sr == NGA_OK, "NgaChat_GenerateStream result OK");

        printf("\nGenerating with tools:\n");
        const char* toolsDef =
            "[{\"name\":\"get_weather\",\"description\":\"Consulta el clima de una ciudad\","
            "\"parameters\":{\"type\":\"object\",\"properties\":{\"city\":{\"type\":\"string\",\"description\":\"Ciudad\"}},\"required\":[\"city\"]}}]";
        const char* toolMsgs = "[{\"role\":\"user\",\"content\":\"Que clima hace en Paris? Usa la herramienta.\"}]";
        std::vector<char> toolOut(8192);
        int32_t w3 = 0;
        NgaResult tr = NgaChat_GenerateWithTools(rt, toolMsgs, toolsDef, nullptr, toolOut.data(), (int32_t)toolOut.size(), &w3);
        CHECK(tr == NGA_OK, "NgaChat_GenerateWithTools result OK");
        if (tr == NGA_OK) printf("result = %s\n", toolOut.data());

        NgaRuntime_Destroy(rt);
        printf("  [ok]   Destroy\n");
    }
    printf("== %s (%d fails) ==\n", fails == 0 ? "PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}