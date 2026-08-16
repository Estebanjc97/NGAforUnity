// SPDX-License-Identifier: Apache-2.0
//
// NGAChatSample: a minimal sample that creates an agent through NGAManager and
// drives a chat UI built with UI Toolkit (entirely in code, no UXML/USS assets).
//
// Setup:
//   1. Add this component to a GameObject that also has a UIDocument component.
//   2. Assign a PanelSettings asset to that UIDocument (required by UI Toolkit).
//   3. Point 'sdkRoot' at your game-agent-sdk folder (same layout as NGADemo).
//   4. Press Play. The runtime loads, an agent is created, and you can chat.
//
// All NGAManager events are dispatched on the main thread, so the handlers below
// touch the UI directly without any extra marshalling.
using System;
using UnityEngine;
using UnityEngine.UIElements;

namespace NGAforUnity.Samples
{
    [RequireComponent(typeof(UIDocument))]
    public sealed class NGAChatSample : MonoBehaviour
    {
        [Header("SDK paths")]
        [Tooltip("SDK root (absolute path, forward slashes). Models/DBs are derived from it.")]
        public string sdkRoot = "C:/Work/Personal/NvidiaAceForUnity/NGAforUnity/native/external/game-agent-sdk";

        [Header("Agent")]
        public string agentId = "assistant";
        [TextArea] public string instructions = "You are a helpful assistant. Keep answers concise and friendly.";
        public int maxSteps = 6;
        [Range(0f, 1f)] public float temperature = 0.2f;

        private NGAManager _manager;
        private NGAAgent _agent;
        private bool _awaiting;

        // UI elements.
        private ScrollView _messages;
        private TextField _input;
        private Button _sendButton;
        private Label _status;

        // ------------------------------------------------------------------ //
        //  Lifecycle
        // ------------------------------------------------------------------ //
        private async void Start()
        {
            BuildUI();

            _manager = NGAManager.Get();
            Subscribe();

            SetInputEnabled(false);
            SetStatus("Loading runtime (the first time is slow)...");

            bool ok = await _manager.InitializeAsync(BuildRuntimeConfig());
            if (!ok) return; // the error is reported through ErrorOccurred

            _agent = _manager.CreateAgent(new NGAAgentConfig
            {
                Id = agentId,
                Instructions = instructions,
                MaxSteps = maxSteps,
                Temperature = temperature
            });
            if (_agent == null) return;

            AddSystem("Agent ready. Type a message and press Enter.");
            SetStatus("Ready");
            SetInputEnabled(true);
            _input.Focus();
        }

        private void OnDestroy()
        {
            Unsubscribe();
            if (_manager != null && _agent != null)
                _manager.RemoveAgent(_agent);
        }

        // ------------------------------------------------------------------ //
        //  NGAManager wiring
        // ------------------------------------------------------------------ //
        private void Subscribe()
        {
            _manager.SdkInitialized += OnSdkInitialized;
            _manager.RuntimeLoaded += OnRuntimeLoaded;
            _manager.AgentMessage += OnAgentMessage;
            _manager.AgentToolCallRequested += OnToolCallRequested;
            _manager.ErrorOccurred += OnError;
        }

        private void Unsubscribe()
        {
            if (_manager == null) return;
            _manager.SdkInitialized -= OnSdkInitialized;
            _manager.RuntimeLoaded -= OnRuntimeLoaded;
            _manager.AgentMessage -= OnAgentMessage;
            _manager.AgentToolCallRequested -= OnToolCallRequested;
            _manager.ErrorOccurred -= OnError;
        }

        private void OnSdkInitialized(uint abi) => SetStatus($"SDK ready (ABI {abi >> 16}.{abi & 0xFFFF})");

        private void OnRuntimeLoaded(NGARuntime runtime) => SetStatus("Runtime loaded. Creating agent...");

        private void OnAgentMessage(NGAAgent agent, string message)
        {
            if (agent != _agent) return;
            AddBubble(string.IsNullOrEmpty(message) ? "(no response)" : message, isUser: false);
            _awaiting = false;
            SetStatus("Ready");
            SetInputEnabled(true);
            _input.Focus();
        }

        // This sample registers no tools, so the agent should not request any. We
        // still answer defensively with an empty result so the loop never stalls.
        private void OnToolCallRequested(NGAAgent agent, NGAToolCall call)
        {
            if (agent != _agent) return;
            _manager.ProvideToolResult(call.Id, "{}");
        }

        private void OnError(NGAError error)
        {
            AddSystem($"[error] {error}");
            _awaiting = false;
            SetStatus("Error");
            SetInputEnabled(true);
        }

        private NGARuntimeConfig BuildRuntimeConfig()
        {
            string models = sdkRoot + "/data/models";
            string emb = models + "/miniLM/all-MiniLM-L6-v2.F32.gguf";
            return new NGARuntimeConfig
            {
                SlmPath = models + "/qwen3.5/Qwen3.5-4B-Q4_K_M.gguf",
                EmbeddingModelPaths = new[] { emb },
                CrossEncoderModelPath = models + "/miniLM/cross_encoder_ms-marco-MiniLM-L6-v2.onnx",
                MaxContextSize = 4096,
                LogLevel = 1
            };
        }

        // ------------------------------------------------------------------ //
        //  Send
        // ------------------------------------------------------------------ //
        private void OnSend()
        {
            if (_agent == null || _awaiting) return;
            string text = _input.value?.Trim();
            if (string.IsNullOrEmpty(text)) return;

            AddBubble(text, isUser: true);
            _input.value = string.Empty;

            _awaiting = true;
            SetInputEnabled(false);
            SetStatus("Thinking...");
            _manager.SendMessage(_agent, text);
        }

