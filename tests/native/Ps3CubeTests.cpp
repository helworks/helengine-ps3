// Dependency-free host tests for the SDK-independent cube geometry and transform contract.
// Every check is an active runtime comparison (no reliance on assert), so the tests are
// valid in any build configuration. Expected behavior is stated explicitly; nothing
// compares the implementation to itself.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "platform/ps3/Ps3CubeMesh.hpp"
#include "platform/ps3/Ps3CubeTransform.hpp"
#include "platform/ps3/Ps3Matrix4.hpp"
#include "platform/ps3/Ps3Vertex.hpp"

using helengine::ps3::Ps3CubeMesh;
using helengine::ps3::Ps3CubeTransform;
using helengine::ps3::Ps3Matrix4;
using helengine::ps3::Ps3Vertex;

namespace {
    /// Counts failed checks so the process exit code reflects every failure, not only the first.
    int FailureCount = 0;

    /// Records one failed expectation with a human-readable reason.
    void Fail(const char* testName, const char* reason) {
        std::fprintf(stderr, "FAIL %s: %s\n", testName, reason);
        ++FailureCount;
    }

    /// Reports a passed test by name.
    void Pass(const char* testName) {
        std::printf("PASS %s\n", testName);
    }

    /// Returns true when two floats agree within an absolute tolerance.
    bool Near(float actual, float expected, float tolerance) {
        return std::fabs(actual - expected) <= tolerance;
    }

    /// Checks that the vertex layout matches the 28-byte contract consumed by the RSX attribute bindings.
    void VertexLayoutTest() {
        static_assert(sizeof(Ps3Vertex) == 28, "Ps3Vertex must be 28 bytes");
        static_assert(offsetof(Ps3Vertex, Color) == 12, "Ps3Vertex color must start at byte 12");
        static_assert(offsetof(Ps3Vertex, Position) == 0, "Ps3Vertex position must start at byte 0");
        if (Ps3CubeMesh::VertexCount != 24 || Ps3CubeMesh::IndexCount != 36) {
            Fail("VertexLayout", "cube must expose 24 vertices and 36 indices");
            return;
        }
        Pass("VertexLayout");
    }

