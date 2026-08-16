// SPDX-License-Identifier: Apache-2.0
//
// NGAManager: a singleton MonoBehaviour that orchestrates the lifecycle of the
// NGA (NVIDIA ACE Game Agent) SDK inside Unity and exposes events for:
//   - SDK initialization,
//   - runtime loading,
//   - knowledge base (RAG) loading,
//   - agent creation and management.
//
// Execution model (as recommended in CLAUDE.md): the blocking inference runs on
// the thread pool and ALL events are dispatched to Unity's main thread through a
// queue drained in Update(), so subscribers can safely touch the Unity API
// (UI, transforms, etc.) from their handlers.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;

namespace NGAforUnity
{
    // NGAToolCall, NGAError and the JsonUtility DTOs (MessageDto/ToolCallDto/ToolCallsDto)
    // live in their own files under Runtime/Core (NGAToolCall.cs, NGAError.cs, NGAData.cs).

    [DisallowMultipleComponent]
    public sealed class NGAManager : MonoBehaviour
    {
        // ------------------------------------------------------------------ //
        //  Singleton
        // ------------------------------------------------------------------ //
        private static NGAManager _instance;

        /// <summary>Current instance, or null if none exists yet.</summary>
        public static NGAManager Instance => _instance;

        /// <summary>Returns the instance, creating it on a persistent GameObject if needed.</summary>
        public static NGAManager Get()
        {
            if (_instance == null)
            {
                var go = new GameObject(nameof(NGAManager));
                _instance = go.AddComponent<NGAManager>();
            }
            return _instance;
        }

        // ------------------------------------------------------------------ //
        //  Inspector configuration (used by InitializeAsync when no config is passed)
        // ------------------------------------------------------------------ //
        [Header("Startup")]
        [Tooltip("If enabled, initializes the SDK and runtime in Start().")]
        [SerializeField] private bool initializeOnStart = false;

        [Header("Runtime")]
        [Tooltip("Absolute path to the SLM model (.gguf). Required.")]
        [SerializeField] private string slmPath = "";

        [Tooltip("Paths to the embedding models used by RAG.")]
        [SerializeField] private string[] embeddingModelPaths = Array.Empty<string>();

        [Tooltip("Path to the cross-encoder (reranker) model. Optional.")]
        [SerializeField] private string crossEncoderModelPath = "";

        [Tooltip("Maximum context size.")]
        [SerializeField] private int maxContextSize = 4096;

        [Tooltip("Inference output buffer size.")]
        [SerializeField] private int inferenceOutputBufferSize = 1024;

        [Tooltip("Native log level.")]
        [SerializeField] private int logLevel = 1;

        // ------------------------------------------------------------------ //
        //  Events
        // ------------------------------------------------------------------ //
        /// <summary>The native SDK loaded and its ABI is compatible. Argument: ABI version.</summary>
        public event Action<uint> SdkInitialized;

        /// <summary>The runtime (SLM + embedders) was loaded onto the GPU.</summary>
        public event Action<NGARuntime> RuntimeLoaded;

        /// <summary>A knowledge base was opened/loaded.</summary>
        public event Action<NGAKnowledgeBase> KnowledgeBaseLoaded;

        /// <summary>A knowledge base was closed.</summary>
        public event Action<NGAKnowledgeBase> KnowledgeBaseClosed;

        /// <summary>An agent was created.</summary>
        public event Action<NGAAgent> AgentCreated;

        /// <summary>An agent was removed (destroyed).</summary>
        public event Action<NGAAgent> AgentRemoved;

        /// <summary>An agent produced a final message. Arguments: agent, text.</summary>
        public event Action<NGAAgent, string> AgentMessage;

        /// <summary>
        /// An agent requests a tool execution. The subscriber must resolve it (unless a
        /// handler was registered via <see cref="SetToolHandler"/>) by calling
        /// <see cref="ProvideToolResult"/> with the call's Id.
        /// </summary>
        public event Action<NGAAgent, NGAToolCall> AgentToolCallRequested;

        /// <summary>An error occurred in any asynchronous operation.</summary>
        public event Action<NGAError> ErrorOccurred;

