// SPDX-License-Identifier: Apache-2.0
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace NGAforUnity
{
    // Frees the GPU runtime automatically (Dispose, GC, or domain reload).
    internal sealed class NGARuntimeSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public NGARuntimeSafeHandle(IntPtr h) : base(true) 
        {
            SetHandle(h);
        }
        protected override bool ReleaseHandle() 
        { 
            NGAInterop.NgaRuntime_Destroy(handle); 
            return true;
        }
    }

    public sealed class NGARuntimeConfig
    {
        public string SlmPath;
        public string[] EmbeddingModelPaths;
        public string CrossEncoderModelPath;
        public int MaxContextSize = 4096;
        public int InferenceOutputBufferSize = 1024;
        public int LogLevel = 1;

        internal string ToJson()
        {
            var sb = new StringBuilder("{");
            sb.Append("\"slmPath\":").Append(J(SlmPath));
            if (EmbeddingModelPaths != null && EmbeddingModelPaths.Length > 0)
            {
                sb.Append(",\"embeddingModelPaths\":[");
                for (int i = 0; i < EmbeddingModelPaths.Length; i++)
                {
                    if (i > 0) sb.Append(',');
                    sb.Append(J(EmbeddingModelPaths[i]));
                }
                sb.Append(']');
            }
            if (!string.IsNullOrEmpty(CrossEncoderModelPath))
                sb.Append(",\"crossEncoderModelPath\":").Append(J(CrossEncoderModelPath));
            sb.Append(",\"maxContextSize\":").Append(MaxContextSize);
            sb.Append(",\"inferenceOutputBufferSize\":").Append(InferenceOutputBufferSize);
            sb.Append(",\"logLevel\":").Append(LogLevel).Append('}');
            return sb.ToString();
        }

        internal static string J(string s)
        {
            if (s == null) return "\"\"";
            var sb = new StringBuilder("\"");
            foreach (char c in s)
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default: sb.Append(c); break;
                }
            return sb.Append('"').ToString();
        }
    }

    public sealed class NGARuntime : IDisposable
    {
        public const uint ExpectedAbiMajor = 0;
        private readonly NGARuntimeSafeHandle _handle;
        internal IntPtr Handle => _handle.DangerousGetHandle();

        private NGARuntime(NGARuntimeSafeHandle h) => _handle = h;

        public static NGARuntime Create(NGARuntimeConfig config)
        {
            if (config == null) throw new ArgumentNullException(nameof(config));
            NGAInterop.EnsureLoaded();
            CheckAbi();
            NgaResult r = NGAInterop.NgaRuntime_Create(config.ToJson(), out IntPtr raw);
            NGAException.Check(r, "NgaRuntime_Create");
            NGARuntimeSafeHandle safe = new NGARuntimeSafeHandle(raw);
            return new NGARuntime(safe);
        }

        public static Task<NGARuntime> CreateAsync(NGARuntimeConfig config) => Task.Run(() => Create(config));

        public string GetInfo() =>
            NGAMarshal.ReadString((byte[] b, int l, out int w) => NGAInterop.NgaRuntime_GetInfo(Handle, b, l, out w), "GetInfo");

        // --- Chat ---
        public string ChatGenerate(string messagesJson, string paramsJson = null) =>
            NGAMarshal.ReadString((byte[] b, int l, out int w) =>
                NGAInterop.NgaChat_Generate(Handle, messagesJson, paramsJson, b, l, out w), "ChatGenerate");

        public Task<string> ChatGenerateAsync(string messagesJson, string paramsJson = null) =>
            Task.Run(() => ChatGenerate(messagesJson, paramsJson));

        // Streaming. onToken is invoked on a BACKGROUND thread — do not touch Unity APIs in it.
        public void ChatGenerateStream(string messagesJson, Action<string> onToken, string paramsJson = null)
        {
            NgaTokenCallback cb = (IntPtr tok, IntPtr user) =>
                onToken(tok == IntPtr.Zero ? "" : (Marshal.PtrToStringUTF8(tok) ?? ""));
            GCHandle gargabeAllocated = GCHandle.Alloc(cb); // keep the delegate alive for the whole call
            try
            {
                NgaResult r = NGAInterop.NgaChat_GenerateStream(Handle, messagesJson, paramsJson, cb, IntPtr.Zero);
                NGAException.Check(r, "ChatGenerateStream");
            }
            finally
            { 
                gargabeAllocated.Free(); 
            }
        }

        public Task ChatGenerateStreamAsync(string messagesJson, Action<string> onToken, string paramsJson = null) =>
            Task.Run(() => ChatGenerateStream(messagesJson, onToken, paramsJson));

        public static void CheckAbi()
        {
            uint abi = NGAInterop.Nga_AbiVersion();
            if ((abi >> 16) != ExpectedAbiMajor)
                throw new InvalidOperationException($"NGAforUnity.dll ABI major {abi >> 16} != expected {ExpectedAbiMajor}");
        }

        public void Dispose() => _handle?.Dispose();
    }
}