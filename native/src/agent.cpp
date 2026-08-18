// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include "nga_helpers.hpp"
#include <memory>
#include <nlohmann/include/json.hpp>
using json = nlohmann::json;

extern "C" NGA_API NgaResult NgaAgent_Create(NgaRuntimeHandle rt, const char* agentConfigJson, NgaAgentHandle* out) {
    NGA_TRY_BEGIN
    if (!rt || !rt->ctx || !rt->model) { nga_set_last_error("runtime null/not initialized"); return NGA_ERR_INVALID_ARG; }
    if (!out) { nga_set_last_error("out is null"); return NGA_ERR_INVALID_ARG; }
    *out = nullptr;

    json cfg = json::object();
    if (agentConfigJson && agentConfigJson[0]) {
        try { cfg = json::parse(agentConfigJson); }
        catch (const std::exception& e) { nga_set_last_error(std::string("agent JSON error: ") + e.what()); return NGA_ERR_INVALID_JSON; }
    }
    std::string id           = cfg.value("id", std::string("agent"));
    std::string instructions = cfg.value("instructions", std::string());
    int   maxSteps      = cfg.value("maxSteps", 10);
    int   historyWindow = cfg.value("historyWindow", 50);
    float temperature   = cfg.value("temperature", 0.2f);
    bool  enableThink   = cfg.value("enableThink", false);

    AceAgentParamsGuard params;
    ACEResult r = ace_createAgentParams(rt->ctx, &params.p);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_createAgentParams");
    ace_agentParamsSetString(params.p, ACEAgentParam_Id, id.c_str());
    if (!instructions.empty()) ace_agentParamsSetString(params.p, ACEAgentParam_Instructions, instructions.c_str());
    ace_agentParamsSetInt(params.p, ACEAgentParam_MaxSteps, maxSteps);
    ace_agentParamsSetInt(params.p, ACEAgentParam_HistoryWindow, historyWindow);

    AceOptsGuard opts;
    r = ace_createInferenceOptions(rt->ctx, &opts.p);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_createInferenceOptions");
    ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_EnableStreaming, 0.0f);
    ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_Temperature, temperature);
    ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_EnableThink, enableThink ? 1.0f : 0.0f);

    auto a = std::make_unique<NgaAgent>();
    a->rt = rt;
    r = ace_agentCreate(rt->model, params.p, opts.p, &a->agent);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_agentCreate");

    *out = a.release();
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API void NgaAgent_Destroy(NgaAgentHandle a) {
    if (!a) return;
    try { if (a->agent) ace_agentDestroy(a->agent); }
    catch (...) {}
    delete a;
}

extern "C" NGA_API NgaResult NgaAgent_AddTool(NgaAgentHandle a, const char* toolSchemaJson) {
    NGA_TRY_BEGIN
    if (!a || !a->agent || !a->rt) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }
    if (!toolSchemaJson || !toolSchemaJson[0]) { nga_set_last_error("toolSchemaJson null/empty"); return NGA_ERR_INVALID_ARG; }

    json t;
    try { t = json::parse(toolSchemaJson); }
    catch (const std::exception& e) { nga_set_last_error(std::string("tool JSON error: ") + e.what()); return NGA_ERR_INVALID_JSON; }

    ACETool* tool = nullptr;
    NgaResult nr = nga_make_tool(a->rt->ctx, t, &tool);
    if (nr != NGA_OK) return nr;

    ACEResult r = ace_agentAddTool(a->agent, tool);
    if (r != ACEResultOk) { ace_destroyTool(tool); return nga_translate_result(r, "ace_agentAddTool"); }
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaAgent_SetSystemPrompt(NgaAgentHandle a, const char* prompt) {
    NGA_TRY_BEGIN
    if (!a || !a->agent) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }
    ACEResult r = ace_agentSetInstructions(a->agent, prompt);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_agentSetInstructions");
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaAgent_SendMessage(NgaAgentHandle a, const char* content) {
    NGA_TRY_BEGIN
    if (!a || !a->agent) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }
    ACEResult r = ace_agentAddUserInput(a->agent, content ? content : "");
    if (r != ACEResultOk) return nga_translate_result(r, "ace_agentAddUserInput");
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaAgent_ProvideToolResult(NgaAgentHandle a, const char* toolCallId, const char* resultJson) {
    NGA_TRY_BEGIN
    if (!a || !a->agent) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }
    if (!toolCallId || !toolCallId[0]) { nga_set_last_error("toolCallId null/empty"); return NGA_ERR_INVALID_ARG; }
    ACEResult r = ace_agentAddToolResult(a->agent, toolCallId, resultJson ? resultJson : "", 0);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_agentAddToolResult");
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaAgent_Step(NgaAgentHandle a, NgaStepKind* kind, char* outJson, int32_t len, int32_t* written) {
    NGA_TRY_BEGIN
    if (!a || !a->agent) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }
    if (!kind) { nga_set_last_error("kind is null"); return NGA_ERR_INVALID_ARG; }
    *kind = NGA_STEP_DONE;

    ACEAgentStatus status;
    ACEResult r = ace_agentRun(a->agent, &status);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_agentRun");

    std::string outStr = "{}";
    switch (status) {
        case ACEAgentStatus_ResponseText: {
            *kind = NGA_STEP_MESSAGE;
            const char* text = nullptr;
            ace_agentGetResponseText(a->agent, &text);
            json o; o["content"] = text ? text : "";
            outStr = o.dump();
            break;
        }
        case ACEAgentStatus_ToolCalls: {
            *kind = NGA_STEP_TOOL_CALL;
            const ACEToolCalls* calls = nullptr;
            ace_agentGetToolCalls(a->agent, &calls);
            json o; o["toolCalls"] = nga_tool_calls_to_json(calls);
            outStr = o.dump();
            break;
        }
        case ACEAgentStatus_Error: {
            const char* err = nullptr;
            ace_agentGetError(a->agent, &err);
            nga_set_last_error(err ? err : "agent error");
            return NGA_ERR_INFERENCE;
        }
        default: // Idle / Shutdown / Cancelled
            *kind = NGA_STEP_DONE;
            break;
    }
    return nga_copy_utf8(outStr, outJson, len, written);
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaAgent_GetHistory(NgaAgentHandle a, char* outJson, int32_t len, int32_t* written) {
    NGA_TRY_BEGIN
    if (!a || !a->agent) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }

    size_t n = 0;
    ace_agentGetConversationCount(a->agent, &n);
    json arr = json::array();
    for (size_t i = 0; i < n; ++i) {
        const ACEChatMessage* msg = nullptr;
        if (ace_agentGetConversation(a->agent, i, &msg) != ACEResultOk || !msg) continue;
        ACEChatRole role = ACEChatRole_User;
        ace_chatMessageGetRole(msg, &role);
        const char* content = nullptr;
        ace_chatMessageGetContent(msg, &content);
        json m; m["role"] = nga_role_to_string(role); m["content"] = content ? content : "";
        arr.push_back(m);
    }
    return nga_copy_utf8(arr.dump(), outJson, len, written);
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaAgent_Cancel(NgaAgentHandle a) {
    NGA_TRY_BEGIN
    if (!a || !a->agent) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }
    ACEResult r = ace_agentCancel(a->agent, 0);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_agentCancel");
    return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaAgent_ClearCancel(NgaAgentHandle a) {
    NGA_TRY_BEGIN
    if (!a || !a->agent) { nga_set_last_error("agent null"); return NGA_ERR_INVALID_ARG; }
    ACEResult r = ace_agentClearCancel(a->agent);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_agentClearCancel");
    return NGA_OK;
    NGA_TRY_END
}
