using UnityEngine;

namespace NGAforUnity
{
    /// <summary>Error information reported through <see cref="NGAManager.ErrorOccurred"/>.</summary>
    public readonly struct NGAError
    {
        /// <summary>Stage where the error happened (e.g. "runtime", "agent loop").</summary>
        public readonly string Context;
        /// <summary>Human-readable message.</summary>
        public readonly string Message;
        /// <summary>Native code if applicable; Ok when the error is not from the ABI.</summary>
        public readonly NgaResult Result;

        public NGAError(string context, string message, NgaResult result = NgaResult.Ok)
        {
            Context = context;
            Message = message;
            Result = result;
        }

        public override string ToString() => $"[{Context}] {Message}";
    }
}