        // ------------------------------------------------------------------ //
        //  State
        // ------------------------------------------------------------------ //
        /// <summary>Active runtime, or null if not loaded.</summary>
        public NGARuntime Runtime { get; private set; }

        /// <summary>True once the native SDK was verified as loadable and compatible.</summary>
        public bool IsSdkInitialized { get; private set; }

        /// <summary>True when a runtime is ready.</summary>
        public bool IsRuntimeReady => Runtime != null;

        // Tracks an agent together with the metadata the loop needs (id, step budget).
        private sealed class ManagedAgent
        {
            public string Id;
            public NGAAgent Agent;
            public int MaxSteps;
        }

        private readonly ConcurrentDictionary<string, ManagedAgent> _agents = new();
        private readonly List<NGAKnowledgeBase> _knowledgeBases = new();

        /// <summary>Live agents (snapshot).</summary>
        public IReadOnlyCollection<NGAAgent> Agents
        {
            get
            {
                var list = new List<NGAAgent>(_agents.Count);
                foreach (ManagedAgent m in _agents.Values) list.Add(m.Agent);
                return list;
            }
        }

        // Actions to run on the main thread (drained in Update).
        private readonly ConcurrentQueue<Action> _mainThread = new();

        // Serializes inference: even with several agents, only one infers at a time
        // (they share the same model/GPU).
        private readonly SemaphoreSlim _inference = new(1, 1);

        // Tool handlers by name (executed on the main thread).
        private readonly Dictionary<string, Func<string, string>> _toolHandlers = new();

        // Tool calls awaiting resolution through the event (Id -> promise).
        private readonly ConcurrentDictionary<string, TaskCompletionSource<string>> _pendingToolCalls = new();

        private bool _initializing;

        // ------------------------------------------------------------------ //
        //  MonoBehaviour lifecycle
        // ------------------------------------------------------------------ //
        private void Awake()
        {
            if (_instance != null && _instance != this)
            {
                Destroy(gameObject);
                return;
            }
            _instance = this;
            DontDestroyOnLoad(gameObject);
        }

        private async void Start()
        {
            if (initializeOnStart)
                await InitializeAsync();
        }

        private void Update()
        {
            // Run everything queued from background threads on the main thread.
            while (_mainThread.TryDequeue(out Action action))
            {
                try { action(); }
                catch (Exception e) { Debug.LogException(e); }
            }
        }

        private void OnDestroy()
        {
            if (_instance == this)
            {
                ShutdownAll();
                _instance = null;
            }
        }

        // ------------------------------------------------------------------ //
        //  SDK + runtime initialization
        // ------------------------------------------------------------------ //
        /// <summary>
        /// Verifies that the native SDK is loadable/compatible (fires SdkInitialized) and
        /// loads the runtime onto the GPU (fires RuntimeLoaded). Idempotent.
        /// When <paramref name="config"/> is null, it is built from the inspector fields.
        /// </summary>
        public async Task<bool> InitializeAsync(NGARuntimeConfig config = null)
        {
            if (IsRuntimeReady) return true;
            if (_initializing) return false;
            _initializing = true;

            try
            {
                // 1) SDK: confirm the DLL loads and the ABI is compatible.
                if (!IsSdkInitialized)
                {
                    uint abi;
                    try
                    {
                        NGAInterop.EnsureLoaded();
                        abi = NGAInterop.Nga_AbiVersion();
                    }
                    catch (DllNotFoundException e)
                    {
                        RaiseError("sdk", "Native DLL 'NGAforUnity' not found: " + e.Message);
                        return false;
                    }
                    catch (Exception e)
                    {
                        RaiseError("sdk", "Failed to load the native SDK: " + e.Message);
                        return false;
                    }

                    uint major = abi >> 16;
                    if (major != NGARuntime.ExpectedAbiMajor)
                    {
                        RaiseError("sdk", $"Incompatible ABI: native major={major}, expected={NGARuntime.ExpectedAbiMajor}");
                        return false;
                    }

                    IsSdkInitialized = true;
                    Post(() => SdkInitialized?.Invoke(abi));
                }

                // 2) Runtime: load models onto the GPU (blocking -> thread pool).
                NGARuntimeConfig cfg = config ?? BuildRuntimeConfigFromInspector();
                try
                {
                    NGARuntime rt = await NGARuntime.CreateAsync(cfg).ConfigureAwait(false);
                    Runtime = rt;
                    Post(() => RuntimeLoaded?.Invoke(rt));
                    return true;
                }
                catch (Exception e)
                {
                    RaiseError("runtime", e);
                    return false;
                }
            }
            finally
            {
                _initializing = false;
            }
        }

