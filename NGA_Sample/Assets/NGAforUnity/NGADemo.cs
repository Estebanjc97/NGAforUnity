// SPDX-License-Identifier: Apache-2.0
using System;
using System.Threading.Tasks;
using UnityEngine;

namespace NGAforUnity
{
    public class NGADemo : MonoBehaviour
    {
        [Tooltip("SDK root (absolute path, forward slashes).")]
        public string sdkRoot = "C:/Work/Personal/NvidiaAceForUnity/NGAforUnity/native/external/game-agent-sdk";
        [TextArea] public string userQuestion = "What hand beats a full house? Answer briefly.";

        private NGARuntime _runtime;
        private NGAKnowledgeBase _kb;
        private NGAAgent _agent;

        // Helper types so Unity's JsonUtility can parse the agent's step JSON.
        [Serializable] class StepContent { public string content; }
        [Serializable] class ToolCall { public string id; public string name; public string arguments; }
        [Serializable] class StepToolCalls { public ToolCall[] toolCalls; }
        [Serializable] class SearchArgs { public string query; }

        async void Start()
        {
            try 
            { 
                await Task.Run(RunAgentDemo); 
            }
            catch (Exception e) 
            { 
                Debug.LogError($"[NGA] {e.Message}"); 
            }
        }

        // Runs on a background thread. Debug.Log is thread-safe; UI is not (see note below).
        private void RunAgentDemo()
        {
            string models = sdkRoot + "/data/models";
            string dbs = sdkRoot + "/data/dbs";
            string emb = models + "/miniLM/all-MiniLM-L6-v2.F32.gguf";

            Debug.Log("[NGA] Loading runtime (SLM -> GPU, first time is slow)...");
            _runtime = NGARuntime.Create(new NGARuntimeConfig
            {
                SlmPath = models + "/qwen3.5/Qwen3.5-4B-Q4_K_M.gguf",
                EmbeddingModelPaths = new[] { emb },
                CrossEncoderModelPath = models + "/miniLM/cross_encoder_ms-marco-MiniLM-L6-v2.onnx",
                MaxContextSize = 4096,
                LogLevel = 1
            });

            Debug.Log("[NGA] " + _runtime.GetInfo());

            _kb = NGAKnowledgeBase.Open(_runtime, new NGAKbConfig
            {
                SemanticDbPath = dbs + "/poker.db",
                LexicalDbPath = dbs + "/poker-lexical.db",
                EmbeddingModelPath = emb
            });

            _agent = NGAAgent.Create(_runtime, new NGAAgentConfig
            {
                Id = "poker",
                Instructions = "You are a helpful poker assistant. Use the search tool to look up poker knowledge before answering. Keep answers under 40 words.",
                MaxSteps = 6
            });
            _agent.AddTool(
                "{\"name\":\"search\",\"description\":\"Search the poker knowledge base for rules, rankings, odds, strategy.\"," +
                "\"parameters\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"the search query\"}},\"required\":[\"query\"]}}");

            _agent.SendMessage(userQuestion);

            for (int step = 0; step < 8; step++)
            {
                var (kind, json) = _agent.Step();

                if (kind == NgaStepKind.Message)
                {
                    Debug.Log("[NGA] AGENT: " + JsonUtility.FromJson<StepContent>(json).content);
                    break;
                }
                else if (kind == NgaStepKind.ToolCall)
                {
                    foreach (var c in JsonUtility.FromJson<StepToolCalls>(json).toolCalls)
                    {
                        Debug.Log($"[NGA] tool {c.name}({c.arguments})");
                        string result = "{}";
                        if (c.name == "search")
                        {
                            string q = JsonUtility.FromJson<SearchArgs>(c.arguments).query;
                            result = _kb.Query(q, NgaRagMode.Hybrid, 3);
                        }
                        _agent.ProvideToolResult(c.id, result);
                    }
                }
                else break; // Done
            }
        }

        private void OnDestroy()
        {
            _agent?.Dispose();
            _kb?.Dispose();
            _runtime?.Dispose();
        }
    }
}