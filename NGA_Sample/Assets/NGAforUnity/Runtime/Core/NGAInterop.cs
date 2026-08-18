// SPDX-License-Identifier: Apache-2.0
using System;
using System.IO;
using System.Runtime.InteropServices;
using UnityEngine;

namespace NGAforUnity
{
    public enum NgaResult
    {
        Ok              = 0, 
        InvalidArg      = 1, 
        NoGpu           = 2,
        ModelLoad       = 3,
        Inference       = 4,
        BufferSmall     = 5,
        NotReady        = 6,
        InvalidJson     = 7,
        NotFound        = 8,
        Internal        = 99
    }

    public enum NgaRagMode 
    { 
        Semantic    = 0, 
        Lexical     = 1, 
        Hybrid      = 2 
    }

    public enum NgaStepKind 
    { 
        Done        = 0,
        ToolCall    = 1,
        Message     = 2,
        Running     = 3 
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void NgaTokenCallback(IntPtr tokenUtf8, IntPtr userData);

    internal static class NGAInterop
    {
        internal const string Dll = "NGAforUnity";
        private const CallingConvention Cdecl = CallingConvention.Cdecl;

        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool SetDllDirectory(string lpPathName);

        private static bool _ready;

        internal static void EnsureLoaded()
        {
            if (_ready) return;
            _ready = true;
            // Assets/Plugins/x86_64 in the editor; <App>_Data/Plugins/x86_64 in a build.
            string dir = Path.Combine(Application.dataPath, "Plugins", "x86_64");
            if (!SetDllDirectory(dir))
                Debug.LogWarning($"[NGA] SetDllDirectory failed for {dir}");
        }

        internal static string LastError()
        {
            IntPtr p = Nga_LastError();
            return p == IntPtr.Zero ? "" : (Marshal.PtrToStringUTF8(p) ?? "");
        }

        // --- Utilities -----------------------------------------------------------
        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern uint Nga_AbiVersion();

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern IntPtr Nga_LastError();

        // --- Runtime -------------------------------------------------------------
        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaRuntime_Create(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string configJson, out IntPtr outHandle);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern void NgaRuntime_Destroy(IntPtr rt);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaRuntime_GetInfo(IntPtr rt, [Out] byte[] outJson, int len, out int written);

        // --- Chat ----------------------------------------------------------------
        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaChat_Generate(IntPtr rt,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string messagesJson,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string paramsJson,
            [Out] byte[] outBuf, int len, out int written);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaChat_GenerateStream(IntPtr rt,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string messagesJson,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string paramsJson,
            NgaTokenCallback onToken, IntPtr userData);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaChat_GenerateWithTools(IntPtr rt,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string messagesJson,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string toolsJson,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string paramsJson,
            [Out] byte[] outJson, int len, out int written);

        // --- RAG -----------------------------------------------------------------
        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaRag_OpenKB(IntPtr rt,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string kbConfigJson, out IntPtr outHandle);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern void NgaRag_CloseKB(IntPtr kb);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaRag_Query(IntPtr kb,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string query,
            NgaRagMode mode, int topK, [Out] byte[] outJson, int len, out int written);

        // --- Agent ---------------------------------------------------------------
        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_Create(IntPtr rt,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string agentConfigJson, out IntPtr outHandle);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern void NgaAgent_Destroy(IntPtr a);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_AddTool(IntPtr a,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string toolSchemaJson);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_SetSystemPrompt(IntPtr a,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string prompt);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_SendMessage(IntPtr a,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string content);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_Step(IntPtr a, out NgaStepKind kind,
            [Out] byte[] outJson, int len, out int written);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_ProvideToolResult(IntPtr a,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string toolCallId,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string resultJson);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_GetHistory(IntPtr a,
            [Out] byte[] outJson, int len, out int written);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_Cancel(IntPtr a);

        [DllImport(Dll, CallingConvention = Cdecl)]
        internal static extern NgaResult NgaAgent_ClearCancel(IntPtr a);
    }
}