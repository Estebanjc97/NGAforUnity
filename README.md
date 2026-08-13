# NGAforUnity

**NGAforUnity** (NVIDIA Game Agent for Unity) is an open-source native wrapper that brings the
**[NVIDIA Game Agent SDK](https://github.com/NVIDIA/game-agent-sdk)** to the **Unity** engine.
It exposes the SDK's full surface — **Agent**, **Chat** and **RAG** APIs — to C# through a thin,
stable C ABI (`NGAforUnity.dll`) consumed via P/Invoke, so you can build on-device AI companions
that run **locally on the player's NVIDIA GPU** with a Small Language Model (SLM).

> Status: functional reference implementation (Runtime + Chat + RAG + Agent), validated end-to-end
> in the Unity Editor. Licensed under **Apache-2.0**.

---

## Background & motivation

At CES 2025 and after, NVIDIA introduced a new class of in-game AI characters under the **NVIDIA ACE**
umbrella — autonomous companions that perceive game state, reason with an on-device SLM, call tools,
and talk to the player. NVIDIA shipped the building blocks publicly:

- The **NVIDIA Game Agent SDK** — a C99/C++ agentic framework with Agent, Chat and RAG APIs that runs
  an SLM on the user's GPU. See the repo: <https://github.com/NVIDIA/game-agent-sdk>
- **Unreal Engine 5 plugins** for it: [*Build On-Device AI Companions with the NVIDIA ACE Game Agent
  SDK and Unreal Engine 5 Plugins*](https://developer.nvidia.com/blog/build-on-device-ai-companions-with-the-nvidia-ace-game-agent-sdk-and-unreal-engine-5-plugins/)
- An open-source SLM tuned for the task: [*NVIDIA ACE Adds Open Source Qwen3 SLM for On-Device
  Deployment in PC Games*](https://developer.nvidia.com/blog/nvidia-ace-adds-open-source-qwen3-slm-for-on-device-deployment-in-pc-games/)

The flagship real-world example is **KRAFTON's PUBG Ally**, a "Co-Playable Character" (CPC) built with
NVIDIA ACE that cooperates with players in *PUBG: BATTLEGROUNDS* — see NVIDIA's write-up:
[*How KRAFTON Built PUBG Ally, a Co-Playable Character Powered by NVIDIA
ACE*](https://developer.nvidia.com/blog/how-krafton-built-pubg-ally-a-co-playable-character-powered-by-nvidia-ace/)
and NVIDIA's overview: [*NVIDIA Redefines Game AI With ACE Autonomous Game
Characters*](https://www.nvidia.com/en-us/geforce/news/nvidia-ace-autonomous-ai-companions-pubg-naraka-bladepoint/).

**The gap this project fills:** NVIDIA provided the SDK and *Unreal Engine* plugins, but **no Unity
integration**. NGAforUnity is that missing layer — the same on-device "brain" (decision-making,
tool-calling, knowledge retrieval), made callable from Unity/C#. Higher-level NPC behaviour
(personality, perception, animation, and voice via ASR/TTS) is meant to be built *on top* in Unity,
using these bindings as primitives.

> This is an independent, community project. It is **not affiliated with or endorsed by NVIDIA or
> KRAFTON**. "NVIDIA", "ACE", "PUBG" and related marks belong to their respective owners.

---

## How it works (architecture)

Four layers; only layer 3 crosses the binary boundary:

```
Layer 4  C# / Unity     NGAInterop.cs, NGARuntime.cs, NGAAgent.cs ...   (this repo, Unity side)
Layer 3  C frontier     NGAforUnity.h  (extern "C", frozen ABI)          (native/include)
Layer 2  C++ domain     wraps the SDK, RAII, try/catch, error mapping    (native/src)
Layer 1  ACE SDK        C99 ABI + SLM/GPU                                 (NVIDIA, unmodified)
```

Design conventions, the ABI contract, and the full phase plan live in
[`docs/NGAforUnity_Architecture.md`](docs/NGAforUnity_Architecture.md).

---

## Repository layout

```
NGAforUnity/
├── native/                         # The C++ wrapper -> NGAforUnity.dll
│   ├── include/NGAforUnity.h        #   Layer 3: the public C ABI
│   ├── src/                         #   Layer 2: runtime, chat, rag, agent, helpers, error, marshal
│   ├── NGA_Test/                    #   Native smoke + hardening test (no Unity needed)
│   └── external/game-agent-sdk/     #   The NVIDIA SDK (NOT committed — you provide it)
├── NGA_Sample/                     # A ready-to-run Unity sample project
│   └── Assets/
│       ├── NGAforUnity/Runtime/     #   The reusable C# layer (copy this into your project)
│       ├── NGAforUnity/NGADemo.cs   #   Example MonoBehaviour
│       └── Plugins/x86_64/          #   Native DLLs go here (NOT committed — you build/copy them)
├── docs/NGAforUnity_Architecture.md
├── LICENSE                          # Apache-2.0
└── README.md
```

Note: compiled binaries (`*.dll`, `*.lib`), the NVIDIA SDK (`external/`) and the models
(`data/models/`) are **git-ignored** — they are large and/or under NVIDIA's own license. You obtain
and build them locally (below).

---

## Requirements

- Windows 10 or 11 (x64)
- NVIDIA **Ampere GPU or newer** with ~4 GB+ free VRAM (the bundled SLM uses ~3.8 GB; runs on 8 GB cards)
- NVIDIA driver **570.65+**
- **CUDA Toolkit 12.8 or later**
- **Visual Studio 2019+** with the *Desktop development with C++* workload
- **Unity 2021 LTS or newer** (for the C# side)

---

## 1. Get the NVIDIA Game Agent SDK + models

The SDK and its ~3 GB model bundle are not redistributed here.

1. Download the source zip (includes the models) from the NVIDIA Developer Portal:
   <https://developer.nvidia.com/downloads/ace/game-agent-sdk-v0.5.0-source.zip>
   (or clone <https://github.com/NVIDIA/game-agent-sdk> and copy `data/models/` from the zip into it).
2. Build the SDK once so its libraries exist: from an *x64 Native Tools Command Prompt for VS*,
   run `setup.bat` then `build.bat` in the SDK folder. This produces `_out/ACE.SDK.lib`,
   `_out/ACE.SDK.dll` and the runtime DLLs (CUDA/ggml/llama/onnx/openblas).
3. Note the SDK root path (the folder containing `data/` and `_out/`). You will reference it below.

## 2. Build `NGAforUnity.dll`

The wrapper links against the SDK's `ACE.SDK.lib` and includes its headers.

1. Open the native project in Visual Studio (the `.vcxproj`/solution under `native/`).
2. Set configuration to **Release / x64**.
3. Point it at your SDK (Project → Properties, All Configurations / x64):
   - **C/C++ → Additional Include Directories**: `<SDK>\source\core` and `<SDK>\external`
   - **Linker → Additional Library Directories**: `<SDK>\_out`
   - **Linker → Input → Additional Dependencies**: `ACE.SDK.lib`
   - **C/C++ → Language → C++ Language Standard**: ISO C++17
   - **C/C++ → Preprocessor**: define `NGAFORUNITY_EXPORTS`
   - **General → Target Name**: `NGAforUnity`
4. Build. Optionally build and run the `NGA_Test` project to validate the DLL without Unity — it
   runs an error-path hardening pass and an autonomous agent that answers a poker question using RAG.

> A CMake build is also described in the architecture doc if you prefer it over the `.vcxproj`.

## 3. Use it in a NEW Unity project

1. **Create a project** in Unity Hub (3D, Built-in or URP), Unity 2021 LTS+.
2. **Copy the C# layer.** Copy the folder `NGA_Sample/Assets/NGAforUnity/` from this repo into your
   project's `Assets/`. It contains `Runtime/` (the bindings) and the example `NGADemo.cs`.
3. **Add the native DLLs.** Create `Assets/Plugins/x86_64/` and copy into it:
   - your freshly built `NGAforUnity.dll`, and
   - **every** DLL from the SDK's `_out/` folder: `ACE.SDK.dll`, `cudart64_12.dll`, `cublas64_12.dll`,
     `cublasLt64_12.dll`, `ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll`, `ggml-cuda.dll`, `llama.dll`,
     `onnxruntime.dll`, `libopenblas.dll`, `cig_scheduler_settings.dll`.

     In the Inspector, make sure each `.dll` is set for **Standalone / Editor**, CPU **x86_64**
     (Unity usually detects this automatically).
4. **Point to the models.** For development, reference the SDK folder by absolute path (see the demo's
   `Sdk Root` field). For a shippable build, place `data/models` and `data/dbs` under
   `Assets/StreamingAssets/` and read them via `Application.streamingAssetsPath`.
5. **Run it.** Create an empty GameObject, add the **NGADemo** component, set **Sdk Root** to your SDK
   path (use forward slashes: `C:/path/to/game-agent-sdk`), and press **Play**. Watch the Console:
   the runtime loads the SLM to the GPU, the agent calls its `search` tool (RAG), and prints an answer.

> **Heads-up (native dependency loading).** Windows does not search the plugin folder for a plugin's
> own dependencies. The bindings call `SetDllDirectory(Assets/Plugins/x86_64)` before the first native
> call (`NGARuntime.Create` → `EnsureLoaded`), which resolves `ACE.SDK.dll` and friends. If you still
> get a `DllNotFoundException`, load `NGAforUnity.dll` explicitly with `LOAD_WITH_ALTERED_SEARCH_PATH`.
>
> **Heads-up (editor locks the DLL).** While the Editor has the plugin loaded you cannot overwrite
> `NGAforUnity.dll`. Close Unity to rebuild the DLL, then reopen.

---

## Quick API tour (C#)

```csharp
using NGAforUnity;

// Load the SLM (do this off the main thread).
var runtime = await NgaRuntime.CreateAsync(new NgaRuntimeConfig {
    SlmPath = sdkRoot + "/data/models/qwen3.5/Qwen3.5-4B-Q4_K_M.gguf",
    EmbeddingModelPaths   = new[] { sdkRoot + "/data/models/miniLM/all-MiniLM-L6-v2.F32.gguf" },
    CrossEncoderModelPath = sdkRoot + "/data/models/miniLM/cross_encoder_ms-marco-MiniLM-L6-v2.onnx",
});

// Stateless chat.
string reply = await runtime.ChatGenerateAsync("[{\"role\":\"user\",\"content\":\"Hi!\"}]");

// RAG.
using var kb = NgaKnowledgeBase.Open(runtime, new NgaKbConfig {
    SemanticDbPath = sdkRoot + "/data/dbs/poker.db",
    LexicalDbPath  = sdkRoot + "/data/dbs/poker-lexical.db",
    EmbeddingModelPath = sdkRoot + "/data/models/miniLM/all-MiniLM-L6-v2.F32.gguf",
});
string hits = kb.Query("what beats a full house?", NgaRagMode.Hybrid, topK: 3);

// Autonomous agent: create, add tools, SendMessage, then pump Step() and answer ToolCall steps.
using var agent = NgaAgent.Create(runtime, new NgaAgentConfig { Instructions = "You are a helpful assistant." });
```

See `NGA_Sample/Assets/NGAforUnity/NGADemo.cs` for a complete agent loop (tool execution via RAG).

---

## License

This wrapper (native and C# code) is licensed under the **Apache License 2.0** — see [`LICENSE`](LICENSE).

**Important:** the **NVIDIA Game Agent SDK is Apache-2.0**, but the **bundled models carry their own
EULA** and are **not** covered by this license. Review NVIDIA's model terms before redistributing, and
never commit the models to your repository.

---

## References

- NVIDIA Game Agent SDK — <https://github.com/NVIDIA/game-agent-sdk>
- NVIDIA ACE for Games — <https://developer.nvidia.com/ace-for-games>
- Build On-Device AI Companions with the ACE Game Agent SDK and UE5 Plugins — <https://developer.nvidia.com/blog/build-on-device-ai-companions-with-the-nvidia-ace-game-agent-sdk-and-unreal-engine-5-plugins/>
- How KRAFTON Built PUBG Ally (Co-Playable Character) — <https://developer.nvidia.com/blog/how-krafton-built-pubg-ally-a-co-playable-character-powered-by-nvidia-ace/>
- NVIDIA ACE Adds Open Source Qwen3 SLM — <https://developer.nvidia.com/blog/nvidia-ace-adds-open-source-qwen3-slm-for-on-device-deployment-in-pc-games/>