        // ------------------------------------------------------------------ //
        //  UI construction (UI Toolkit, in code)
        // ------------------------------------------------------------------ //
        private void BuildUI()
        {
            var document = GetComponent<UIDocument>();
            if (document.panelSettings == null)
                Debug.LogError("[NGAChatSample] The UIDocument has no PanelSettings assigned; the UI will not render.");

            VisualElement root = document.rootVisualElement;
            root.Clear();
            root.style.flexGrow = 1f;
            root.style.flexDirection = FlexDirection.Column;
            root.style.alignItems = Align.Center;
            root.style.backgroundColor = new Color(0.10f, 0.11f, 0.13f);
            root.style.paddingTop = 12f;
            root.style.paddingBottom = 12f;

            // Centered panel with a max width.
            var panel = new VisualElement();
            panel.style.flexGrow = 1f;
            panel.style.width = Length.Percent(100);
            panel.style.maxWidth = 720f;
            panel.style.flexDirection = FlexDirection.Column;
            root.Add(panel);

            var title = new Label("NGA Chat");
            title.style.color = new Color(0.95f, 0.95f, 0.97f);
            title.style.fontSize = 20f;
            title.style.unityFontStyleAndWeight = FontStyle.Bold;
            title.style.marginBottom = 2f;
            title.style.marginLeft = 8f;
            panel.Add(title);

            _status = new Label("Starting...");
            _status.style.color = new Color(0.60f, 0.62f, 0.66f);
            _status.style.fontSize = 12f;
            _status.style.marginBottom = 8f;
            _status.style.marginLeft = 8f;
            panel.Add(_status);

            _messages = new ScrollView(ScrollViewMode.Vertical);
            _messages.style.flexGrow = 1f;
            _messages.style.backgroundColor = new Color(0.13f, 0.14f, 0.17f);
            _messages.style.borderTopLeftRadius = 10f;
            _messages.style.borderTopRightRadius = 10f;
            _messages.style.borderBottomLeftRadius = 10f;
            _messages.style.borderBottomRightRadius = 10f;
            _messages.style.paddingTop = 8f;
            _messages.style.paddingBottom = 8f;
            _messages.style.paddingLeft = 8f;
            _messages.style.paddingRight = 8f;
            panel.Add(_messages);

            // Input row.
            var inputRow = new VisualElement();
            inputRow.style.flexDirection = FlexDirection.Row;
            inputRow.style.marginTop = 8f;
            panel.Add(inputRow);

            _input = new TextField { multiline = false };
            _input.style.flexGrow = 1f;
            _input.style.marginRight = 6f;
            _input.style.minHeight = 30f;
            _input.RegisterCallback<KeyDownEvent>(evt =>
            {
                if (evt.keyCode == KeyCode.Return || evt.keyCode == KeyCode.KeypadEnter)
                {
                    OnSend();
                    evt.StopPropagation();
                }
            });
            inputRow.Add(_input);

            _sendButton = new Button(OnSend) { text = "Send" };
            _sendButton.style.width = 90f;
            _sendButton.style.minHeight = 30f;
            inputRow.Add(_sendButton);
        }

        private void AddBubble(string text, bool isUser)
        {
            var bubble = new Label(text);
            bubble.style.whiteSpace = WhiteSpace.Normal;
            bubble.style.maxWidth = Length.Percent(80);
            bubble.style.marginTop = 4f;
            bubble.style.marginBottom = 4f;
            bubble.style.paddingTop = 8f;
            bubble.style.paddingBottom = 8f;
            bubble.style.paddingLeft = 10f;
            bubble.style.paddingRight = 10f;
            bubble.style.borderTopLeftRadius = 10f;
            bubble.style.borderTopRightRadius = 10f;
            bubble.style.borderBottomLeftRadius = 10f;
            bubble.style.borderBottomRightRadius = 10f;

            if (isUser)
            {
                bubble.style.alignSelf = Align.FlexEnd;
                bubble.style.backgroundColor = new Color(0.20f, 0.45f, 0.85f);
                bubble.style.color = Color.white;
            }
            else
            {
                bubble.style.alignSelf = Align.FlexStart;
                bubble.style.backgroundColor = new Color(0.22f, 0.24f, 0.28f);
                bubble.style.color = new Color(0.92f, 0.93f, 0.95f);
            }

            AppendAndScroll(bubble);
        }

        private void AddSystem(string text)
        {
            var line = new Label(text);
            line.style.whiteSpace = WhiteSpace.Normal;
            line.style.alignSelf = Align.Center;
            line.style.color = new Color(0.55f, 0.57f, 0.61f);
            line.style.fontSize = 12f;
            line.style.unityFontStyleAndWeight = FontStyle.Italic;
            line.style.marginTop = 6f;
            line.style.marginBottom = 6f;
            AppendAndScroll(line);
        }

        private void AppendAndScroll(VisualElement element)
        {
            if (_messages == null) return;
            _messages.Add(element);
            // Scroll to the newest element after layout has been computed.
            _messages.schedule.Execute(() => _messages.ScrollTo(element)).ExecuteLater(16);
        }

        private void SetStatus(string text)
        {
            if (_status != null) _status.text = text;
        }

        private void SetInputEnabled(bool enabled)
        {
            if (_input != null) _input.SetEnabled(enabled);
            if (_sendButton != null) _sendButton.SetEnabled(enabled);
        }
    }
}
