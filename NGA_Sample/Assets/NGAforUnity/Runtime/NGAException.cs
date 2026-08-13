// SPDX-License-Identifier: Apache-2.0
using System;

namespace NGAforUnity
{
    public sealed class NGAException : Exception
    {
        public NgaResult Result { get; }
        public NGAException(NgaResult r, string message) : base($"NGA error {r}: {message}") => Result = r;

        internal static void Check(NgaResult r, string context)
        {
            if (r != NgaResult.Ok)
                throw new NGAException(r, $"{context} — {NGAInterop.LastError()}");
        }
    }
}