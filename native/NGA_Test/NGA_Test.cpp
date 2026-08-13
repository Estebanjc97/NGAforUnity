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

int main() {
    printf("== NGAforUnity F1 smoke test ==\n");

    uint32_t abi = Nga_AbiVersion();
    printf("ABI = %u.%u\n", abi >> 16, abi & 0xFFFF);
    CHECK(abi == NGA_ABI_VERSION, "Nga_AbiVersion header mathes");

    NgaRuntimeHandle rt = nullptr;
    CHECK(NgaRuntime_Create(nullptr, &rt) == NGA_ERR_INVALID_ARG, "config null -> INVALID_ARG");
    CHECK(NgaRuntime_Create("{ json error", &rt) == NGA_ERR_INVALID_JSON, "JSON error -> INVALID_JSON");
    CHECK(Nga_LastError()[0] != '\0', "Nga_LastError has error message");

    std::string root = SDK_ROOT;
    std::string slm = root + "/data/models/qwen3.5/Qwen3.5-4B-Q4_K_M.gguf";
    std::string emb = root + "/data/models/miniLM/all-MiniLM-L6-v2.F32.gguf";
    std::string cross = root + "/data/models/miniLM/cross_encoder_ms-marco-MiniLM-L6-v2.onnx";
    std::string cfg = std::string("{")
        + "\"slmPath\":\"" + slm + "\","
        + "\"embeddingModelPaths\":[\"" + emb + "\"],"
        + "\"crossEncoderModelPath\":\"" + cross + "\","
        + "\"maxContextSize\":4096,\"inferenceOutputBufferSize\":1024,\"logLevel\":1}";


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
        
        // --- Chat API ---
        //printf("\nGenerating model response...\n");
        //const char* messages = "[{\"role\":\"user\",\"content\":\"Say hi and intrudece yourself.\"}]";
        //std::vector<char> reply(8192);   // Response buffer
        //int32_t w2 = 0;
        //NgaResult cr = NgaChat_Generate(rt, messages, nullptr, reply.data(), (int32_t)reply.size(), &w2);
        //CHECK(cr == NGA_OK, "NgaChat_Generate result OK");
        //if (cr == NGA_OK) printf("Model response = %s\n", reply.data());
        //else              printf("error: %s\n", Nga_LastError());

        //printf("\nGenerating model response via streamming (token per token):\n");
        //const char* messages2 = "[{\"role\":\"user\",\"content\":\"Who made you?.\"}]";
        //NgaResult sr = NgaChat_GenerateStream(rt, messages2, nullptr, onToken, nullptr);
        //printf("\n");
        //CHECK(sr == NGA_OK, "NgaChat_GenerateStream result OK");

        //printf("\nGenerating with tools:\n");
        //const char* toolsDef =
        //    "[{\"name\":\"get_weather\",\"description\":\"Consulta el clima de una ciudad\","
        //    "\"parameters\":{\"type\":\"object\",\"properties\":{\"city\":{\"type\":\"string\",\"description\":\"Ciudad\"}},\"required\":[\"city\"]}}]";
        //const char* toolMsgs = "[{\"role\":\"user\",\"content\":\"Que clima hace en Paris? Usa la herramienta.\"}]";
        //std::vector<char> toolOut(8192);
        //int32_t w3 = 0;
        //NgaResult tr = NgaChat_GenerateWithTools(rt, toolMsgs, toolsDef, nullptr, toolOut.data(), (int32_t)toolOut.size(), &w3);
        //CHECK(tr == NGA_OK, "NgaChat_GenerateWithTools result OK");
        //if (tr == NGA_OK) printf("result = %s\n", toolOut.data());

        //// --- RAG API ---

        //printf("\nabriendo base de conocimiento (poker)...\n");
        //std::string kbcfg = std::string("{")
        //    + "\"semanticDbPath\":\"" + root + "/data/dbs/poker.db\","
        //    + "\"lexicalDbPath\":\"" + root + "/data/dbs/poker-lexical.db\","
        //    + "\"embeddingModelPath\":\"" + emb + "\"}";
        //NgaKbHandle kb = nullptr;
        //NgaResult kr = NgaRag_OpenKB(rt, kbcfg.c_str(), &kb);
        //CHECK(kr == NGA_OK, "NgaRag_OpenKB abre la KB");
        //if (kr == NGA_OK) {
        //    std::vector<char> rbuf(11252);
        //    int32_t rw = 0;
        //    NgaResult qr = NgaRag_Query(kb, "what hands beat a full house?", NGA_RAG_HYBRID, 3,
        //        rbuf.data(), (int32_t)rbuf.size(), &rw);
        //    CHECK(qr == NGA_OK, "NgaRag_Query (hybrid) devuelve OK");
        //    if (qr == NGA_OK) printf("resultados RAG = %s\n", rbuf.data());
        //    else              printf("  error: %s\n", Nga_LastError());
        //    NgaRag_CloseKB(kb);
        //}
        //else {
        //    printf("  error: %s\n", Nga_LastError());
        //}

        // --- Autonomus agent with quering tools (RAG) ---
        printf("\n=== Agent ===\n");
        std::string kbcfg2 = std::string("{")
            + "\"semanticDbPath\":\"" + root + "/data/dbs/poker.db\","
            + "\"lexicalDbPath\":\"" + root + "/data/dbs/poker-lexical.db\","
            + "\"embeddingModelPath\":\"" + emb + "\"}";
        NgaKbHandle kb2 = nullptr;
        NgaRag_OpenKB(rt, kbcfg2.c_str(), &kb2);

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
                    printf("AGENTE: %s\n", j.value("content", std::string()).c_str());
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
                        if (name == "search" && kb2) {
                            std::string q;
                            try { q = json::parse(args).value("query", std::string()); }
                            catch (...) {}
                            std::vector<char> rr(8192); int32_t rw = 0;
                            if (NgaRag_Query(kb2, q.c_str(), NGA_RAG_HYBRID, 3, rr.data(), (int32_t)rr.size(), &rw) == NGA_OK)
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
        NgaRag_CloseKB(kb2);

        NgaRuntime_Destroy(rt);
        printf("  [ok]   Destroy\n");
    }
    printf("== %s (%d fails) ==\n", fails == 0 ? "PASSED" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}