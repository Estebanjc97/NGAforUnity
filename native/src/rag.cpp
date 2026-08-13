// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include "nga_helpers.hpp"
#include <memory>
#include <nlohmann/include/json.hpp>
using json = nlohmann::json;

// Open a knowledge base (semantic and/or lexical index) from a JSON config.
extern "C" NGA_API NgaResult NgaRag_OpenKB(NgaRuntimeHandle runtime_handle, const char* kbConfigJson, NgaKbHandle* out)
{
    NGA_TRY_BEGIN
    if (!runtime_handle || !runtime_handle->ctx) { nga_set_last_error("runtime null/not initialized"); return NGA_ERR_INVALID_ARG; }
    if (!out) { nga_set_last_error("out is null"); return NGA_ERR_INVALID_ARG; }
    *out = nullptr;
    if (!kbConfigJson || !kbConfigJson[0]) { nga_set_last_error("kbConfigJson null/empty"); return NGA_ERR_INVALID_ARG; }

    json cfg;
    try { cfg = json::parse(kbConfigJson); }
    catch (const std::exception& e) { nga_set_last_error(std::string("kb JSON error: ") + e.what()); return NGA_ERR_INVALID_JSON; }

    std::string semPath = cfg.value("semanticDbPath", std::string());
    std::string lexPath = cfg.value("lexicalDbPath", std::string());
    std::string embPath = cfg.value("embeddingModelPath", std::string());
    if (semPath.empty() && lexPath.empty()) { nga_set_last_error("specify semanticDbPath and/or lexicalDbPath"); return NGA_ERR_INVALID_ARG; }

    auto kb = std::make_unique<NgaKnowledgeBase>();
    kb->rt = runtime_handle;

    if (!semPath.empty()) {
        AceDbParamsGuard params;
        ACEResult r = ace_createDatabaseLoadParams(runtime_handle->ctx, &params.p);
        if (r != ACEResultOk) return nga_translate_result(r, "ace_createDatabaseLoadParams");
        ace_databaseLoadParamsSetInt(params.p, ACEDatabaseLoadParam_IndexType, ACEIndexType_Semantic);
        if (!embPath.empty())
            ace_databaseLoadParamsSetString(params.p, ACEDatabaseLoadParam_EmbeddingModelPath, embPath.c_str());
        r = ace_loadDatabase(runtime_handle->ctx, &kb->semantic, semPath.c_str(), params.p);
        if (r != ACEResultOk) return nga_translate_result(r, "ace_loadDatabase(semantic)");
    }

    if (!lexPath.empty()) {
        AceDbParamsGuard params;
        ACEResult r = ace_createDatabaseLoadParams(runtime_handle->ctx, &params.p);
        if (r != ACEResultOk) { if (kb->semantic) ace_destroyDatabase(kb->semantic); return nga_translate_result(r, "ace_createDatabaseLoadParams"); }
        ace_databaseLoadParamsSetInt(params.p, ACEDatabaseLoadParam_IndexType, ACEIndexType_Lexical);
        r = ace_loadDatabase(runtime_handle->ctx, &kb->lexical, lexPath.c_str(), params.p);
        if (r != ACEResultOk) { if (kb->semantic) ace_destroyDatabase(kb->semantic); return nga_translate_result(r, "ace_loadDatabase(lexical)"); }
    }

    *out = kb.release();
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API void NgaRag_CloseKB(NgaKbHandle kb)
{
    if (!kb) return;
    try {
        if (kb->semantic) ace_destroyDatabase(kb->semantic);
        if (kb->lexical)  ace_destroyDatabase(kb->lexical);
    }
    catch (...) {}
    delete kb;
}

// Query the knowledge base. Returns JSON: [{"id","document","distance","numTokens"}, ...].
extern "C" NGA_API NgaResult NgaRag_Query(NgaKbHandle kb, const char* query, NgaRagMode mode, int32_t topK,
    char* outJson, int32_t len, int32_t* written)
{
    NGA_TRY_BEGIN
    if (!kb || !kb->rt || !kb->rt->ctx) { nga_set_last_error("kb null/not initialized"); return NGA_ERR_INVALID_ARG; }
    if (!query || !query[0]) { nga_set_last_error("query null/empty"); return NGA_ERR_INVALID_ARG; }

    AceSearchOptsGuard opts;
    ACEResult r = ace_createSearchOptions(kb->rt->ctx, &opts.p);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_createSearchOptions");
    if (topK > 0) ace_setSearchOptionInt(opts.p, ACESearchOption_MaxNumResults, topK);

    AceSearchResultsGuard results;
    if (mode == NGA_RAG_SEMANTIC) {
        if (!kb->semantic) { nga_set_last_error("the KB does not have a semantic index"); return NGA_ERR_NOT_FOUND; }
        r = ace_databaseSearch(kb->semantic, query, opts.p, &results.p);
        if (r != ACEResultOk) return nga_translate_result(r, "ace_databaseSearch(semantic)");
    }
    else if (mode == NGA_RAG_LEXICAL) {
        if (!kb->lexical) { nga_set_last_error("the KB does not have a lexical index"); return NGA_ERR_NOT_FOUND; }
        r = ace_databaseSearch(kb->lexical, query, opts.p, &results.p);
        if (r != ACEResultOk) return nga_translate_result(r, "ace_databaseSearch(lexical)");
    }
    else { // NGA_RAG_HYBRID
        if (!kb->semantic || !kb->lexical) { nga_set_last_error("hybrid requires semantic AND lexical indexes"); return NGA_ERR_NOT_FOUND; }
        ACEDatabase* dbs[] = { kb->semantic, kb->lexical };
        r = ace_hybridSearch(kb->rt->ctx, dbs, 2, query, opts.p, &results.p);
        if (r != ACEResultOk) return nga_translate_result(r, "ace_hybridSearch");
    }

    json arr = json::array();
    size_t n = 0;
    ace_searchResultsGetCount(results.p, &n);
    for (size_t i = 0; i < n; ++i) {
        ACESearchResult* hit = nullptr;
        if (ace_searchResultsGet(results.p, i, &hit) != ACEResultOk || !hit) continue;
        const char* id = nullptr; const char* doc = nullptr; float dist = 0.0f; uint32_t nt = 0;
        ace_searchResultGetId(hit, &id);
        ace_searchResultGetDocument(hit, &doc);
        ace_searchResultGetDistance(hit, &dist);
        ace_searchResultGetNumTokens(hit, &nt);
        json item;
        item["id"]        = id ? id : "";
        item["document"]  = doc ? doc : "";
        item["distance"]  = dist;
        item["numTokens"] = nt;
        arr.push_back(item);
    }
    return nga_copy_utf8(arr.dump(), outJson, len, written);
    NGA_TRY_END
}
