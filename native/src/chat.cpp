// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include <nlohmann/include/json.hpp>
#include <thread>
#include <atomic>
using json = nlohmann::json;
using string = std::string;

static ACEChatRole roleFromString(const string& r) {
    if (r == "system")    return ACEChatRole_System;
    if (r == "assistant") return ACEChatRole_Assistant;
    if (r == "tool")      return ACEChatRole_Tool;
    return ACEChatRole_User;
}

extern "C" NGA_API NgaResult NgaChat_Generate(NgaRuntimeHandle runtime_handle, const char* messagesJson, 
    const char* paramsJson, char* outBuf, int32_t len, int32_t* written) 
{
    NGA_TRY_BEGIN
        
        if (!runtime_handle || !runtime_handle->ctx || !runtime_handle->model) 
        { 
            nga_set_last_error("runtime null/not initialized"); 
            return NGA_ERR_INVALID_ARG; 
        }

        if (!messagesJson || !messagesJson[0]) 
        { 
            nga_set_last_error("messagesJson null/empty"); 
            return NGA_ERR_INVALID_ARG; 
        }

        json messages;

        try 
        { 
            messages = json::parse(messagesJson); 
        }
        catch (const std::exception& e) 
        { 
            nga_set_last_error(string("messages JSON error: ") + e.what()); 
            return NGA_ERR_INVALID_JSON; 
        }
        if (!messages.is_array() || messages.empty()) 
        { 
            nga_set_last_error("messagesJson must be a not empty array"); 
            return NGA_ERR_INVALID_ARG; 
        }

        ACEChat* chat = nullptr;
        ACEResult ace_result = ace_createChat(runtime_handle->ctx, &chat);

        if (ace_result != ACEResultOk) return nga_translate_result(ace_result, "ace_createChat");

        for (auto& message : messages) {
            string role = message.value("role", string("user"));
            string content = message.value("content", string());
            ACEChatMessage* entry = nullptr;
            ace_result = ace_chatAdd(chat, &entry);
            if (ace_result != ACEResultOk) 
            { 
                ace_destroyChat(chat);
                return nga_translate_result(ace_result, "ace_chatAdd");
            }
            ace_chatMessageSetRole(entry, roleFromString(role));
            ace_chatMessageSetContent(entry, content.c_str());
        }

        ACEInferenceOptions* opts = nullptr;
        ace_result = ace_createInferenceOptions(runtime_handle->ctx, &opts);
        if (ace_result != ACEResultOk) 
        { 
            ace_destroyChat(chat); 
            return nga_translate_result(ace_result, "ace_createInferenceOptions"); 
        }

        ace_setInferenceOptionFloat(opts, ACEInferenceOption_EnableStreaming, 0.0f);
        ace_setInferenceOptionFloat(opts, ACEInferenceOption_Temperature, 0.2f);

        if (paramsJson && paramsJson[0]) {
            json p;
            try { p = json::parse(paramsJson); }
            catch (...) { p = json::object(); }
            
            if (p.contains("temperature"))   ace_setInferenceOptionFloat(opts, ACEInferenceOption_Temperature, p["temperature"].get<float>());
            if (p.contains("topP"))          ace_setInferenceOptionFloat(opts, ACEInferenceOption_TopP, p["topP"].get<float>());
            if (p.contains("topK"))          ace_setInferenceOptionFloat(opts, ACEInferenceOption_TopK, p["topK"].get<float>());
            if (p.contains("repeatPenalty")) ace_setInferenceOptionFloat(opts, ACEInferenceOption_RepeatPenalty, p["repeatPenalty"].get<float>());
            if (p.contains("enableThink"))   ace_setInferenceOptionFloat(opts, ACEInferenceOption_EnableThink, p["enableThink"].get<bool>() ? 1.0f : 0.0f);
        }

        ACEChatMessage* outEntry = nullptr;
        ace_result = ace_Model_Chat(runtime_handle->model, chat, opts, &outEntry);
        if (ace_result != ACEResultOk) {
            ace_destroyInferenceOptions(opts);
            ace_destroyChat(chat);
            return nga_translate_result(ace_result, "ace_Model_Chat");
        }

        const char* content = nullptr;
        ace_chatMessageGetContent(outEntry, &content);
        string text = content ? content : "";
        NgaResult copyResult = nga_copy_utf8(text, outBuf, len, written);

        ace_destroyChatMessage(outEntry);
        ace_destroyInferenceOptions(opts);
        ace_destroyChat(chat);

        return copyResult;
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaChat_GenerateStream(NgaRuntimeHandle runtime_handle, const char* messagesJson,
    const char* paramsJson, NgaTokenCallback onToken, void* userData) 
{
    NGA_TRY_BEGIN
        if (!runtime_handle || !runtime_handle->ctx || !runtime_handle->model) 
        { 
            nga_set_last_error("runtime null/not initialized"); 
            return NGA_ERR_INVALID_ARG; 
        }

        if (!messagesJson || !messagesJson[0]) 
        { 
            nga_set_last_error("messagesJson null/empty"); 
            return NGA_ERR_INVALID_ARG; 
        }

        if (!onToken) 
        { 
            nga_set_last_error("onToken callback is null"); 
            return NGA_ERR_INVALID_ARG; 
        }

        json msgs;

        try 
        { 
            msgs = json::parse(messagesJson); 
        }
        catch (const std::exception& e) 
        {
            nga_set_last_error(std::string("messages JSON error: ") + e.what());
            return NGA_ERR_INVALID_JSON; 
        }

        if (!msgs.is_array() || msgs.empty()) 
        { 
            nga_set_last_error("messagesJson must be a not empty array");
            return NGA_ERR_INVALID_ARG;
        }

        ACEChat* chat = nullptr;
        ACEResult ace_result = ace_createChat(runtime_handle->ctx, &chat);

        if (ace_result != ACEResultOk) 
            return nga_translate_result(ace_result, "ace_createChat");

        for (auto& m : msgs) {
            std::string role = m.value("role", std::string("user"));
            std::string content = m.value("content", std::string());
            ACEChatMessage* entry = nullptr;
            ace_result = ace_chatAdd(chat, &entry);
            if (ace_result != ACEResultOk) { ace_destroyChat(chat); return nga_translate_result(ace_result, "ace_chatAdd"); }
            ace_chatMessageSetRole(entry, roleFromString(role));
            ace_chatMessageSetContent(entry, content.c_str());
        }

        ACEInferenceOptions* opts = nullptr;
        ace_result = ace_createInferenceOptions(runtime_handle->ctx, &opts);
        if (ace_result != ACEResultOk) { ace_destroyChat(chat); return nga_translate_result(ace_result, "ace_createInferenceOptions"); }
        ace_setInferenceOptionFloat(opts, ACEInferenceOption_EnableStreaming, 1.0f); // streaming ON
        ace_setInferenceOptionFloat(opts, ACEInferenceOption_Temperature, 0.2f);
        if (paramsJson && paramsJson[0]) {
            json p; try { p = json::parse(paramsJson); }
            catch (...) { p = json::object(); }
            if (p.contains("temperature"))   ace_setInferenceOptionFloat(opts, ACEInferenceOption_Temperature, p["temperature"].get<float>());
            if (p.contains("repeatPenalty")) ace_setInferenceOptionFloat(opts, ACEInferenceOption_RepeatPenalty, p["repeatPenalty"].get<float>());
            if (p.contains("enableThink"))   ace_setInferenceOptionFloat(opts, ACEInferenceOption_EnableThink, p["enableThink"].get<bool>() ? 1.0f : 0.0f);
        }

        std::atomic<bool> stop{ false };
        ACEModel* model = runtime_handle->model;
        std::thread reader([model, onToken, userData, &stop]() 
        {
            while (!stop.load()) {
                ACETokenEvent ev;
                ACEResult rr = ace_Model_GetNextEvent(model, &ev, 100);
                if (rr == ACEResultWaitTimeout) continue;
                if (rr != ACEResultOk) break;
                if (ev.eventType == ACEEventType_Data) onToken(ev.tokenData, userData);
                else if (ev.eventType == ACEEventType_End) break;
            }
        });

        ACEChatMessage* outEntry = nullptr;
        ace_result = ace_Model_Chat(model, chat, opts, &outEntry);

        stop.store(true);
        if (reader.joinable()) reader.join();

        if (outEntry) ace_destroyChatMessage(outEntry);
        ace_destroyInferenceOptions(opts);
        ace_destroyChat(chat);

        if (ace_result != ACEResultOk) return nga_translate_result(ace_result, "ace_Model_Chat");
        return NGA_OK;
    NGA_TRY_END
}

extern "C" NGA_API NgaResult NgaChat_GenerateWithTools(NgaRuntimeHandle rt, const char* messagesJson,
    const char* toolsJson, const char* paramsJson,
    char* outJson, int32_t len, int32_t* written) {
    
    NGA_TRY_BEGIN
        if (!rt || !rt->ctx || !rt->model) 
        { 
            nga_set_last_error("runtime null/not initialized"); 
            return NGA_ERR_INVALID_ARG;
        }
        
        if (!messagesJson || !messagesJson[0]) 
        {
            nga_set_last_error("messagesJson null/empty");
            return NGA_ERR_INVALID_ARG;
        }

        json msgs;

        try 
        { 
            msgs = json::parse(messagesJson);
        }
        catch (const std::exception& e) 
        { 
            nga_set_last_error(std::string("messages JSON error: ") + e.what());
            return NGA_ERR_INVALID_JSON;
        }

        if (!msgs.is_array() || msgs.empty()) 
        { 
            nga_set_last_error("messagesJson must be a not empty array");
            return NGA_ERR_INVALID_ARG;
        }

        ACEChat* chat = nullptr;
        ACEResult r = ace_createChat(rt->ctx, &chat);
        if (r != ACEResultOk) return nga_translate_result(r, "ace_createChat");
        for (auto& m : msgs) {
            std::string role = m.value("role", std::string("user"));
            std::string content = m.value("content", std::string());
            ACEChatMessage* entry = nullptr;
            r = ace_chatAdd(chat, &entry);
            if (r != ACEResultOk) { ace_destroyChat(chat); return nga_translate_result(r, "ace_chatAdd"); }
            ace_chatMessageSetRole(entry, roleFromString(role));
            ace_chatMessageSetContent(entry, content.c_str());
        }

        if (toolsJson && toolsJson[0]) {
            json tools;
            try { tools = json::parse(toolsJson); }
            catch (const std::exception& e) { ace_destroyChat(chat); nga_set_last_error(std::string("tools JSON malo: ") + e.what()); return NGA_ERR_INVALID_JSON; }
            if (tools.is_array()) {
                for (auto& t : tools) {
                    ACETool* tool = nullptr;
                    r = ace_createTool(rt->ctx, &tool);
                    if (r != ACEResultOk) { ace_destroyChat(chat); return nga_translate_result(r, "ace_createTool"); }
                    std::string name = t.value("name", std::string());
                    std::string desc = t.value("description", std::string());
                    std::string params = t.contains("parameters") ? t["parameters"].dump() : std::string("{}");
                    ace_toolSetName(tool, name.c_str());
                    ace_toolSetDescription(tool, desc.c_str());
                    ace_toolSetParameters(tool, params.c_str());
                    ace_chatAddTool(chat, tool);
                }
            }
        }

        ACEInferenceOptions* opts = nullptr;
        r = ace_createInferenceOptions(rt->ctx, &opts);
        if (r != ACEResultOk) { ace_destroyChat(chat); return nga_translate_result(r, "ace_createInferenceOptions"); }
        ace_setInferenceOptionFloat(opts, ACEInferenceOption_EnableStreaming, 0.0f);
        ace_setInferenceOptionFloat(opts, ACEInferenceOption_Temperature, 0.2f);
        if (paramsJson && paramsJson[0]) {
            json p; try { p = json::parse(paramsJson); }
            catch (...) { p = json::object(); }
            if (p.contains("temperature")) ace_setInferenceOptionFloat(opts, ACEInferenceOption_Temperature, p["temperature"].get<float>());
            if (p.contains("enableThink")) ace_setInferenceOptionFloat(opts, ACEInferenceOption_EnableThink, p["enableThink"].get<bool>() ? 1.0f : 0.0f);
        }

        ACEChatMessage* outEntry = nullptr;
        r = ace_Model_Chat(rt->model, chat, opts, &outEntry);
        if (r != ACEResultOk) { ace_destroyInferenceOptions(opts); ace_destroyChat(chat); return nga_translate_result(r, "ace_Model_Chat"); }

        json result;
        const char* content = nullptr;
        ace_chatMessageGetContent(outEntry, &content);
        result["content"] = content ? content : "";
        result["toolCalls"] = json::array();

        ACEToolCalls* calls = nullptr;
        if (ace_chatMessageGetToolCalls(outEntry, &calls) == ACEResultOk && calls) {
            size_t n = 0;
            ace_toolCallsGetCount(calls, &n);
            for (size_t i = 0; i < n; ++i) {
                ACEToolCall* call = nullptr;
                if (ace_toolCallsGetCall(calls, i, &call) != ACEResultOk || !call) continue;
                const char* id = nullptr; const char* nm = nullptr; const char* args = nullptr;
                ace_toolCallGetId(call, &id);
                ace_toolCallGetName(call, &nm);
                ace_toolCallGetArguments(call, &args);
                json tc;
                tc["id"] = id ? id : "";
                tc["name"] = nm ? nm : "";
                tc["arguments"] = args ? args : "";
                result["toolCalls"].push_back(tc);
            }
        }

        std::string outStr = result.dump();
        NgaResult copyResult = nga_copy_utf8(outStr, outJson, len, written);

        ace_destroyChatMessage(outEntry);
        ace_destroyInferenceOptions(opts);
        ace_destroyChat(chat);
        return copyResult;
    NGA_TRY_END
}
