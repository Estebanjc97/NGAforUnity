# NGAforUnity — SDK Architecture

> **What this is.** Reference document for building a native wrapper (`.dll`) that exposes
> **the complete surface of the NVIDIA Game Agent SDK** (Agent + Chat + RAG) to Unity via
> P/Invoke. It fixes conventions and architecture; the **exact signatures** always come from
> the real SDK headers (`source/core/ace.h`). Product name: **NGAforUnity**
> (NVIDIA Game Agent for Unity). Public API prefix: **`Nga`**. DLL: **`NGAforUnity.dll`**.

---

## 0. Operating rules

1. **The source of truth is the SDK headers, not this doc.** If a signature here differs from
   `ace.h`, **the header wins**; update this doc.
2. **Do not invent SDK functions.** If it isn't in the headers, don't wrap it. Mark it `// TODO`.
3. **One binding = one C function of the SDK**, with no business logic at the boundary. Logic
   lives in C++ (layer 2) or in C# (layer 4), never in the C layer (layer 3).
4. **Every exported function has its C# counterpart** with `[DllImport]` and a test without Unity.

---

## 1. The base SDK (what we wrap)

NVIDIA **already exposes a clean C99 ABI** in `source/core/ace.h`, plus a RAII C++ wrapper in
`ace_cpp.h`. Inference runs **locally on the GPU** with an SLM. API families:

| Family | Nature | Key function (real, `ace.h`) |
|---|---|---|
| **Agent** | Stateful; drives the inference + tool-call loop on its own | `ace_agentCreate`, `ace_agentRun`, `ace_agentGetToolCalls`, `ace_agentAddToolResult` |
| **Chat** | Stateless; the app drives the loop | `ace_createChat`, `ace_Model_Chat`, `ace_Model_GetNextEvent` |
| **RAG** | Semantic/lexical/hybrid retrieval | `ace_loadDatabase`, `ace_databaseSearch`, `ace_hybridSearch` |

**SDK build artifacts** (after `build.bat`): `_out/ACE.SDK.dll` + `_out/ACE.SDK.lib` (import
lib), plus the runtime DLLs: `cudart64_12.dll`, `cublas*_12.dll`, `ggml*.dll`, `llama.dll`,
`onnxruntime.dll`, `libopenblas.dll`, `cig_scheduler_settings.dll`.

**Bundled models** (`data/models/`, ~3 GB, own EULA):
- SLM: `qwen3.5/Qwen3.5-4B-Q4_K_M.gguf`
- Embedders: `miniLM/all-MiniLM-L6-v2.F32.gguf`, `multilingual/multilingual-e5-base-Q8_0.gguf`
- Cross-encoder: `miniLM/cross_encoder_ms-marco-MiniLM-L6-v2.onnx`

### Environment requirements (inherited, hard)
Windows 10/11 · NVIDIA **Ampere or newer** GPU (~3.8 GB VRAM for the SLM) · driver **570.65+**
· **CUDA 12.8 SDK or later** · **Visual Studio 2019+** with "Desktop development with C++".

---

## 2. Wrapper architecture (4 layers)

```
┌───────────────────────────────────────────────────────────────┐
│ Layer 4 — C# / Unity    NGAInterop.cs, NgaRuntime.cs, ...      │  in the Unity project
│  [DllImport], SafeHandle, marshalling, async/await             │
├───────────────────────────────────────────────────────────────┤
│ Layer 3 — C frontier    NGAforUnity.h  (extern "C", exported)  │  ← THE ABI you freeze
│  opaque pointers · UTF-8 · POD structs · NgaResult             │
├───────────────────────────────────────────────────────────────┤
│ Layer 2 — C++ domain    translation, RAII, error handling      │  internal, free to change
│  calls ace.h / ace_cpp.h, catches exceptions                   │
├───────────────────────────────────────────────────────────────┤
│ Layer 1 — ACE SDK       C99 ABI + C++ wrapper + SLM/GPU        │  NVIDIA, untouched
└───────────────────────────────────────────────────────────────┘
```

**Only layer 3 crosses the binary boundary.** Layers 1→2 can be refactored freely; any change
to layer 3 requires bumping the ABI version (§7).

NPC logic (personalities, game loop) is built **on top**, in C# inside Unity. The DLL stays
generic and reusable.

---

## 3. ABI conventions (layer 3) — MANDATORY

### 3.1 Naming
- Global prefix: `Nga`. Format: `Nga<Family>_<Verb>` → `NgaRuntime_Create`, `NgaChat_Generate`,
  `NgaRag_Query`, `NgaAgent_SendMessage`. Utilities: `Nga_*` (`Nga_AbiVersion`, `Nga_LastError`).

