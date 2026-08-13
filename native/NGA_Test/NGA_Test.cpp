// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include "NGAforUnity.h"
#include <nlohmann/include/json.hpp>
using json = nlohmann::json;

static const char* SDK_ROOT = "C:/Work/Personal/NvidiaAceForUnity/NGAforUnity/native/external/game-agent-sdk";

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);++fails;} else printf("  [ok]   %s\n",m);}while(0)


static void __cdecl onToken(const char* tok, void* user) {
    printf("%s", tok);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// F5 hardening: every public function must reject bad input with a clean code
// (never crash) and populate Nga_LastError. None of these need the model loaded.
// ---------------------------------------------------------------------------
static void runHardening() {
    printf("\n== F5 hardening (error paths, no runtime needed) ==\n");
    const int before = fails;

    CHECK(Nga_AbiVersion() == NGA_ABI_VERSION, "AbiVersion matches header");

    char   buf[8] = {};
    int32_t w = 0;

    // Runtime
    NgaRuntimeHandle rt = nullptr;
    CHECK(NgaRuntime_Create(nullptr, &rt)               == NGA_ERR_INVALID_ARG,  "Runtime_Create(null cfg) -> INVALID_ARG");
    CHECK(NgaRuntime_Create("{ not json", &rt)          == NGA_ERR_INVALID_JSON, "Runtime_Create(bad json) -> INVALID_JSON");
    CHECK(NgaRuntime_Create("{}", &rt)                  == NGA_ERR_INVALID_ARG,  "Runtime_Create(no slmPath) -> INVALID_ARG");
    CHECK(NgaRuntime_Create("{\"slmPath\":\"x\"}", nullptr) == NGA_ERR_INVALID_ARG, "Runtime_Create(null out) -> INVALID_ARG");
    CHECK(Nga_LastError()[0] != '\0', "LastError populated after a failure");
    CHECK(NgaRuntime_GetInfo(nullptr, buf, sizeof(buf), &w) == NGA_ERR_INVALID_ARG, "Runtime_GetInfo(null) -> INVALID_ARG");

    // Chat
    CHECK(NgaChat_Generate(nullptr, "[]", nullptr, buf, sizeof(buf), &w)              == NGA_ERR_INVALID_ARG, "Chat_Generate(null rt) -> INVALID_ARG");
    CHECK(NgaChat_GenerateStream(nullptr, "[]", nullptr, onToken, nullptr)            == NGA_ERR_INVALID_ARG, "Chat_GenerateStream(null rt) -> INVALID_ARG");
    CHECK(NgaChat_GenerateWithTools(nullptr, "[]", nullptr, nullptr, buf, sizeof(buf), &w) == NGA_ERR_INVALID_ARG, "Chat_GenerateWithTools(null rt) -> INVALID_ARG");

    // RAG
    NgaKbHandle kb = nullptr;
    CHECK(NgaRag_OpenKB(nullptr, "{}", &kb)                                       == NGA_ERR_INVALID_ARG, "Rag_OpenKB(null rt) -> INVALID_ARG");
    CHECK(NgaRag_Query(nullptr, "q", NGA_RAG_HYBRID, 3, buf, sizeof(buf), &w)     == NGA_ERR_INVALID_ARG, "Rag_Query(null kb) -> INVALID_ARG");

    // Agent
    NgaAgentHandle ag = nullptr;
    NgaStepKind kind;
    CHECK(NgaAgent_Create(nullptr, "{}", &ag)                        == NGA_ERR_INVALID_ARG, "Agent_Create(null rt) -> INVALID_ARG");
    CHECK(NgaAgent_AddTool(nullptr, "{}")                            == NGA_ERR_INVALID_ARG, "Agent_AddTool(null) -> INVALID_ARG");
    CHECK(NgaAgent_SendMessage(nullptr, "hi")                        == NGA_ERR_INVALID_ARG, "Agent_SendMessage(null) -> INVALID_ARG");
    CHECK(NgaAgent_Step(nullptr, &kind, buf, sizeof(buf), &w)        == NGA_ERR_INVALID_ARG, "Agent_Step(null) -> INVALID_ARG");
    CHECK(NgaAgent_ProvideToolResult(nullptr, "id", "{}")           == NGA_ERR_INVALID_ARG, "Agent_ProvideToolResult(null) -> INVALID_ARG");
    CHECK(NgaAgent_GetHistory(nullptr, buf, sizeof(buf), &w)         == NGA_ERR_INVALID_ARG, "Agent_GetHistory(null) -> INVALID_ARG");

    // Destroy on null must be safe.
    NgaRuntime_Destroy(nullptr);
    NgaRag_CloseKB(nullptr);
    NgaAgent_Destroy(nullptr);
    printf("  [ok]   Destroy(null) x3 did not crash\n");

    printf("== hardening: %d new failure(s) ==\n", fails - before);
}

int main() {
    printf("== NGAforUnity smoke + hardening test ==\n");

    uint32_t abi = Nga_AbiVersion();
    printf("ABI = %u.%u\n", abi >> 16, abi & 0xFFFF);
    CHECK(abi == NGA_ABI_VERSION, "Nga_AbiVersion matches header");

    // F5: fast error-path battery (no GPU needed).
    runHardening();

    std::string root = SDK_ROOT;
    std::string slm = root + "/data/models/qwen3.5/Qwen3.5-4B-Q4_K_M.gguf";
    std::string emb = root + "/data/models/miniLM/all-MiniLM-L6-v2.F32.gguf";
    std::string cross = root + "/data/models/miniLM/cross_encoder_ms-marco-MiniLM-L6-v2.onnx";
    std::string cfg = std::string("{")
        + "\"slmPath\":\"" + slm + "\","
        + "\"embeddingModelPaths\":[\"" + emb + "\"],"
        + "\"crossEncoderModelPath\":\"" + cross + "\","
        + "\"maxContextSize\":4096,\"inferenceOutputBufferSize\":1024,\"logLevel\":1}";

    printf("\nLoading the SLM (the first time is slow: model -> GPU)...\n");

    NgaRuntimeHandle rt = nullptr;
    NgaResult r = NgaRuntime_Create(cfg.c_str(), &rt);
    if (r != NGA_OK)
    {
        printf("  [FAIL] Create=%d: %s\n", r, Nga_LastError());
        ++fails;
    }
    else {
        int32_t need = 0;
        CHECK(NgaRuntime_GetInfo(rt, nullptr, 0, &need) == NGA_ERR_BUFFER_SMALL && need > 0, "GetInfo reports required size");
        std::vector<char> buf(need); int32_t w = 0;
        CHECK(NgaRuntime_GetInfo(rt, buf.data(), need, &w) == NGA_OK, "GetInfo fills the buffer");
        printf("  info = %s\n", buf.data());

        // --- Chat API (enable if you want to exercise generation) ---
        //const char* messages = "[{\"role\":\"user\",\"content\":\"Say hi and introduce yourself.\"}]";
        //std::vector<char> reply(8192);
        //int32_t w2 = 0;
        //NgaResult cr = NgaChat_Generate(rt, messages, nullptr, reply.data(), (int32_t)reply.size(), &w2);
        //CHECK(cr == NGA_OK, "NgaChat_Generate result OK");
        //if (cr == NGA_OK) printf("Model response = %s\n", reply.data());

        // --- Autonomous agent with a search tool (RAG) ---
        printf("\n=== Agent ===\n");
        std::string kbcfg = std::string("{")
            + "\"semanticDbPath\":\"" + root + "/data/dbs/poker.db\","
            + "\"lexicalDbPath\":\"" + root + "/data/dbs/poker-lexical.db\","
            + "\"embeddingModelPath\":\"" + emb + "\"}";
        NgaKbHandle kb = nullptr;
        NgaRag_OpenKB(rt, kbcfg.c_str(), &kb);

        std::string agentCfg = std::string("{")
            + "\"id\":\"poker\","
            + "\"instructions\":\"You are a helpful poker assistant. Use the search tool to look up poker knowledge before answering. Keep answers under 40 words.\","
            + "\"maxSteps\":6}";
        NgaAgentHandle ag = nullptr;
        NgaResult ar = NgaAgent_Create(rt, agentCfg.c_str(), &ag);
        CHECK(ar == NGA_OK, "NgaAgent_Create");
        if (ar == NGA_OK) {
            const char* searchTool =
                "{\"name\":\"search\",\"description\":\"Search the poker knowledge base for rules, rankings, odds, strategy.\","
                "\"parameters\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"the search query\"}},\"required\":[\"query\"]}}";
            NgaAgent_AddTool(ag, searchTool);

            NgaAgent_SendMessage(ag, "What hand beats a full house? Answer briefly.");

            std::vector<char> sbuf(16384);
            for (int step = 0; step < 8; ++step) {
                NgaStepKind kind; int32_t sw = 0;
                NgaResult sr = NgaAgent_Step(ag, &kind, sbuf.data(), (int32_t)sbuf.size(), &sw);
                if (sr != NGA_OK) { printf("  step error: %s\n", Nga_LastError()); break; }

                if (kind == NGA_STEP_MESSAGE) {
                    json j = json::parse(sbuf.data());
                    printf("AGENT: %s\n", j.value("content", std::string()).c_str());
                    break;
                }
                else if (kind == NGA_STEP_TOOL_CALL) {
                    json j = json::parse(sbuf.data());
                    for (auto& tc : j["toolCalls"]) {
                        std::string id = tc.value("id", std::string());
                        std::string name = tc.value("name", std::string());
                        std::string args = tc.value("arguments", std::string());
                        printf("  [tool] %s(%s)\n", name.c_str(), args.c_str());
                        std::string result = "{}";
                        if (name == "search" && kb) {
                            std::string q;
                            try { q = json::parse(args).value("query", std::string()); }
                            catch (...) {}
                            std::vector<char> rr(8192); int32_t rw = 0;
                            if (NgaRag_Query(kb, q.c_str(), NGA_RAG_HYBRID, 3, rr.data(), (int32_t)rr.size(), &rw) == NGA_OK)
                                result = rr.data();
                        }
                        NgaAgent_ProvideToolResult(ag, id.c_str(), result.c_str());
                    }
                }
                else { // NGA_STEP_DONE
                    break;
                }
            }
            NgaAgent_Destroy(ag);
        }
        NgaRag_CloseKB(kb);

        NgaRuntime_Destroy(rt);
        printf("  [ok]   Destroy\n");
    }
    printf("\n== %s (%d fails) ==\n", fails == 0 ? "PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
