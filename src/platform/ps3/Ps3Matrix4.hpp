#pragma once

namespace helengine::ps3 {
    /// A 4x4 float matrix in mathematical row-major storage: Values[row * 4 + column].
    /// Matrices multiply column vectors (result = M * v), so composing transforms reads
    /// right to left, e.g. Projection * View * Model. This convention is local to the
    /// diagnostic cube host and is not a Helengine engine contract.
    struct Ps3Matrix4 {
        /// Sixteen elements stored row by row.
        float Values[16];
    };
}
