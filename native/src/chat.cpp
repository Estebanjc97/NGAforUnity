// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include "nga_helpers.hpp"
#include <nlohmann/include/json.hpp>
#include <thread>
#include <atomic>
using json = nlohmann::json;

// Stateless single-shot generation. Returns the assistant text in outBuf.
extern "C" NGA_API NgaResult NgaChat_Generate(NgaRuntimeHandle rt, const char* messagesJson,
    const char* paramsJson, char* outBuf, int32_t len, int32_t* written)
{
    NGA_TRY_BEGIN
    if (!rt || !rt->ctx || !rt->model) { nga_set_last_error("runtime null/not initialized"); return NGA_ERR_INVALID_ARG; }

    AceChatGuard chat;
    NgaResult nr = nga_build_chat(rt, messagesJson, chat);
    if (nr != NGA_OK) return nr;

    AceOptsGuard opts;
    nr = nga_make_options(rt, paramsJson, /*streaming*/ false, opts);
    if (nr != NGA_OK) return nr;

    AceMsgGuard out;
    ACEResult r = ace_Model_Chat(rt->model, chat.p, opts.p, &out.p);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_Model_Chat");

    const char* content = nullptr;
    ace_chatMessageGetContent(out.p, &content);
    return nga_copy_utf8(content ? content : "", outBuf, len, written);
    NGA_TRY_END
}

// Streaming generation: tokens are delivered via onToken on a background thread.
extern "C" NGA_API NgaResult NgaChat_GenerateStream(NgaRuntimeHandle rt, const char* messagesJson,
    const char* paramsJson, NgaTokenCallback onToken, void* userData)
{
    NGA_TRY_BEGIN
    if (!rt || !rt->ctx || !rt->model) { nga_set_last_error("runtime null/not initialized"); return NGA_ERR_INVALID_ARG; }
    if (!onToken) { nga_set_last_error("onToken callback is null"); return NGA_ERR_INVALID_ARG; }

    AceChatGuard chat;
    NgaResult nr = nga_build_chat(rt, messagesJson, chat);
    if (nr != NGA_OK) return nr;

    AceOptsGuard opts;
    nr = nga_make_options(rt, paramsJson, /*streaming*/ true, opts);
    if (nr != NGA_OK) return nr;

    // Reader thread drains token events from the SDK and forwards them to onToken.
    std::atomic<bool> stop{ false };
    ACEModel* model = rt->model;
    std::thread reader([model, onToken, userData, &stop]() {
        while (!stop.load()) {
            ACETokenEvent ev;
            ACEResult rr = ace_Model_GetNextEvent(model, &ev, 100);
            if (rr == ACEResultWaitTimeout) continue;
            if (rr != ACEResultOk) break;
            if (ev.eventType == ACEEventType_Data) onToken(ev.tokenData, userData);
            else if (ev.eventType == ACEEventType_End) break;
        }
    });

    AceMsgGuard out;
    ACEResult r = ace_Model_Chat(model, chat.p, opts.p, &out.p);

    stop.store(true);
    if (reader.joinable()) reader.join();

    if (r != ACEResultOk) return nga_translate_result(r, "ace_Model_Chat");
    return NGA_OK;
    NGA_TRY_END
}

// One stateless step with tool definitions. Returns JSON: {"content", "toolCalls":[...]}.
extern "C" NGA_API NgaResult NgaChat_GenerateWithTools(NgaRuntimeHandle rt, const char* messagesJson,
    const char* toolsJson, const char* paramsJson, char* outJson, int32_t len, int32_t* written)
{
    NGA_TRY_BEGIN
    if (!rt || !rt->ctx || !rt->model) { nga_set_last_error("runtime null/not initialized"); return NGA_ERR_INVALID_ARG; }

    AceChatGuard chat;
    NgaResult nr = nga_build_chat(rt, messagesJson, chat);
    if (nr != NGA_OK) return nr;

    if (toolsJson && toolsJson[0]) {
        json tools;
        try { tools = json::parse(toolsJson); }
        catch (const std::exception& e) { nga_set_last_error(std::string("tools JSON error: ") + e.what()); return NGA_ERR_INVALID_JSON; }
        if (tools.is_array()) {
            for (auto& t : tools) {
                ACETool* tool = nullptr;
                nr = nga_make_tool(rt->ctx, t, &tool);
                if (nr != NGA_OK) return nr;
                ace_chatAddTool(chat.p, tool); // ownership -> chat
            }
        }
    }

    AceOptsGuard opts;
    nr = nga_make_options(rt, paramsJson, /*streaming*/ false, opts);
    if (nr != NGA_OK) return nr;

    AceMsgGuard out;
    ACEResult r = ace_Model_Chat(rt->model, chat.p, opts.p, &out.p);
    if (r != ACEResultOk) return nga_translate_result(r, "ace_Model_Chat");

    json result;
    const char* content = nullptr;
    ace_chatMessageGetContent(out.p, &content);
    result["content"] = content ? content : "";

    ACEToolCalls* calls = nullptr;
    ace_chatMessageGetToolCalls(out.p, &calls);
    result["toolCalls"] = nga_tool_calls_to_json(calls);

    return nga_copy_utf8(result.dump(), outJson, len, written);
    NGA_TRY_END
}
