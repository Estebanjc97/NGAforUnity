// SPDX-License-Identifier: Apache-2.0
// Shared, header-only helpers for the layer-2 sources (chat.cpp, rag.cpp, agent.cpp).
// Header-only (inline) so it needs no separate .cpp / no project change.
#ifndef NGA_HELPERS_HPP
#define NGA_HELPERS_HPP

#include "internal.hpp"
#include <nlohmann/include/json.hpp>

// -----------------------------------------------------------------------------
// RAII guards: each frees its SDK resource automatically when it leaves scope,
// including on early error returns. Replaces the repeated ace_destroy* calls.
// -----------------------------------------------------------------------------
struct AceChatGuard         { ACEChat*               p = nullptr; ~AceChatGuard()         { if (p) ace_destroyChat(p); } };
struct AceOptsGuard         { ACEInferenceOptions*   p = nullptr; ~AceOptsGuard()         { if (p) ace_destroyInferenceOptions(p); } };
struct AceMsgGuard          { ACEChatMessage*        p = nullptr; ~AceMsgGuard()          { if (p) ace_destroyChatMessage(p); } };
struct AceAgentParamsGuard  { ACEAgentParams*        p = nullptr; ~AceAgentParamsGuard()  { if (p) ace_destroyAgentParams(p); } };
struct AceSearchOptsGuard   { ACESearchOptions*      p = nullptr; ~AceSearchOptsGuard()   { if (p) ace_destroySearchOptions(p); } };
struct AceSearchResultsGuard{ ACESearchResults*      p = nullptr; ~AceSearchResultsGuard(){ if (p) ace_destroySearchResults(p); } };
struct AceDbParamsGuard     { ACEDatabaseLoadParams* p = nullptr; ~AceDbParamsGuard()     { if (p) ace_destroyDatabaseLoadParams(p); } };

// -----------------------------------------------------------------------------
// Role mapping between our string form and the SDK enum.
// -----------------------------------------------------------------------------
inline ACEChatRole nga_role_from_string(const std::string& r) {
    if (r == "system")    return ACEChatRole_System;
    if (r == "assistant") return ACEChatRole_Assistant;
    if (r == "tool")      return ACEChatRole_Tool;
    return ACEChatRole_User;
}

inline const char* nga_role_to_string(ACEChatRole r) {
    switch (r) {
        case ACEChatRole_System:    return "system";
        case ACEChatRole_Assistant: return "assistant";
        case ACEChatRole_Tool:      return "tool";
        default:                    return "user";
    }
}

// -----------------------------------------------------------------------------
// Build an ACEChat from a messages JSON array ([{"role","content"}, ...]).
// On success, chat.p owns the chat (freed by the guard). On any error, returns a
// mapped NgaResult and leaves the (empty or partial) chat for the guard to free.
// -----------------------------------------------------------------------------
inline NgaResult nga_build_chat(NgaRuntime* rt, const char* messagesJson, AceChatGuard& chat) {
    if (!messagesJson || !messagesJson[0]) { nga_set_last_error("messagesJson null/empty"); return NGA_ERR_INVALID_ARG; }

    nlohmann::json msgs;
    try { msgs = nlohmann::json::parse(messagesJson); }
    catch (const std::exception& e) { nga_set_last_error(std::string("messages JSON error: ") + e.what()); return NGA_ERR_INVALID_JSON; }
    if (!msgs.is_array() || msgs.empty()) { nga_set_last_error("messagesJson must be a non-empty array"); return NGA_ERR_INVALID_ARG; }

    ACEResult r = ace_createChat(rt->ctx, &chat.p);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_createChat");

    for (auto& m : msgs) {
        std::string role    = m.value("role", std::string("user"));
        std::string content = m.value("content", std::string());
        ACEChatMessage* entry = nullptr;
        r = ace_chatAdd(chat.p, &entry);
        if (r != ACEResultOk) return nga_translate_result(r, "ace_chatAdd");
        ace_chatMessageSetRole(entry, nga_role_from_string(role));
        ace_chatMessageSetContent(entry, content.c_str());
    }
    return NGA_OK;
}

// -----------------------------------------------------------------------------
// Create + configure inference options: sensible defaults, then override with any
// keys present in paramsJson (temperature, topP, topK, repeatPenalty, enableThink).
// -----------------------------------------------------------------------------
inline NgaResult nga_make_options(NgaRuntime* rt, const char* paramsJson, bool streaming, AceOptsGuard& opts) {
    ACEResult r = ace_createInferenceOptions(rt->ctx, &opts.p);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_createInferenceOptions");

    ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_EnableStreaming, streaming ? 1.0f : 0.0f);
    ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_Temperature, 0.2f);

    if (paramsJson && paramsJson[0]) {
        nlohmann::json p;
        try { p = nlohmann::json::parse(paramsJson); } catch (...) { p = nlohmann::json::object(); }
        if (p.contains("temperature"))   ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_Temperature,   p["temperature"].get<float>());
        if (p.contains("topP"))          ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_TopP,          p["topP"].get<float>());
        if (p.contains("topK"))          ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_TopK,          p["topK"].get<float>());
        if (p.contains("repeatPenalty")) ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_RepeatPenalty, p["repeatPenalty"].get<float>());
        if (p.contains("enableThink"))   ace_setInferenceOptionFloat(opts.p, ACEInferenceOption_EnableThink,   p["enableThink"].get<bool>() ? 1.0f : 0.0f);
    }
    return NGA_OK;
}

// -----------------------------------------------------------------------------
// Create an ACETool from a tool-definition JSON object ({name, description, parameters}).
// On success *out owns the tool; the caller passes it to ace_chatAddTool / ace_agentAddTool.
// -----------------------------------------------------------------------------
inline NgaResult nga_make_tool(ACEContext* ctx, const nlohmann::json& t, ACETool** out) {
    *out = nullptr;
    ACETool* tool = nullptr;
    ACEResult r = ace_createTool(ctx, &tool);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_createTool");

    std::string name   = t.value("name", std::string());
    std::string desc   = t.value("description", std::string());
    std::string params = t.contains("parameters") ? t["parameters"].dump() : std::string("{}");
    ace_toolSetName(tool, name.c_str());
    ace_toolSetDescription(tool, desc.c_str());
    ace_toolSetParameters(tool, params.c_str());

    *out = tool;
    return NGA_OK;
}

// -----------------------------------------------------------------------------
// Serialize an ACEToolCalls list to a JSON array [{"id","name","arguments"}, ...].
// Safe with a null list (returns an empty array).
// -----------------------------------------------------------------------------
inline nlohmann::json nga_tool_calls_to_json(const ACEToolCalls* calls) {
    nlohmann::json arr = nlohmann::json::array();
    if (!calls) return arr;

    size_t n = 0;
    ace_toolCallsGetCount(calls, &n);
    for (size_t i = 0; i < n; ++i) {
        ACEToolCall* call = nullptr;
        if (ace_toolCallsGetCall(calls, i, &call) != ACEResultOk || !call) continue;
        const char* id = nullptr; const char* nm = nullptr; const char* args = nullptr;
        ace_toolCallGetId(call, &id);
        ace_toolCallGetName(call, &nm);
        ace_toolCallGetArguments(call, &args);
        nlohmann::json tc;
        tc["id"]        = id   ? id   : "";
        tc["name"]      = nm   ? nm   : "";
        tc["arguments"] = args ? args : "";
        arr.push_back(tc);
    }
    return arr;
}

#endif // NGA_HELPERS_HPP