        private NGARuntimeConfig BuildRuntimeConfigFromInspector() => new NGARuntimeConfig
        {
            SlmPath = slmPath,
            EmbeddingModelPaths = embeddingModelPaths,
            CrossEncoderModelPath = crossEncoderModelPath,
            MaxContextSize = maxContextSize,
            InferenceOutputBufferSize = inferenceOutputBufferSize,
            LogLevel = logLevel
        };

        // ------------------------------------------------------------------ //
        //  Knowledge bases (RAG)
        // ------------------------------------------------------------------ //
        /// <summary>Opens a knowledge base and fires KnowledgeBaseLoaded.</summary>
        public async Task<NGAKnowledgeBase> OpenKnowledgeBaseAsync(NGAKbConfig config)
        {
            if (!EnsureRuntime("knowledge base")) return null;
            try
            {
                NGAKnowledgeBase kb = await NGAKnowledgeBase.OpenAsync(Runtime, config).ConfigureAwait(false);
                lock (_knowledgeBases) _knowledgeBases.Add(kb);
                Post(() => KnowledgeBaseLoaded?.Invoke(kb));
                return kb;
            }
            catch (Exception e)
            {
                RaiseError("knowledge base", e);
                return null;
            }
        }

        /// <summary>Closes a knowledge base and fires KnowledgeBaseClosed.</summary>
        public void CloseKnowledgeBase(NGAKnowledgeBase kb)
        {
            if (kb == null) return;
            lock (_knowledgeBases) _knowledgeBases.Remove(kb);
            Post(() => KnowledgeBaseClosed?.Invoke(kb));
            kb.Dispose();
        }

        // ------------------------------------------------------------------ //
        //  Agents: creation and management
        // ------------------------------------------------------------------ //
        /// <summary>
        /// Creates an agent (optionally registering its tools) and fires AgentCreated.
        /// Returns null if the runtime is not ready or the id already exists.
        /// </summary>
        public NGAAgent CreateAgent(NGAAgentConfig config, IEnumerable<string> toolSchemasJson = null)
        {
            if (!EnsureRuntime("agent")) return null;
            if (config == null) { RaiseError("agent", "config is null"); return null; }

            string id = string.IsNullOrEmpty(config.Id) ? "agent" : config.Id;
            if (_agents.ContainsKey(id))
            {
                RaiseError("agent", $"an agent with id '{id}' already exists");
                return null;
            }

            try
            {
                NGAAgent agent = NGAAgent.Create(Runtime, config);
                if (toolSchemasJson != null)
                    foreach (string schema in toolSchemasJson)
                        if (!string.IsNullOrEmpty(schema)) agent.AddTool(schema);

                _agents[id] = new ManagedAgent { Id = id, Agent = agent, MaxSteps = Math.Max(1, config.MaxSteps) };
                Post(() => AgentCreated?.Invoke(agent));
                return agent;
            }
            catch (Exception e)
            {
                RaiseError("agent", e);
                return null;
            }
        }

        /// <summary>Returns the agent by id, or null.</summary>
        public NGAAgent GetAgent(string id) =>
            id != null && _agents.TryGetValue(id, out ManagedAgent m) ? m.Agent : null;

        /// <summary>Destroys an agent and fires AgentRemoved.</summary>
        public void RemoveAgent(NGAAgent agent)
        {
            ManagedAgent managed = FindManaged(agent);
            if (managed != null) RemoveAgent(managed.Id);
        }

        /// <summary>Destroys an agent by id and fires AgentRemoved.</summary>
        public void RemoveAgent(string id)
        {
            if (id != null && _agents.TryRemove(id, out ManagedAgent managed))
            {
                Post(() => AgentRemoved?.Invoke(managed.Agent));
                managed.Agent.Dispose();
            }
        }