### 3.2 Types that cross the boundary
| Concept | C representation | Never |
|---|---|---|
| Handle | Opaque pointer: `typedef struct NgaRuntime* NgaRuntimeHandle;` | Expose C++ structs |
| String (in) | `const char*` UTF-8 | `std::string`, `wchar_t*` |
| String (out) | Caller buffer (`char*, int32_t, int32_t*`) **or** DLL-owned + `_Free` | Return a temporary's `c_str()` |
| Struct | POD `#pragma pack(4)`, fixed fields | STL, vtables |
| Bool | `int32_t` (0/1) | C++ `bool` |
| Error | `NgaResult` (int enum) as return value | Exceptions |
| Callback | `__cdecl` with `void* userData` | Lambdas, `std::function` |

### 3.3 Unified result
```c
typedef enum {
    NGA_OK               = 0,
    NGA_ERR_INVALID_ARG  = 1,
    NGA_ERR_NO_GPU       = 2,   // driver too old / no compatible GPU
    NGA_ERR_MODEL_LOAD   = 3,
    NGA_ERR_INFERENCE    = 4,
    NGA_ERR_BUFFER_SMALL = 5,   // buffer too small; *written = required size (incl. NUL)
    NGA_ERR_NOT_READY    = 6,   // async operation in progress
    NGA_ERR_INVALID_JSON = 7,
    NGA_ERR_NOT_FOUND    = 8,   // handle/KB/tool does not exist
    NGA_ERR_INTERNAL     = 99
} NgaResult;
```
These codes are **mapped** from the SDK's real `ACEResult` values (`ace_result.h`: `ACEResultOk`,
`ACEResultInvalidParameter`, `ACEResultInvalidDriverVersion`, `ACEResultBufferTooSmall`, …).

### 3.4 Anti-crash frontier (absolute invariant)
**No C++ exception may cross the boundary.** Every exported function wraps its body:
```c
extern "C" NGA_API NgaResult NgaRuntime_Create(/* ... */) {
    try { /* ... layer 2 ... */ return NGA_OK; }
    catch (const std::exception& e) { nga_set_last_error(e.what()); return NGA_ERR_INTERNAL; }
    catch (...) { nga_set_last_error("unknown"); return NGA_ERR_INTERNAL; }
}
```
An escaping exception = a crash of the **Unity editor**, not just play mode.

### 3.5 Export macro
```c
#ifdef NGAFORUNITY_EXPORTS
  #define NGA_API __declspec(dllexport)
#else
  #define NGA_API __declspec(dllimport)
#endif
```
Note: when including `ace.h`, do **not** define `ACE_EXPORT` (so `ACE_API` = `dllimport` and it
links against `ACE.SDK.lib`).

---

## 4. Coverage map — what to expose per family

> Target wrapper signatures, over **real** functions from `ace.h`.

### 4.0 Runtime (process) — wraps `ace_initContext` + `ace_loadModel`
```c
NgaResult   NgaRuntime_Create(const char* configJson, NgaRuntimeHandle* out);
void        NgaRuntime_Destroy(NgaRuntimeHandle rt);
NgaResult   NgaRuntime_GetInfo(NgaRuntimeHandle rt, char* outJson, int32_t len, int32_t* written);
uint32_t    Nga_AbiVersion(void);
const char* Nga_LastError(void);
```
`configJson`: `slmPath` (required), `embeddingModelPaths[]`, `crossEncoderModelPath`,
`maxContextSize`, `inferenceOutputBufferSize`, `logLevel`.

### 4.1 Chat API — wraps `ace_createChat` / `ace_Model_Chat` / `ace_Model_GetNextEvent`
```c
NgaResult NgaChat_Generate(NgaRuntimeHandle rt, const char* messagesJson, const char* paramsJson,
                           char* outBuf, int32_t len, int32_t* written);
typedef void (__cdecl *NgaTokenCallback)(const char* tokenUtf8, void* userData);
NgaResult NgaChat_GenerateStream(NgaRuntimeHandle rt, const char* messagesJson, const char* paramsJson,
                                 NgaTokenCallback onToken, void* userData);
NgaResult NgaChat_GenerateWithTools(NgaRuntimeHandle rt, const char* messagesJson,
                                    const char* toolsJson, const char* paramsJson,
                                    char* outJson, int32_t len, int32_t* written);
```
Streaming uses the SDK's real pattern: `ace_Model_GetNextEvent` returns `ACETokenEvent`
(`Begin`/`Data`/`End`) on a dedicated thread.

