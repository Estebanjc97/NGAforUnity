using UnityEngine;

namespace NGAforUnity
{
    /// <summary>A tool-call request emitted by an agent during its inference loop.</summary>
    public readonly struct NGAToolCall
    {
        /// <summary>Unique id of this call; used to return its result.</summary>
        public readonly string Id;
        /// <summary>Name of the requested tool.</summary>
        public readonly string Name;
        /// <summary>Tool arguments as a JSON string.</summary>
        public readonly string Arguments;

        public NGAToolCall(string id, string name, string arguments)
        {
            Id = id;
            Name = name;
            Arguments = arguments;
        }

        public override string ToString() => $"{Name}#{Id}({Arguments})";
    }
}
