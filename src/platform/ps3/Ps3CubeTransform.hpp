#pragma once

#include "platform/ps3/Ps3Matrix4.hpp"

namespace helengine::ps3 {
    /// Minimal diagnostic model/view/projection calculation for the cube host, using scalar
    /// float arithmetic and fixed arrays only. Conventions (local to this host):
    /// - Row-major matrices multiplying column vectors (see Ps3Matrix4).
    /// - Model = RotationY(radians) * RotationX(0.4), so the rotation about Y happens first
    ///   and a fixed tilt then exposes the top face.
    /// - Camera at (0, 0, 5) looking down -Z with +Y up, i.e. View = Translation(0, 0, -5).
    /// - Right-handed perspective with near 0.1, far 100 and a vertical field of view of
    ///   pi/3; clip-space depth spans -w..w and the RSX viewport maps it into 0..1.
    class Ps3CubeTransform {
    public:
        /// Returns Projection * View * Model for the given rotation about Y and display aspect
        /// (width / height). Throws std::invalid_argument for non-finite radians or aspect, or
        /// for a nonpositive aspect, so the native boot host can report the failure and stop.
        static Ps3Matrix4 Build(float radians, float aspect);

        /// Computes output = matrix * input for a homogeneous column vector:
        /// output[r] = sum(matrix[r*4+k] * input[k], k = 0..3).
        static void Transform(const Ps3Matrix4& matrix, const float input[4], float output[4]);

        /// Computes the matrix product A * B:
        /// result[r*4+c] = sum(A[r*4+k] * B[k*4+c], k = 0..3).
        static Ps3Matrix4 Multiply(const Ps3Matrix4& a, const Ps3Matrix4& b);

    private:
        /// Fixed camera tilt about X applied after the animated rotation.
        static constexpr float TiltRadians = 0.4f;

        /// Distance from the camera to the cube center along +Z.
        static constexpr float CameraDistance = 5.0f;

        /// Near clipping distance.
        static constexpr float NearPlane = 0.1f;

        /// Far clipping distance.
        static constexpr float FarPlane = 100.0f;

        /// Vertical field of view in radians (pi/3).
        static constexpr float VerticalFieldOfView = 3.14159265358979323846f / 3.0f;

        /// Returns the identity matrix.
        static Ps3Matrix4 Identity();

        /// Returns the conventional right-handed rotation about X by the given angle.
        static Ps3Matrix4 RotationX(float radians);

        /// Returns the conventional right-handed rotation about Y by the given angle.
        static Ps3Matrix4 RotationY(float radians);

        /// Returns a translation by (x, y, z).
        static Ps3Matrix4 Translation(float x, float y, float z);

        /// Returns the OpenGL-style perspective matrix documented in the plan:
        /// [ f/aspect 0 0 0; 0 f 0 0; 0 0 -(far+near)/(far-near) -2*far*near/(far-near); 0 0 -1 0 ].
        static Ps3Matrix4 Perspective(float aspect);
    };
}
