// SPDX-License-Identifier: Apache-2.0
using System;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace NGAforUnity
{
    internal sealed class NGAKbSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public NGAKbSafeHandle(IntPtr h) : base(true) { SetHandle(h); }
        protected override bool ReleaseHandle() { NGAInterop.NgaRag_CloseKB(handle); return true; }
    }

    public sealed class NGAKbConfig
    {
        public string SemanticDbPath;
        public string LexicalDbPath;
        public string EmbeddingModelPath;

        internal string ToJson()
        {
            var sb = new StringBuilder("{");
            bool first = true;
            void Add(string k, string v)
            {
                if (string.IsNullOrEmpty(v)) return;
                if (!first) sb.Append(',');
                sb.Append('"').Append(k).Append("\":").Append(NGARuntimeConfig.J(v));
                first = false;
            }
            Add("semanticDbPath", SemanticDbPath);
            Add("lexicalDbPath", LexicalDbPath);
            Add("embeddingModelPath", EmbeddingModelPath);
            return sb.Append('}').ToString();
        }
    }

    public sealed class NGAKnowledgeBase : IDisposable
    {
        private readonly NGAKbSafeHandle _handle;
        internal IntPtr Handle => _handle.DangerousGetHandle();
        private NGAKnowledgeBase(NGAKbSafeHandle h) => _handle = h;

        public static NGAKnowledgeBase Open(NGARuntime runtime, NGAKbConfig config)
        {
            if (runtime == null) throw new ArgumentNullException(nameof(runtime));
            if (config == null) throw new ArgumentNullException(nameof(config));
            NgaResult r = NGAInterop.NgaRag_OpenKB(runtime.Handle, config.ToJson(), out IntPtr raw);
            NGAException.Check(r, "NgaRag_OpenKB");
            return new NGAKnowledgeBase(new NGAKbSafeHandle(raw));
        }

        public static Task<NGAKnowledgeBase> OpenAsync(NGARuntime runtime, NGAKbConfig config) =>
            Task.Run(() => Open(runtime, config));

        // Returns JSON: [{"id","document","distance","numTokens"}, ...]
        public string Query(string query, NgaRagMode mode = NgaRagMode.Hybrid, int topK = 3) =>
            NGAMarshal.ReadString((byte[] b, int l, out int w) =>
                NGAInterop.NgaRag_Query(Handle, query, mode, topK, b, l, out w), "NgaRag_Query", 32768);

        public Task<string> QueryAsync(string query, NgaRagMode mode = NgaRagMode.Hybrid, int topK = 3) =>
            Task.Run(() => Query(query, mode, topK));

        public void Dispose() => _handle?.Dispose();
    }
}