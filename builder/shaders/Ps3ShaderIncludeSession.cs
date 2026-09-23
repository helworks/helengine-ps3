using helengine;
using Silk.NET.Shaderc;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace helengine.ps3.builder;

/// <summary>Owns shaderc include callbacks, resolved source records and native include memory for one compile.</summary>
public sealed unsafe class Ps3ShaderIncludeSession : IDisposable {
    /// <summary>Serializes dependency consistency checks and native allocation bookkeeping.</summary>
    readonly object Gate = new();

    /// <summary>Resolver invoked for active include directives.</summary>
    readonly IShaderIncludeResolver IncludeResolver;

    /// <summary>Native allocator used only for shaderc include result storage.</summary>
    readonly Func<int, IntPtr> NativeAllocator;

    /// <summary>Strong references that keep native callback delegates alive until session disposal.</summary>
    readonly IncludeResolveFn ResolveDelegate;

    /// <summary>Strong reference that keeps the native release delegate alive until session disposal.</summary>
    readonly IncludeResultReleaseFn ReleaseDelegate;

    /// <summary>Silk callback handle retained until shaderc has released its options and results.</summary>
    readonly PfnIncludeResolveFn ResolveCallbackHandle;

    /// <summary>Silk release handle retained until shaderc has released its options and results.</summary>
    readonly PfnIncludeResultReleaseFn ReleaseCallbackHandle;

    /// <summary>Resolved source contents used to detect inconsistent repeated paths.</summary>
    readonly Dictionary<string, string> ResolvedContents = new(StringComparer.Ordinal);

    /// <summary>Every active include resolution, preserving repeats in callback order.</summary>
    readonly List<Ps3ShaderSourceDependency> DependenciesList = [];

    /// <summary>Native include result blocks still owned by shaderc or awaiting final session cleanup.</summary>
    readonly Dictionary<IntPtr, Ps3ShaderIncludeAllocation> Allocations = [];

    /// <summary>Preallocated include error returned if any callback allocation fails.</summary>
    readonly Ps3ShaderIncludeAllocation EmergencyAllocation;

    /// <summary>Whether this session has released all callback allocations.</summary>
    bool IsDisposed;

    /// <summary>The first fatal resolver or consistency error, retained across shaderc lookup retries.</summary>
    Exception FatalFailureValue;

    /// <summary>Whether the first fatal callback failure was a same-path content change.</summary>
    bool FatalConsistencyFailure;

    /// <summary>Resolved or requested path associated with the first fatal callback failure.</summary>
    string FatalFailurePath;

    /// <summary>Source path that requested the include associated with the first fatal callback failure.</summary>
    string FatalFailureRequestingFile;

    /// <summary>Initializes a session with the process unmanaged allocator.</summary>
    /// <param name="includeResolver">Resolver for active source include directives.</param>
    public Ps3ShaderIncludeSession(IShaderIncludeResolver includeResolver) : this(includeResolver, Marshal.AllocHGlobal) { }

    /// <summary>Initializes rooted callbacks and allocation ownership for one compiler invocation.</summary>
    /// <param name="includeResolver">Resolver for active source include directives.</param>
    /// <param name="nativeAllocator">Allocator used for include result structures and UTF-8 buffers.</param>
    internal Ps3ShaderIncludeSession(IShaderIncludeResolver includeResolver, Func<int, IntPtr> nativeAllocator) {
        IncludeResolver = includeResolver ?? throw new ArgumentNullException(nameof(includeResolver));
        NativeAllocator = nativeAllocator ?? throw new ArgumentNullException(nameof(nativeAllocator));
        EmergencyAllocation = CreateAllocation("ps3-shaderc-include-error", "PS3 shaderc include callback failed during native allocation.", true);

        IncludeResolveFn resolveDelegate = ResolveInclude;
        IncludeResultReleaseFn releaseDelegate = ReleaseInclude;
        PfnIncludeResolveFn resolveCallbackHandle = default;
        PfnIncludeResultReleaseFn releaseCallbackHandle = default;
        try {
            resolveCallbackHandle = PfnIncludeResolveFn.From(resolveDelegate);
            releaseCallbackHandle = PfnIncludeResultReleaseFn.From(releaseDelegate);
        } catch {
            resolveCallbackHandle.Dispose();
            releaseCallbackHandle.Dispose();
            EmergencyAllocation.Free();
            throw;
        }
        ResolveDelegate = resolveDelegate;
        ReleaseDelegate = releaseDelegate;
        ResolveCallbackHandle = resolveCallbackHandle;
        ReleaseCallbackHandle = releaseCallbackHandle;
    }