        /// <summary>
        /// Sends a user message to an agent and starts its reasoning loop. Results arrive
        /// through the AgentMessage / AgentToolCallRequested events.
        /// </summary>
        public void SendMessage(NGAAgent agent, string message)
        {
            ManagedAgent managed = FindManaged(agent);
            if (managed == null) { RaiseError("agent", "unknown agent (create it through NGAManager)"); return; }
            _ = RunAgentLoopAsync(managed, message);
        }

        /// <summary>Overload by id.</summary>
        public void SendMessage(string agentId, string message)
        {
            if (agentId != null && _agents.TryGetValue(agentId, out ManagedAgent managed))
                _ = RunAgentLoopAsync(managed, message);
            else
                RaiseError("agent", $"agent '{agentId}' not found");
        }

        // ------------------------------------------------------------------ //
        //  Tools
        // ------------------------------------------------------------------ //
        /// <summary>
        /// Registers a synchronous handler for a tool by name. It receives the arguments
        /// (JSON) and returns the result (JSON). It runs on the main thread, so it can read
        /// Unity state safely. If no handler exists for a tool, AgentToolCallRequested is
        /// raised instead.
        /// </summary>
        public void SetToolHandler(string toolName, Func<string, string> handler)
        {
            if (string.IsNullOrEmpty(toolName)) return;
            if (handler == null) _toolHandlers.Remove(toolName);
            else _toolHandlers[toolName] = handler;
        }

        /// <summary>Delivers the result of a tool call requested via AgentToolCallRequested.</summary>
        public void ProvideToolResult(string toolCallId, string resultJson)
        {
            if (toolCallId != null && _pendingToolCalls.TryRemove(toolCallId, out TaskCompletionSource<string> tcs))
                tcs.TrySetResult(resultJson ?? "{}");
        }

        // ------------------------------------------------------------------ //
        //  Cleanup
        // ------------------------------------------------------------------ //
        /// <summary>Destroys all agents, knowledge bases and the runtime.</summary>
        public void ShutdownAll()
        {
            foreach (ManagedAgent m in _agents.Values)
            {
                try { m.Agent.Dispose(); } catch (Exception e) { Debug.LogException(e); }
            }
            _agents.Clear();

            lock (_knowledgeBases)
            {
                foreach (NGAKnowledgeBase kb in _knowledgeBases)
                {
                    try { kb.Dispose(); } catch (Exception e) { Debug.LogException(e); }
                }
                _knowledgeBases.Clear();
            }

            try { Runtime?.Dispose(); } catch (Exception e) { Debug.LogException(e); }
            Runtime = null;
            IsSdkInitialized = false;
        }

        // ------------------------------------------------------------------ //
        //  Internal agent loop
        // ------------------------------------------------------------------ //
        private async Task RunAgentLoopAsync(ManagedAgent managed, string message)
        {
            NGAAgent agent = managed.Agent;

            // The native agent finishes a turn ONLY when a Step returns Done (the SDK's
            // ACEAgentStatus_Idle). Message (ResponseText) and ToolCall are intermediate:
            // the loop must keep pumping until Done, otherwise the turn is never finalized
            // and the NEXT user turn returns no response. See the SDK Agent-Sample loop:
            //   AddUserInput(...); do { Run(); ... } while (status != Idle/Shutdown/Cancelled);
            // We therefore accumulate the response text and raise AgentMessage exactly once,
            // when the turn completes. This also prevents a race where the UI would re-enable
            // input (on the message) while this turn is still finalizing.
            int guard = managed.MaxSteps * 2 + 8; // room for tool rounds + the final Idle step
            string response = null;

            try
            {
                // Add the user input (cheap, no inference) under the same serialization lock.
                await RunLockedAsync(() => agent.SendMessage(message)).ConfigureAwait(false);

                for (int i = 0; i < guard; i++)
                {
                    (NgaStepKind kind, string json) = await RunLockedAsync(() => agent.Step()).ConfigureAwait(false);

                    switch (kind)
                    {
                        case NgaStepKind.Message:
                            string content = ParseMessage(json);
                            if (!string.IsNullOrEmpty(content))
                                response = response == null ? content : response + "\n" + content;
                            break; // keep pumping until Done

                        case NgaStepKind.ToolCall:
                            ToolCallDto[] calls = ParseToolCalls(json);
                            if (calls != null)
                            {
                                foreach (ToolCallDto dto in calls)
                                {
                                    var call = new NGAToolCall(dto.id, dto.name, dto.arguments);
                                    string result = await ResolveToolCallAsync(agent, call).ConfigureAwait(false);
                                    await RunLockedAsync(() => agent.ProvideToolResult(call.Id, result)).ConfigureAwait(false);
                                }
                            }
                            break;

                        case NgaStepKind.Done:
                            Post(() => AgentMessage?.Invoke(agent, response ?? string.Empty));
                            return;

                        case NgaStepKind.Running:
                        default:
                            break; // keep pumping
                    }
                }

                // Guard exhausted: still deliver whatever we gathered so the UI unblocks.
                Post(() => AgentMessage?.Invoke(agent, response ?? string.Empty));
            }
            catch (Exception e)
            {
                RaiseError("agent loop", e);
            }
        }

