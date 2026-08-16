// SPDX-License-Identifier: Apache-2.0
using System;
using System.Text;

namespace NGAforUnity
{
    internal static class NGAMarshal
    {
        internal delegate NgaResult BufferCall(byte[] buf, int len, out int written);

        internal static string ReadString(BufferCall call, string context, int initial = 16384)
        {
            var buf = new byte[initial];
            NgaResult r = call(buf, buf.Length, out int written);
            if (r == NgaResult.BufferSmall && written > buf.Length)
            {
                buf = new byte[written];
                r = call(buf, buf.Length, out written);
            }
            NGAException.Check(r, context);
            int n = Math.Max(0, written - 1);
            return Encoding.UTF8.GetString(buf, 0, n);
        }
    }
}