    /// <summary>Gets all active include dependencies in the order shaderc requested them.</summary>
    public IReadOnlyList<Ps3ShaderSourceDependency> Dependencies {
        get {
            lock (Gate) return DependenciesList.ToArray();
        }
    }

    /// <summary>Gets a human-readable first fatal failure after native compilation has returned.</summary>
    public string FatalFailureMessage {
        get {
            lock (Gate) {
                if (FatalConsistencyFailure) {
                    return $"Include resolver returned different contents for '{FatalFailurePath}' in one compilation.";
                }
                if (FatalFailureValue == null) return null;
                if (FatalFailurePath == null) return $"Shaderc include callback failed: {FatalFailureValue.Message}";
                return $"Unable to resolve include '{FatalFailurePath}' requested by '{FatalFailureRequestingFile}': {FatalFailureValue.Message}";
            }
        }
    }

    /// <summary>Gets the rooted shaderc resolve callback handle.</summary>
    public PfnIncludeResolveFn ResolveCallback => ResolveCallbackHandle;

    /// <summary>Gets the rooted shaderc release callback handle.</summary>
    public PfnIncludeResultReleaseFn ReleaseCallback => ReleaseCallbackHandle;

    /// <summary>Releases any native include allocations shaderc did not release before compilation ended.</summary>
    public void Dispose() {
        lock (Gate) {
            if (IsDisposed) return;
            IsDisposed = true;
            foreach (Ps3ShaderIncludeAllocation allocation in Allocations.Values) allocation.Free();
            Allocations.Clear();
            EmergencyAllocation.Free();
        }
        ResolveCallbackHandle.Dispose();
        ReleaseCallbackHandle.Dispose();
    }

    /// <summary>Resolves one shaderc callback and converts all managed failures into error include results.</summary>
    /// <param name="userData">Opaque shaderc callback data, unused because the delegate captures this session.</param>
    /// <param name="requestedSource">Include path written by the shader author.</param>
    /// <param name="type">Shaderc include mode value.</param>
    /// <param name="requestingSource">Source path that contains the include directive.</param>
    /// <param name="includeDepth">Depth of this include in the active include chain.</param>
    /// <returns>A shaderc-owned result allocation that must be released through <see cref="ReleaseInclude"/>.</returns>
    unsafe IncludeResult* ResolveInclude(void* userData, byte* requestedSource, int type, byte* requestingSource, nuint includeDepth) {
        string includePath = string.Empty;
        string requestingFile = string.Empty;
        try {
            try {
                includePath = DecodeUtf8(requestedSource);
                requestingFile = DecodeUtf8(requestingSource);
                if (IsDisposed) throw new ObjectDisposedException(nameof(Ps3ShaderIncludeSession));
                ShaderIncludeResult resolved = IncludeResolver.Resolve(requestingFile, includePath);
                if (!RecordDependency(resolved, requestingFile)) return (IncludeResult*)EmergencyAllocation.Result;
                return AllocateResult(resolved.Path, resolved.Source, false);
            } catch (Exception error) {
                return CreateErrorResultSafely(error, includePath, requestingFile, type);
            }
        } catch (Exception unexpectedFailure) {
            try {
                RecordFatalFailure(unexpectedFailure, includePath, requestingFile);
            } catch {
            }
            return (IncludeResult*)EmergencyAllocation.Result;
        }
    }

