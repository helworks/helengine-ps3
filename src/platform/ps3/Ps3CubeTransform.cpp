#include "platform/ps3/Ps3CubeTransform.hpp"

#include <cmath>
#include <stdexcept>

namespace helengine::ps3 {
    Ps3Matrix4 Ps3CubeTransform::Build(float radians, float aspect) {
        if (!std::isfinite(radians)) {
            throw std::invalid_argument("Ps3CubeTransform::Build requires finite radians");
        }
        if (!std::isfinite(aspect) || !(aspect > 0.0f)) {
            throw std::invalid_argument("Ps3CubeTransform::Build requires a finite, positive aspect ratio");
        }
        const Ps3Matrix4 model = Multiply(RotationY(radians), RotationX(TiltRadians));
        const Ps3Matrix4 view = Translation(0.0f, 0.0f, -CameraDistance);
        const Ps3Matrix4 projection = Perspective(aspect);
        return Multiply(projection, Multiply(view, model));
    }

    void Ps3CubeTransform::Transform(const Ps3Matrix4& matrix, const float input[4], float output[4]) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += matrix.Values[row * 4 + k] * input[k];
            }
            output[row] = sum;
        }
    }

    Ps3Matrix4 Ps3CubeTransform::Multiply(const Ps3Matrix4& a, const Ps3Matrix4& b) {
        Ps3Matrix4 result = {};
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.Values[row * 4 + k] * b.Values[k * 4 + column];
                }
                result.Values[row * 4 + column] = sum;
            }
        }
        return result;
    }

    Ps3Matrix4 Ps3CubeTransform::Identity() {
        Ps3Matrix4 result = {};
        result.Values[0] = 1.0f;
        result.Values[5] = 1.0f;
        result.Values[10] = 1.0f;
        result.Values[15] = 1.0f;
        return result;
    }

    Ps3Matrix4 Ps3CubeTransform::RotationX(float radians) {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Ps3Matrix4 result = Identity();
        result.Values[1 * 4 + 1] = c;
        result.Values[1 * 4 + 2] = -s;
        result.Values[2 * 4 + 1] = s;
        result.Values[2 * 4 + 2] = c;
        return result;
    }

    Ps3Matrix4 Ps3CubeTransform::RotationY(float radians) {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Ps3Matrix4 result = Identity();
        result.Values[0 * 4 + 0] = c;
        result.Values[0 * 4 + 2] = s;
        result.Values[2 * 4 + 0] = -s;
        result.Values[2 * 4 + 2] = c;
        return result;
    }

    Ps3Matrix4 Ps3CubeTransform::Translation(float x, float y, float z) {
        Ps3Matrix4 result = Identity();
        result.Values[0 * 4 + 3] = x;
        result.Values[1 * 4 + 3] = y;
        result.Values[2 * 4 + 3] = z;
        return result;
    }

    Ps3Matrix4 Ps3CubeTransform::Perspective(float aspect) {
        const float f = 1.0f / std::tan(VerticalFieldOfView * 0.5f);
        Ps3Matrix4 result = {};
        result.Values[0 * 4 + 0] = f / aspect;
        result.Values[1 * 4 + 1] = f;
        result.Values[2 * 4 + 2] = -(FarPlane + NearPlane) / (FarPlane - NearPlane);
        result.Values[2 * 4 + 3] = -2.0f * FarPlane * NearPlane / (FarPlane - NearPlane);
        result.Values[3 * 4 + 2] = -1.0f;
        return result;
    }
}