### 4.2 RAG API — wraps `ace_loadDatabase` / `ace_databaseSearch` / `ace_hybridSearch`
```c
NgaResult NgaRag_OpenKB(NgaRuntimeHandle rt, const char* kbConfigJson, NgaKbHandle* out);
void      NgaRag_CloseKB(NgaKbHandle kb);
typedef enum { NGA_RAG_SEMANTIC=0, NGA_RAG_LEXICAL=1, NGA_RAG_HYBRID=2 } NgaRagMode;
NgaResult NgaRag_Query(NgaKbHandle kb, const char* query, NgaRagMode mode, int32_t topK,
                       char* outJson, int32_t len, int32_t* written);
```
`ACEIndexType_Semantic`/`_Lexical`; options via `ace_createSearchOptions` +
`ACESearchOption_MaxNumResults`.

### 4.3 Agent API — wraps `ace_agentCreate` + the `ace_agentRun` pattern
```c
NgaResult NgaAgent_Create(NgaRuntimeHandle rt, const char* agentConfigJson, NgaAgentHandle* out);
void      NgaAgent_Destroy(NgaAgentHandle a);
NgaResult NgaAgent_RegisterTool(NgaAgentHandle a, const char* toolSchemaJson, int32_t* outToolId);
NgaResult NgaAgent_SendMessage(NgaAgentHandle a, const char* content);  // ace_agentAddUserInput
// Loop pumping: ace_agentRun -> ACEAgentStatus (ResponseText | ToolCalls | Idle | ...)
typedef enum { NGA_STEP_DONE=0, NGA_STEP_TOOL_CALL=1, NGA_STEP_MESSAGE=2, NGA_STEP_RUNNING=3 } NgaStepKind;
NgaResult NgaAgent_Step(NgaAgentHandle a, NgaStepKind* kind, char* outJson, int32_t len, int32_t* written);
NgaResult NgaAgent_ProvideToolResult(NgaAgentHandle a, const char* toolCallId, const char* resultJson);
NgaResult NgaAgent_GetHistory(NgaAgentHandle a, char* outJson, int32_t len, int32_t* written);
NgaResult NgaAgent_SetSystemPrompt(NgaAgentHandle a, const char* prompt); // ace_agentSetInstructions
```
**Multi-agent:** several `NgaAgentHandle` over one `NgaRuntimeHandle` share the SLM but keep
their own identity and history (`ace_agentCreate` over the same `ACEModel`).

---

## 5. C++ ↔ C# marshalling — decision table

| Direction | ABI (C) | C# `[DllImport]` | Notes |
|---|---|---|---|
| Handle | opaque pointer | derived `SafeHandle` | `ReleaseHandle()` calls the `_Destroy` |
| String → native | `const char*` UTF-8 | `[MarshalAs(UnmanagedType.LPUTF8Str)] string` | .NET frees automatically |
| String ← native (short) | `char* buf, int len, int* written` | `byte[]` + `Encoding.UTF8` | On `BUFFER_SMALL`, retry with `*written` |
| Struct | `#pragma pack(4)` | `[StructLayout(Sequential, Pack=4)]` | The packs MUST match |
| Callback | `__cdecl` fn-ptr + `void*` | `[UnmanagedFunctionPointer(Cdecl)] delegate` | **Keep the delegate alive** (GCHandle) |
| Bool | `int32_t` 0/1 | `int` → `bool` in the friendly layer | Never `MarshalAs(Bool)` over a C++ `bool` |

**Golden rule (callbacks):** every delegate passed to native is stored in a field (or
`GCHandle.Alloc`) for the duration of the operation, or the GC collects it and the DLL calls
dead memory.

**Threading in Unity:** inference blocks; run it on a `Task` (thread pool) and marshal the
result back to the main thread (SynchronizationContext or a queue drained in `Update`). Streaming
callbacks arrive on a DLL thread: **only enqueue**; touch Unity APIs solely from the main thread.

---

## 6. Repository structure

