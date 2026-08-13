// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include <memory>
#include <nlohmann/include/json.hpp>
using json = nlohmann::json;
using string = std::string;

extern "C" NGA_API uint32_t Nga_AbiVersion(void) 
{ 
    return NGA_ABI_VERSION; 
}

extern "C" NGA_API const char* Nga_LastError(void) 
{ 
    return nga_get_last_error(); 
}

extern "C" NGA_API NgaResult NgaRuntime_Create(const char* str_config_json, NgaRuntimeHandle* out) 
{
    NGA_TRY_BEGIN
    if (!out) 
    { 
        nga_set_last_error("out is null"); 
        return NGA_ERR_INVALID_ARG; 
    }
    
    *out = nullptr;
    
    if (!str_config_json || !str_config_json[0]) 
    { 
        nga_set_last_error("configJson null/empty"); 
        return NGA_ERR_INVALID_ARG; 
    }

    json config_json;

    try 
    { 
        config_json = json::parse(str_config_json); 
    }
    catch (const std::exception& e) 
    { 
        nga_set_last_error(string("bad config JSON: ") + e.what());
        return NGA_ERR_INVALID_JSON; 
    }

    const string slmPath = config_json.value("slmPath", string());

    if (slmPath.empty()) 
    { 
        nga_set_last_error("'slmPath' is required"); 
        return NGA_ERR_INVALID_ARG; 
    }

    std::vector<string> embStore;

    if (config_json.contains("embeddingModelPaths") && config_json["embeddingModelPaths"].is_array())
        for (auto& p : config_json["embeddingModelPaths"]) if (p.is_string()) embStore.push_back(p.get<string>());
    std::vector<const char*> embPtrs;
    for (auto& s : embStore) embPtrs.push_back(s.c_str());

    const string cross = config_json.value("crossEncoderModelPath", string());
    const int maxCtx = config_json.value("maxContextSize", 0);
    const int outBuf = config_json.value("inferenceOutputBufferSize", 0);
    const int logLvl = config_json.value("logLevel", (int)ACELogLevel_Warn);

    if (ace_validateDriverVersion() != ACEResultOk)
        nga_set_last_error("warning: NVIDIA driver may be too old");

    ACEInitArgs args{};
    args.version = ACE_INIT_ARGS_VERSION;
    args.embeddingModelPaths = embPtrs.empty() ? nullptr : embPtrs.data();
    args.embeddingModelCount = (unsigned long)embPtrs.size();
    args.crossEncoderModelPath = cross.empty() ? nullptr : cross.c_str();
    args.level = (ACELogLevel)logLvl;

    auto nga_runtime = std::make_unique<NgaRuntime>();
    ACEResult ace_result = ace_initContext(&nga_runtime->ctx, &args);
    if (ace_result != ACEResultOk) return nga_translate_result(ace_result, "ace_initContext");

    ACEModelLoadParams* model_load_params = nullptr;

    ace_result = ace_createModelLoadParams(nga_runtime->ctx, &model_load_params);

    if (ace_result != ACEResultOk) 
    { 
        ace_destroyContext(nga_runtime->ctx); 
        return nga_translate_result(ace_result, "ace_createModelLoadParams"); 
    }

    if (maxCtx > 0) ace_modelLoadParamsSetInt(model_load_params, ACEModelLoadParam_MaxContextSize, maxCtx);

    if (outBuf > 0) ace_modelLoadParamsSetInt(model_load_params, ACEModelLoadParam_InferenceOutputBufferSize, outBuf);

    ace_result = ace_loadModel(nga_runtime->ctx, &nga_runtime->model, slmPath.c_str(), model_load_params);

    ace_destroyModelLoadParams(model_load_params);

    if (ace_result != ACEResultOk) 
    { 
        ace_destroyContext(nga_runtime->ctx); 
        return nga_translate_result(ace_result, "ace_loadModel"); 
    }

    json info; 
    
    info["abiVersion"] = NGA_ABI_VERSION; 
    info["slmPath"] = slmPath;
    info["embeddingModelPaths"] = embStore; 
    info["maxContextSize"] = maxCtx;
    nga_runtime->infoJson = info.dump();

    *out = nga_runtime.release();

    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API void NgaRuntime_Destroy(NgaRuntimeHandle nga_runtime_handle) 
{
    if (!nga_runtime_handle) return;
    try 
    {
        if (nga_runtime_handle->model) 
        { 
            ace_destroyModel(nga_runtime_handle->model); 
            nga_runtime_handle->model = nullptr; 
        }
        if (nga_runtime_handle->ctx) 
        { 
            ace_destroyContext(nga_runtime_handle->ctx); 
            nga_runtime_handle->ctx = nullptr; 
        }
    }
    catch (...) {}
    delete nga_runtime_handle;
}

extern "C" NGA_API NgaResult NgaRuntime_GetInfo(NgaRuntimeHandle nga_runtime_handle, char* outJson, int32_t len, int32_t* written) 
{
    NGA_TRY_BEGIN
        if (!nga_runtime_handle) 
        { 
            nga_set_last_error("runtime handle is null"); 
            return NGA_ERR_INVALID_ARG; 
        }
        return nga_copy_utf8(nga_runtime_handle->infoJson, outJson, len, written);
    NGA_TRY_END
}