    /// <summary>Builds a detailed native error response, falling back to the preallocated result on any managed failure.</summary>
    /// <param name="failure">The resolver, consistency, or normal-result allocation failure.</param>
    /// <param name="includePath">Original include path decoded from shaderc.</param>
    /// <param name="requestingFile">Source path decoded from shaderc.</param>
    /// <param name="includeType">Shaderc include mode value.</param>
    /// <returns>An allocated detailed error result, or the session's emergency result.</returns>
    unsafe IncludeResult* CreateErrorResultSafely(Exception failure, string includePath, string requestingFile, int includeType) {
        bool relativeLookupMiss = includeType == (int)IncludeType.Relative && failure is FileNotFoundException;
        if (!relativeLookupMiss) RecordFatalFailure(failure, includePath, requestingFile);
        try {
            string diagnostic = $"Unable to resolve include '{includePath}' requested by '{requestingFile}': {failure.Message}";
            return AllocateResult(includePath, diagnostic, true);
        } catch (Exception errorResultFailure) {
            try {
                RecordFatalFailure(errorResultFailure, includePath, requestingFile);
            } catch {
            }
            return (IncludeResult*)EmergencyAllocation.Result;
        }
    }

    /// <summary>Releases one callback result exactly once; unknown or repeated pointers are ignored.</summary>
    /// <param name="userData">Opaque shaderc callback data, unused because the delegate captures this session.</param>
    /// <param name="includeResult">Native result pointer previously returned by this session.</param>
    unsafe void ReleaseInclude(void* userData, IncludeResult* includeResult) {
        try {
            if (includeResult == null || (IntPtr)includeResult == EmergencyAllocation.Result) return;
            lock (Gate) {
                if (!Allocations.TryGetValue((IntPtr)includeResult, out Ps3ShaderIncludeAllocation allocation)) return;
                allocation.Free();
                Allocations.Remove((IntPtr)includeResult);
            }
        } catch (Exception error) {
            try {
                RecordFatalFailure(error, null, null);
            } catch {
            }
        }
    }