```
NGAforUnity/
├── native/
│   ├── include/NGAforUnity.h        ← layer 3 (public ABI, frozen/SemVer)
│   ├── src/
│   │   ├── internal.hpp             ← try/catch macro, result mapping, opaque structs
│   │   ├── error.cpp                ← nga_set_last_error / Nga_LastError (thread-local)
│   │   ├── marshal.cpp              ← UTF-8 string/buffer copy
│   │   ├── runtime.cpp              ← NgaRuntime_* + Nga_AbiVersion
│   │   ├── chat.cpp                 ← NgaChat_*   (F2)
│   │   ├── rag.cpp                  ← NgaRag_*    (F3)
│   │   └── agent.cpp                ← NgaAgent_*  (F4)
│   ├── tests/                       ← native tests (no Unity)
│   └── NGAforUnity.vcxproj / CMakeLists.txt
├── unity/Runtime/                   ← layer 4: DllImport + SafeHandle + async wrappers
│   ├── NGAInterop.cs · NgaRuntime.cs · NgaChat.cs · NgaRag.cs · NgaAgent.cs
├── unity/Plugins/x86_64/            ← NGAforUnity.dll + ACE.SDK.dll + CUDA/ggml/llama/onnx DLLs
└── VERSION                          ← ABI version (SemVer)
```
Include dirs the SDK consumer needs: `<sdk>/source/core` and `<sdk>/external` (nlohmann/json).
Link against `<sdk>/_out/ACE.SDK.lib`.

---

## 7. Versioning and compatibility
- `Nga_AbiVersion()` returns `MAJOR<<16 | MINOR`. **MAJOR** bumps on layout/signature/semantic
  changes; **MINOR** when appending functions at the end. The C# layer validates MAJOR on load
  and aborts on mismatch.
- The `NGAforUnity.h` header is **frozen** when a version is closed. Internal changes (layers 1–2)
  are free.
- The base SDK is **beta v0.5.0**: its ABI may change; layer 2 absorbs it so layer 3 (your API to
  Unity) stays stable.

---

## 8. Phase plan (verifiable WITHOUT Unity until the last)

| Phase | Deliverable | Exit criterion |
|---|---|---|
| **F0** | NVIDIA baseline: SDK built + `Agent-Sample.exe` on GPU. | The sample infers locally. ✅ (done) |
| **F1** | DLL skeleton: `NgaRuntime_Create/Destroy`, `Nga_AbiVersion`, `Nga_LastError` + export macro + try/catch frontier. | The DLL loads the SLM and frees cleanly; native test passes. |
| **F2** | Full Chat API: `Generate`, `GenerateStream`, `GenerateWithTools`. | The CLI generates text and streams tokens. |
| **F3** | Full RAG API: `OpenKB/Query` in 3 modes. | Query returns a coherent top-k. |
| **F4** | Full Agent API: create, tools, `SendMessage`, `Step`/`ProvideToolResult`, history, multi-agent. | An agent completes ≥2 tool-calls; two agents coexist. |
| **F5** | Hardening: every `NgaResult` tested; JSON fuzzing; no exception crosses the ABI. | The DLL does not crash on malformed input. |
| **F6** | C#/Unity layer: `NGAInterop.cs` + `async` wrappers + smoke scene. Packaged with models. | Chat, RAG and an Agent with tools work in a Unity scene. |

Intentional order: **Chat → RAG → Agent** (least to most state). Agent conceptually depends on the other two.

---

## 9. Risks
- **Beta SDK:** signatures may change; the header wins, bump the version.
- **~3 GB models:** kept out of git (LFS or separate delivery); document `data/models/`.
- **Windows + Ampere+ only:** the fallback (non-RTX machines) is solved in C#/Unity, not the DLL.
- **Licenses:** SDK is Apache-2.0; **models carry their own EULA** → review for commercial use.
- **GPU contention (inference vs. render):** the SDK supports CiG (D3D12) for coexistence; the
  inference-budget policy is decided in Unity.
- **Do not put game logic in the DLL.** Personalities, schedules, perception = C# in Unity.

---

## 10. Per-binding checklist
- [ ] Signature confirmed against `ace.h` (not invented).
- [ ] Name follows `Nga<Family>_<Verb>`.
- [ ] Body in `try/catch` returning `NgaResult` (never throws to the frontier).
- [ ] Input strings UTF-8; output via caller-buffer or DLL-owned+Free (document which).
- [ ] Structs `#pragma pack(4)` with an identical C# counterpart `Pack=4`.
- [ ] Callbacks `__cdecl` with `void* userData`; C# keeps the delegate alive.
- [ ] `[DllImport]` in the `.cs`.
- [ ] Native test (happy path + at least one error).
- [ ] If layer 3 changes: bump the ABI version.

---

*Based on the NVIDIA Game Agent SDK v0.5.0 (beta, Apache-2.0). This document fixes conventions;
exact signatures are always validated against the downloaded SDK's headers.*
