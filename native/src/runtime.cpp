// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include <memory>
#include <nlohmann/include/json.hpp>
using json = nlohmann::json;

extern "C" NGA_API uint32_t Nga_AbiVersion(void) 
{ 
    return NGA_ABI_VERSION; 
}

extern "C" NGA_API const char* Nga_LastError(void) 
{ 
    return nga_get_last_error(); 
}

extern "C" NGA_API NgaResult NgaRuntime_Create(const char* configJson, NgaRuntimeHandle* out) 
{
    NGA_TRY_BEGIN
        if (!out) { nga_set_last_error("out is null"); return NGA_ERR_INVALID_ARG; }
    *out = nullptr;
    if (!configJson || !configJson[0]) { nga_set_last_error("configJson null/empty"); return NGA_ERR_INVALID_ARG; }

    json cfg;
    try { cfg = json::parse(configJson); }
    catch (const std::exception& e) { nga_set_last_error(std::string("bad config JSON: ") + e.what()); return NGA_ERR_INVALID_JSON; }

    const std::string slmPath = cfg.value("slmPath", std::string());
    if (slmPath.empty()) { nga_set_last_error("'slmPath' is required"); return NGA_ERR_INVALID_ARG; }

    std::vector<std::string> embStore;
    if (cfg.contains("embeddingModelPaths") && cfg["embeddingModelPaths"].is_array())
        for (auto& p : cfg["embeddingModelPaths"]) if (p.is_string()) embStore.push_back(p.get<std::string>());
    std::vector<const char*> embPtrs;
    for (auto& s : embStore) embPtrs.push_back(s.c_str());

    const std::string cross = cfg.value("crossEncoderModelPath", std::string());
    const int maxCtx = cfg.value("maxContextSize", 0);
    const int outBuf = cfg.value("inferenceOutputBufferSize", 0);
    const int logLvl = cfg.value("logLevel", (int)ACELogLevel_Warn);

    if (ace_validateDriverVersion() != ACEResultOk)
        nga_set_last_error("warning: NVIDIA driver may be too old");

    ACEInitArgs args{};
    args.version = ACE_INIT_ARGS_VERSION;
    args.embeddingModelPaths = embPtrs.empty() ? nullptr : embPtrs.data();
    args.embeddingModelCount = (unsigned long)embPtrs.size();
    args.crossEncoderModelPath = cross.empty() ? nullptr : cross.c_str();
    args.level = (ACELogLevel)logLvl;

    auto rt = std::make_unique<NgaRuntime>();
    ACEResult r = ace_initContext(&rt->ctx, &args);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_initContext");

    ACEModelLoadParams* lp = nullptr;
    r = ace_createModelLoadParams(rt->ctx, &lp);
    if (r != ACEResultOk) { ace_destroyContext(rt->ctx); return nga_translate_result(r, "ace_createModelLoadParams"); }
    if (maxCtx > 0) ace_modelLoadParamsSetInt(lp, ACEModelLoadParam_MaxContextSize, maxCtx);
    if (outBuf > 0) ace_modelLoadParamsSetInt(lp, ACEModelLoadParam_InferenceOutputBufferSize, outBuf);
    r = ace_loadModel(rt->ctx, &rt->model, slmPath.c_str(), lp);
    ace_destroyModelLoadParams(lp);
    if (r != ACEResultOk) { ace_destroyContext(rt->ctx); return nga_translate_result(r, "ace_loadModel"); }

    json info; info["abiVersion"] = NGA_ABI_VERSION; info["slmPath"] = slmPath;
    info["embeddingModelPaths"] = embStore; info["maxContextSize"] = maxCtx;
    rt->infoJson = info.dump();

    *out = rt.release();
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API void NgaRuntime_Destroy(NgaRuntimeHandle rt) 
{
    if (!rt) return;
    try {
        if (rt->model) { ace_destroyModel(rt->model); rt->model = nullptr; }
        if (rt->ctx) { ace_destroyContext(rt->ctx); rt->ctx = nullptr; }
    }
    catch (...) {}
    delete rt;
}

extern "C" NGA_API NgaResult NgaRuntime_GetInfo(NgaRuntimeHandle rt, char* outJson, int32_t len, int32_t* written) 
{
    NGA_TRY_BEGIN
        if (!rt) { nga_set_last_error("runtime handle is null"); return NGA_ERR_INVALID_ARG; }
    return nga_copy_utf8(rt->infoJson, outJson, len, written);
    NGA_TRY_END
}