    /// <summary>Records the active include and rejects a resolver that changes content for one path mid-compile.</summary>
    /// <param name="resolved">Resolved path and source returned by the compiler include resolver.</param>
    bool RecordDependency(ShaderIncludeResult resolved, string requestingFile) {
        lock (Gate) {
            if (ResolvedContents.TryGetValue(resolved.Path, out string priorContent) && !string.Equals(priorContent, resolved.Source, StringComparison.Ordinal)) {
                RecordFatalConsistencyFailure(resolved.Path, requestingFile);
                return false;
            }
            string contentHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(resolved.Source)));
            ResolvedContents[resolved.Path] = resolved.Source;
            DependenciesList.Add(new Ps3ShaderSourceDependency(resolved.Path, contentHash));
            return true;
        }
    }

    /// <summary>Retains a caught exception and its already-decoded paths without formatting or allocating a wrapper.</summary>
    /// <param name="failure">Fatal exception captured inside a native callback.</param>
    /// <param name="includePath">Original include path, when decoding succeeded.</param>
    /// <param name="requestingFile">Requesting source path, when decoding succeeded.</param>
    void RecordFatalFailure(Exception failure, string includePath, string requestingFile) {
        lock (Gate) {
            if (FatalFailureValue != null || FatalConsistencyFailure) return;
            FatalFailureValue = failure;
            FatalFailurePath = includePath;
            FatalFailureRequestingFile = requestingFile;
        }
    }

    /// <summary>Records an inconsistent resolved path without constructing a diagnostic inside the native callback.</summary>
    /// <param name="includePath">Resolver-provided path with changing contents.</param>
    /// <param name="requestingFile">Source path that caused the conflicting resolution.</param>
    void RecordFatalConsistencyFailure(string includePath, string requestingFile) {
        if (FatalFailureValue != null || FatalConsistencyFailure) return;
        FatalConsistencyFailure = true;
        FatalFailurePath = includePath;
        FatalFailureRequestingFile = requestingFile;
    }

    /// <summary>Creates a null-terminated UTF-8 result and tracks every native allocation until release.</summary>
    /// <param name="sourceName">Resolved include path, or the failed authored path.</param>
    /// <param name="content">Resolved include contents or error diagnostic.</param>
    /// <param name="isError">Whether shaderc should treat the result as a failed include.</param>
    /// <returns>The address of the allocated shaderc include result.</returns>
    unsafe IncludeResult* AllocateResult(string sourceName, string content, bool isError) {
        Ps3ShaderIncludeAllocation allocation = CreateAllocation(sourceName, content, isError);
        try {
            lock (Gate) {
                if (IsDisposed) throw new ObjectDisposedException(nameof(Ps3ShaderIncludeSession));
                Allocations.Add(allocation.Result, allocation);
            }
            return (IncludeResult*)allocation.Result;
        } catch {
            allocation.Free();
            throw;
        }
    }

    /// <summary>Creates native result storage using the configured allocator and exact UTF-8 byte lengths.</summary>
    /// <param name="sourceName">Resolved include path or error source name.</param>
    /// <param name="content">Resolved include source or an error diagnostic.</param>
    /// <param name="isError">Whether shaderc should treat the result as an include failure.</param>
    /// <returns>An owned result structure and its UTF-8 buffers.</returns>
    unsafe Ps3ShaderIncludeAllocation CreateAllocation(string sourceName, string content, bool isError) {
        byte[] sourceNameBytes = Encoding.UTF8.GetBytes(sourceName);
        byte[] contentBytes = Encoding.UTF8.GetBytes(content);
        Ps3ShaderIncludeAllocation allocation = new();
        try {
            allocation.SourceName = AllocateUtf8(sourceNameBytes);
            allocation.Content = AllocateUtf8(contentBytes);
            allocation.Result = AllocateNative(sizeof(IncludeResult));
            IncludeResult result = new() {
                SourceName = (byte*)allocation.SourceName,
                SourceNameLength = isError ? 0u : (nuint)sourceNameBytes.Length,
                Content = (byte*)allocation.Content,
                ContentLength = (nuint)contentBytes.Length,
                UserData = null
            };
            Marshal.StructureToPtr(result, allocation.Result, false);
            return allocation;
        } catch {
            allocation.Free();
            throw;
        }
    }

    /// <summary>Allocates a positive-sized unmanaged block or raises a managed allocation error.</summary>
    /// <param name="size">Requested native storage size.</param>
    /// <returns>Nonzero unmanaged storage.</returns>
    IntPtr AllocateNative(int size) {
        IntPtr address = NativeAllocator(size);
        return address != IntPtr.Zero ? address : throw new OutOfMemoryException("Shaderc include native allocation failed.");
    }

    /// <summary>Allocates UTF-8 bytes plus the trailing NUL required by shaderc native string consumers.</summary>
    /// <param name="utf8Bytes">Encoded string bytes excluding the terminator.</param>
    /// <returns>Unmanaged storage for the bytes and trailing NUL.</returns>
    IntPtr AllocateUtf8(byte[] utf8Bytes) {
        IntPtr buffer = AllocateNative(checked(utf8Bytes.Length + 1));
        try {
            Marshal.Copy(utf8Bytes, 0, buffer, utf8Bytes.Length);
            Marshal.WriteByte(buffer, utf8Bytes.Length, 0);
            return buffer;
        } catch {
            Marshal.FreeHGlobal(buffer);
            throw;
        }
    }

    /// <summary>Decodes a shaderc UTF-8, NUL-terminated callback string.</summary>
    /// <param name="value">Native string pointer.</param>
    /// <returns>The managed string, or an empty string for a null pointer.</returns>
    static unsafe string DecodeUtf8(byte* value) => value == null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)value) ?? string.Empty;

}