    /// Checks index bounds, one constant color per face and outward (counter-clockwise from outside) winding.
    void GeometryWindingTest() {
        const Ps3Vertex* vertices = Ps3CubeMesh::Vertices();
        const std::uint16_t* indices = Ps3CubeMesh::Indices();
        for (int index = 0; index < Ps3CubeMesh::IndexCount; ++index) {
            if (indices[index] >= Ps3CubeMesh::VertexCount) {
                Fail("GeometryWinding", "index out of range");
                return;
            }
        }
        for (int face = 0; face < 6; ++face) {
            const Ps3Vertex& first = vertices[face * 4];
            for (int corner = 1; corner < 4; ++corner) {
                const Ps3Vertex& other = vertices[face * 4 + corner];
                for (int channel = 0; channel < 4; ++channel) {
                    if (other.Color[channel] != first.Color[channel]) {
                        Fail("GeometryWinding", "face corners must share one color");
                        return;
                    }
                }
            }
            if (first.Color[3] != 1.0f) {
                Fail("GeometryWinding", "face alpha must be 1");
                return;
            }
            for (int otherFace = 0; otherFace < face; ++otherFace) {
                const Ps3Vertex& otherFirst = vertices[otherFace * 4];
                if (otherFirst.Color[0] == first.Color[0] && otherFirst.Color[1] == first.Color[1] && otherFirst.Color[2] == first.Color[2]) {
                    Fail("GeometryWinding", "faces must have distinct colors");
                    return;
                }
            }
        }
        for (int triangle = 0; triangle < Ps3CubeMesh::IndexCount / 3; ++triangle) {
            const float* a = vertices[indices[triangle * 3 + 0]].Position;
            const float* b = vertices[indices[triangle * 3 + 1]].Position;
            const float* c = vertices[indices[triangle * 3 + 2]].Position;
            const float edgeAb[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const float edgeAc[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            const float normal[3] = {
                edgeAb[1] * edgeAc[2] - edgeAb[2] * edgeAc[1],
                edgeAb[2] * edgeAc[0] - edgeAb[0] * edgeAc[2],
                edgeAb[0] * edgeAc[1] - edgeAb[1] * edgeAc[0]
            };
            const float centroid[3] = { a[0] + b[0] + c[0], a[1] + b[1] + c[1], a[2] + b[2] + c[2] };
            const float outward = normal[0] * centroid[0] + normal[1] * centroid[1] + normal[2] * centroid[2];
            if (!(outward > 0.0f)) {
                Fail("GeometryWinding", "triangle winding must face outward");
                return;
            }
            const float area2 = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            if (!Near(area2, 4.0f, 1e-5f)) {
                Fail("GeometryWinding", "each triangle must cover half of a 2x2 face");
                return;
            }
        }
        for (int vertex = 0; vertex < Ps3CubeMesh::VertexCount; ++vertex) {
            for (int axis = 0; axis < 3; ++axis) {
                if (std::fabs(vertices[vertex].Position[axis]) != 1.0f) {
                    Fail("GeometryWinding", "cube corners must lie at +/-1");
                    return;
                }
            }
        }
        Pass("GeometryWinding");
    }

    /// Checks that multiplying by the identity and transforming with the identity leave values unchanged.
    void IdentityTest() {
        Ps3Matrix4 identity = {};
        for (int diagonal = 0; diagonal < 4; ++diagonal) {
            identity.Values[diagonal * 4 + diagonal] = 1.0f;
        }
        Ps3Matrix4 sample = {};
        for (int element = 0; element < 16; ++element) {
            sample.Values[element] = static_cast<float>(element + 1) * 0.5f;
        }
        const Ps3Matrix4 leftProduct = Ps3CubeTransform::Multiply(identity, sample);
        const Ps3Matrix4 rightProduct = Ps3CubeTransform::Multiply(sample, identity);
        for (int element = 0; element < 16; ++element) {
            if (leftProduct.Values[element] != sample.Values[element] || rightProduct.Values[element] != sample.Values[element]) {
                Fail("Identity", "identity multiplication must preserve the matrix");
                return;
            }
        }
        const float input[4] = { 1.5f, -2.0f, 3.25f, 1.0f };
        float output[4] = {};
        Ps3CubeTransform::Transform(identity, input, output);
        for (int component = 0; component < 4; ++component) {
            if (output[component] != input[component]) {
                Fail("Identity", "identity transform must preserve the vector");
                return;
            }
        }
        // Row-major, column-vector convention: row r of A dotted with v.
        Ps3Matrix4 rows = {};
        rows.Values[0 * 4 + 3] = 2.0f; // x' = 2*w
        rows.Values[1 * 4 + 0] = 1.0f; // y' = x
        rows.Values[2 * 4 + 1] = 1.0f; // z' = y
        rows.Values[3 * 4 + 2] = 1.0f; // w' = z
        Ps3CubeTransform::Transform(rows, input, output);
        if (output[0] != 2.0f || output[1] != 1.5f || output[2] != -2.0f || output[3] != 3.25f) {
            Fail("Identity", "transform must use mathematical rows against a column vector");
            return;
        }
        // Multiply order: (A*B)*v == A*(B*v).
        const Ps3Matrix4 product = Ps3CubeTransform::Multiply(rows, sample);
        float viaProduct[4] = {};
        float viaSample[4] = {};
        float viaBoth[4] = {};
        Ps3CubeTransform::Transform(product, input, viaProduct);
        Ps3CubeTransform::Transform(sample, input, viaSample);
        Ps3CubeTransform::Transform(rows, viaSample, viaBoth);
        for (int component = 0; component < 4; ++component) {
            if (!Near(viaProduct[component], viaBoth[component], 1e-3f)) {
                Fail("Identity", "Multiply(A,B) must equal composition A after B");
                return;
            }
        }
        Pass("Identity");
    }

    /// Checks that the cube center projects to the screen center, in front of the camera and inside the depth range.
    void CenterProjectionTest() {
        const auto m = Ps3CubeTransform::Build(0.0f, 16.0f / 9.0f);
        const float origin[4] = { 0, 0, 0, 1 };
        float clip[4] = {};
        Ps3CubeTransform::Transform(m, origin, clip);
        if (std::fabs(clip[0]) > 1e-5f || std::fabs(clip[1]) > 1e-5f ||
            clip[3] <= 0 || clip[2] < -clip[3] || clip[2] > clip[3]) {
            Fail("CenterProjection", "cube center projection failed");
            return;
        }
        if (!Near(clip[3], 5.0f, 1e-4f)) {
            Fail("CenterProjection", "clip w must equal the camera distance of 5");
            return;
        }
        Pass("CenterProjection");
    }

    /// Checks that a point with greater model-space X lands further right in normalized device coordinates.
    void PositiveXTest() {
        const auto m = Ps3CubeTransform::Build(0.0f, 16.0f / 9.0f);
        const float origin[4] = { 0, 0, 0, 1 };
        const float right[4] = { 1, 0, 0, 1 };
        float originClip[4] = {};
        float rightClip[4] = {};
        Ps3CubeTransform::Transform(m, origin, originClip);
        Ps3CubeTransform::Transform(m, right, rightClip);
        const float originNdcX = originClip[0] / originClip[3];
        const float rightNdcX = rightClip[0] / rightClip[3];
        if (!(rightNdcX > originNdcX) || !(rightNdcX > 0.0f)) {
            Fail("PositiveX", "greater model X must project further right");
            return;
        }
        // Rotation about X does not move the X axis, so the expected value is f/aspect * 1 / 5.
        const float f = 1.0f / std::tan((3.14159265358979323846f / 3.0f) * 0.5f);
        if (!Near(rightNdcX, (f / (16.0f / 9.0f)) / 5.0f, 1e-4f)) {
            Fail("PositiveX", "projected X must match f/aspect over camera distance");
            return;
        }
        Pass("PositiveX");
    }

    /// Checks that all 24 cube vertices remain finite and in front of the camera at 0, pi/2 and pi radians.
    void FiniteVerticesTest() {
        const float poses[3] = { 0.0f, 3.14159265358979323846f * 0.5f, 3.14159265358979323846f };
        const Ps3Vertex* vertices = Ps3CubeMesh::Vertices();
        for (float pose : poses) {
            const auto m = Ps3CubeTransform::Build(pose, 16.0f / 9.0f);
            for (int vertex = 0; vertex < Ps3CubeMesh::VertexCount; ++vertex) {
                const float input[4] = { vertices[vertex].Position[0], vertices[vertex].Position[1], vertices[vertex].Position[2], 1.0f };
                float clip[4] = {};
                Ps3CubeTransform::Transform(m, input, clip);
                for (int component = 0; component < 4; ++component) {
                    if (!std::isfinite(clip[component])) {
                        Fail("FiniteVertices", "clip coordinate must be finite");
                        return;
                    }
                }
                if (!(clip[3] > 0.0f) || clip[2] < -clip[3] || clip[2] > clip[3]) {
                    Fail("FiniteVertices", "every cube vertex must be in front of the camera and inside the depth range");
                    return;
                }
                if (std::fabs(clip[0]) > clip[3] || std::fabs(clip[1]) > clip[3]) {
                    Fail("FiniteVertices", "every cube vertex must be inside the view frustum");
                    return;
                }
            }
        }
        Pass("FiniteVertices");
    }

    /// Checks that a nearer point receives a smaller normalized depth than a farther point.
    void DepthOrderTest() {
        const auto m = Ps3CubeTransform::Build(0.0f, 16.0f / 9.0f);
        const float near[4] = { 0, 0, 1, 1 };
        const float far[4] = { 0, 0, -1, 1 };
        float nearClip[4] = {};
        float farClip[4] = {};
        Ps3CubeTransform::Transform(m, near, nearClip);
        Ps3CubeTransform::Transform(m, far, farClip);
        if (!(nearClip[3] < farClip[3])) {
            Fail("DepthOrder", "model +Z must be nearer to the camera at +5 than model -Z");
            return;
        }
        if (!(nearClip[2] / nearClip[3] < farClip[2] / farClip[3])) {
            Fail("DepthOrder", "nearer points must have smaller normalized depth");
            return;
        }
        Pass("DepthOrder");
    }

    /// Checks that a wider aspect ratio shrinks horizontal projection while vertical projection is unchanged.
    void AspectScaleTest() {
        const auto square = Ps3CubeTransform::Build(0.0f, 1.0f);
        const auto wide = Ps3CubeTransform::Build(0.0f, 2.0f);
        const float point[4] = { 1, 1, 0, 1 };
        float squareClip[4] = {};
        float wideClip[4] = {};
        Ps3CubeTransform::Transform(square, point, squareClip);
        Ps3CubeTransform::Transform(wide, point, wideClip);
        if (!Near(wideClip[0] * 2.0f, squareClip[0], 1e-5f)) {
            Fail("AspectScale", "doubling the aspect must halve the horizontal clip coordinate");
            return;
        }
        if (!Near(wideClip[1], squareClip[1], 1e-5f) || !Near(wideClip[3], squareClip[3], 1e-5f)) {
            Fail("AspectScale", "aspect must not change vertical or w coordinates");
            return;
        }
        Pass("AspectScale");
    }

    /// Checks that rotating by pi mirrors the X axis relative to the unrotated pose.
    void RotationTest() {
        const auto zero = Ps3CubeTransform::Build(0.0f, 1.0f);
        const auto half = Ps3CubeTransform::Build(3.14159265358979323846f, 1.0f);
        const float point[4] = { 1, 0, 0, 1 };
        float zeroClip[4] = {};
        float halfClip[4] = {};
        Ps3CubeTransform::Transform(zero, point, zeroClip);
        Ps3CubeTransform::Transform(half, point, halfClip);
        if (!Near(halfClip[0], -zeroClip[0], 1e-4f) || !Near(halfClip[3], zeroClip[3], 1e-4f)) {
            Fail("Rotation", "rotating by pi about Y must mirror model X");
            return;
        }
        // Rotation is applied before the fixed tilt, so the tilt keeps +Y above the center at every pose.
        const float top[4] = { 0, 1, 0, 1 };
        float topClip[4] = {};
        Ps3CubeTransform::Transform(half, top, topClip);
        if (!(topClip[1] > 0.0f)) {
            Fail("Rotation", "model +Y must stay above the screen center");
            return;
        }
        Pass("Rotation");
    }

    /// Checks that invalid aspect ratios and non-finite inputs are rejected with std::invalid_argument.
    void InvalidInputTest() {
        const float badAspects[3] = { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN() };
        for (float aspect : badAspects) {
            bool rejected = false;
            try {
                (void)Ps3CubeTransform::Build(0.0f, aspect);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            if (!rejected) {
                Fail("InvalidInput", "nonpositive or non-finite aspect must be rejected");
                return;
            }
        }
        bool rejectedRadians = false;
        try {
            (void)Ps3CubeTransform::Build(std::numeric_limits<float>::infinity(), 1.0f);
        } catch (const std::invalid_argument&) {
            rejectedRadians = true;
        }
        if (!rejectedRadians) {
            Fail("InvalidInput", "non-finite radians must be rejected");
            return;
        }
        Pass("InvalidInput");
    }
}

/// Runs every geometry and transform check and returns nonzero when any expectation failed.
int main() {
    VertexLayoutTest();
    GeometryWindingTest();
    IdentityTest();
    CenterProjectionTest();
    PositiveXTest();
    FiniteVerticesTest();
    DepthOrderTest();
    AspectScaleTest();
    RotationTest();
    InvalidInputTest();
    if (FailureCount != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", FailureCount);
        return 1;
    }
    std::printf("All cube geometry and transform checks passed\n");
    return 0;
}
