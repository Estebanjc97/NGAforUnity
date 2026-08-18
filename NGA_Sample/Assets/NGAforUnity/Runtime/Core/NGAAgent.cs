// SPDX-License-Identifier: Apache-2.0
using System;
using System.Globalization;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace NGAforUnity
{
    internal sealed class NGAAgentSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public NGAAgentSafeHandle(IntPtr h) : base(true) { SetHandle(h); }
        protected override bool ReleaseHandle() { NGAInterop.NgaAgent_Destroy(handle); return true; }
    }

    public sealed class NGAAgentConfig
    {
        public string Id = "agent";
        public string Instructions;
        public int MaxSteps = 10;
        public int HistoryWindow = 50;
        public float Temperature = 0.2f;
        public bool EnableThink = false;

        internal string ToJson()
        {
            var sb = new StringBuilder("{");
            sb.Append("\"id\":").Append(NGARuntimeConfig.J(Id));
            if (!string.IsNullOrEmpty(Instructions))
                sb.Append(",\"instructions\":").Append(NGARuntimeConfig.J(Instructions));
            sb.Append(",\"maxSteps\":").Append(MaxSteps);
            sb.Append(",\"historyWindow\":").Append(HistoryWindow);
            sb.Append(",\"temperature\":").Append(Temperature.ToString(CultureInfo.InvariantCulture));
            sb.Append(",\"enableThink\":").Append(EnableThink ? "true" : "false");
            return sb.Append('}').ToString();
        }
    }

    public sealed class NGAAgent : IDisposable
    {
        private readonly NGAAgentSafeHandle _handle;
        internal IntPtr Handle => _handle.DangerousGetHandle();
        private NGAAgent(NGAAgentSafeHandle h) => _handle = h;

        public static NGAAgent Create(NGARuntime runtime, NGAAgentConfig config)
        {
            if (runtime == null) throw new ArgumentNullException(nameof(runtime));
            if (config == null) config = new NGAAgentConfig();
            NgaResult r = NGAInterop.NgaAgent_Create(runtime.Handle, config.ToJson(), out IntPtr raw);
            NGAException.Check(r, "NgaAgent_Create");
            return new NGAAgent(new NGAAgentSafeHandle(raw));
        }

        public void AddTool(string toolSchemaJson) =>
            NGAException.Check(NGAInterop.NgaAgent_AddTool(Handle, toolSchemaJson), "AddTool");

        public void SetSystemPrompt(string prompt) =>
            NGAException.Check(NGAInterop.NgaAgent_SetSystemPrompt(Handle, prompt), "SetSystemPrompt");

        public void SendMessage(string content) =>
            NGAException.Check(NGAInterop.NgaAgent_SendMessage(Handle, content), "SendMessage");

        public void ProvideToolResult(string toolCallId, string resultJson) =>
            NGAException.Check(NGAInterop.NgaAgent_ProvideToolResult(Handle, toolCallId, resultJson), "ProvideToolResult");

        public void Cancel() =>
            NGAException.Check(NGAInterop.NgaAgent_Cancel(Handle), "Cancel");

        public void ClearCancel() =>
            NGAException.Check(NGAInterop.NgaAgent_ClearCancel(Handle), "ClearCancel");

        // One step. Returns the kind and the JSON payload
        // (Message -> {"content":...}; ToolCall -> {"toolCalls":[...]}).
        public (NgaStepKind kind, string json) Step()
        {
            NgaStepKind kind = NgaStepKind.Done;
            string json = NGAMarshal.ReadString((byte[] b, int l, out int w) =>
            {
                NgaResult r = NGAInterop.NgaAgent_Step(Handle, out NgaStepKind k, b, l, out w);
                kind = k;
                return r;
            }, "Agent.Step", 65536);
            return (kind, json);
        }

        public string GetHistory() =>
            NGAMarshal.ReadString((byte[] b, int l, out int w) =>
                NGAInterop.NgaAgent_GetHistory(Handle, b, l, out w), "GetHistory", 65536);

        public void Dispose() => _handle?.Dispose();
    }
}