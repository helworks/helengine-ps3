#pragma once

namespace helengine::ps3 {

/// <summary>Clears a resource owner's active callback flag on every scope exit, including exception unwinding.</summary>
class Ps3Batch2DFlushGuard final {
private:
    /// <summary>Mutation-in-progress state protected for one flush callback invocation.</summary>
    bool& ActiveFlag;

public:
    /// <summary>Marks the callback active until this guard leaves scope.</summary>
    /// <param name="activeFlag">Owner flag used to reject nested resource mutations.</param>
    explicit Ps3Batch2DFlushGuard(bool& activeFlag) : ActiveFlag(activeFlag) { ActiveFlag = true; }

    /// <summary>Restores the owner's mutation state without throwing.</summary>
    ~Ps3Batch2DFlushGuard() { ActiveFlag = false; }

    /// <summary>Prevents duplicate reset ownership for the same active flag.</summary>
    Ps3Batch2DFlushGuard(const Ps3Batch2DFlushGuard&) = delete;

    /// <summary>Prevents transfer of the guard's reset responsibility.</summary>
    Ps3Batch2DFlushGuard& operator=(const Ps3Batch2DFlushGuard&) = delete;
};

} // namespace helengine::ps3
