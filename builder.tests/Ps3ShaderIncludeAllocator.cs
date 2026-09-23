using System.Runtime.InteropServices;

namespace helengine.ps3.builder.tests;

/// <summary>Provides real unmanaged blocks until a configured allocation count, then simulates allocation failure.</summary>
public sealed class Ps3ShaderIncludeAllocator {
    /// <summary>Maximum number of unmanaged allocations that this instance will grant.</summary>
    readonly int SuccessfulAllocationLimit;

    /// <summary>Number of allocation requests made through this instance.</summary>
    int RequestCount;

    /// <summary>Initializes deterministic unmanaged allocation failure at a specified request count.</summary>
    /// <param name="successfulAllocationLimit">Number of initial native allocations to satisfy.</param>
    public Ps3ShaderIncludeAllocator(int successfulAllocationLimit) {
        if (successfulAllocationLimit < 0) throw new ArgumentOutOfRangeException(nameof(successfulAllocationLimit));
        SuccessfulAllocationLimit = successfulAllocationLimit;
    }

    /// <summary>Gets the number of allocation requests received.</summary>
    public int AllocationCount => RequestCount;

    /// <summary>Allocates native memory until the configured limit, then returns the native failure sentinel.</summary>
    /// <param name="size">Requested allocation size in bytes.</param>
    /// <returns>Unmanaged memory or zero to simulate allocation failure.</returns>
    public IntPtr Allocate(int size) {
        RequestCount++;
        return RequestCount <= SuccessfulAllocationLimit ? Marshal.AllocHGlobal(size) : IntPtr.Zero;
    }
}
