using System.Runtime.InteropServices;

namespace helengine.ps3.builder;

/// <summary>Owns the unmanaged result structure and UTF-8 buffers for one shaderc include response.</summary>
internal sealed class Ps3ShaderIncludeAllocation {
    /// <summary>Native include result structure address.</summary>
    internal IntPtr Result;

    /// <summary>Null-terminated UTF-8 resolved source name address.</summary>
    internal IntPtr SourceName;

    /// <summary>Null-terminated UTF-8 include body or diagnostic address.</summary>
    internal IntPtr Content;

    /// <summary>Frees each owned native block once and clears its address.</summary>
    internal void Free() {
        IntPtr content = Content;
        Content = IntPtr.Zero;
        if (content != IntPtr.Zero) Marshal.FreeHGlobal(content);
        IntPtr sourceName = SourceName;
        SourceName = IntPtr.Zero;
        if (sourceName != IntPtr.Zero) Marshal.FreeHGlobal(sourceName);
        IntPtr result = Result;
        Result = IntPtr.Zero;
        if (result != IntPtr.Zero) Marshal.FreeHGlobal(result);
    }
}