        // Runs a blocking native call under the inference lock, on the thread pool.
        private async Task<T> RunLockedAsync<T>(Func<T> fn)
        {
            await _inference.WaitAsync().ConfigureAwait(false);
            try { return await Task.Run(fn).ConfigureAwait(false); }
            finally { _inference.Release(); }
        }

        private async Task RunLockedAsync(Action fn)
        {
            await _inference.WaitAsync().ConfigureAwait(false);
            try { await Task.Run(fn).ConfigureAwait(false); }
            finally { _inference.Release(); }
        }

        // Resolves a tool call: uses the registered handler if present; otherwise raises the event.
        private Task<string> ResolveToolCallAsync(NGAAgent agent, NGAToolCall call)
        {
            var tcs = new TaskCompletionSource<string>();

            if (_toolHandlers.TryGetValue(call.Name, out Func<string, string> handler))
            {
                Post(() =>
                {
                    try { tcs.TrySetResult(handler(call.Arguments) ?? "{}"); }
                    catch (Exception e) { tcs.TrySetResult("{\"error\":" + JsonString(e.Message) + "}"); }
                });
            }
            else
            {
                _pendingToolCalls[call.Id] = tcs;
                Post(() => AgentToolCallRequested?.Invoke(agent, call));
            }

            return tcs.Task;
        }

        // ------------------------------------------------------------------ //
        //  Helpers
        // ------------------------------------------------------------------ //
        private ManagedAgent FindManaged(NGAAgent agent)
        {
            if (agent == null) return null;
            foreach (ManagedAgent m in _agents.Values)
                if (ReferenceEquals(m.Agent, agent)) return m;
            return null;
        }

        private bool EnsureRuntime(string context)
        {
            if (IsRuntimeReady) return true;
            RaiseError(context, "the runtime is not initialized (call InitializeAsync first)");
            return false;
        }

        private void Post(Action action)
        {
            if (action != null) _mainThread.Enqueue(action);
        }

        private void RaiseError(string context, Exception e)
        {
            NgaResult code = e is NGAException nex ? nex.Result : NgaResult.Ok;
            RaiseError(context, e.Message, code);
        }

        private void RaiseError(string context, string message, NgaResult code = NgaResult.Ok)
        {
            var err = new NGAError(context, message, code);
            Post(() =>
            {
                Debug.LogError($"[NGA] {err}");
                ErrorOccurred?.Invoke(err);
            });
        }

        private static string ParseMessage(string json)
        {
            if (string.IsNullOrEmpty(json)) return string.Empty;
            try { return JsonUtility.FromJson<MessageDto>(json)?.content ?? string.Empty; }
            catch { return string.Empty; }
        }

        private static ToolCallDto[] ParseToolCalls(string json)
        {
            if (string.IsNullOrEmpty(json)) return null;
            try { return JsonUtility.FromJson<ToolCallsDto>(json)?.toolCalls; }
            catch { return null; }
        }

        // Escapes a string as a JSON literal (quotes included).
        private static string JsonString(string s)
        {
            if (s == null) return "\"\"";
            var sb = new StringBuilder(s.Length + 2);
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < ' ') sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
            return sb.ToString();
        }
    }
}
