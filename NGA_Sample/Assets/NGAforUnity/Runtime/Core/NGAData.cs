using System;

namespace NGAforUnity
{
    // DTOs used to parse the native output with JsonUtility.
    [Serializable] public class MessageDto { public string content; }
    [Serializable] public class ToolCallDto { public string id; public string name; public string arguments; }
    [Serializable] public class ToolCallsDto { public ToolCallDto[] toolCalls; }
}