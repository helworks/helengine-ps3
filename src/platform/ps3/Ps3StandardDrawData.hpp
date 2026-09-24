#pragma once

#include "Ps3Matrix4.hpp"

namespace helengine::ps3 {

/// <summary>Contains explicit CPU inputs needed to resolve one ForwardStandard draw.</summary>
struct Ps3StandardDrawData {
    /// <summary>Gets the SDK-packed world matrix as four dot-product vectors.</summary>
    Ps3Matrix4 World;

    /// <summary>Gets the SDK-packed world-view-projection matrix as four dot-product vectors.</summary>
    Ps3Matrix4 WorldViewProjection;

    /// <summary>Gets the SDK-packed normal transform as four dot-product vectors.</summary>
    Ps3Matrix4 NormalMatrix;

    /// <summary>Gets the caller-supplied camera world position as an explicit float4.</summary>
    float CameraPosition[4];

    /// <summary>Gets the normalized material base color as an explicit RGBA float4.</summary>
    float BaseColor[4];

    /// <summary>Gets the scene ambient radiance as an explicit float4 for lit draws.</summary>
    float AmbientLight[4];

    /// <summary>Gets the supplied directional-light RGB and unused fourth component.</summary>
    float DirectionalColor[4];

    /// <summary>Gets the supplied directional-light XYZ and unused fourth component.</summary>
    float DirectionalDirection[4];

    /// <summary>Indicates whether this draw uses the explicit lit or unlit shader policy.</summary>
    bool Lit;

    /// <summary>Indicates whether one directional light is present for a lit draw.</summary>
    bool HasDirectionalLight;

    /// <summary>Retains raster-state sidedness without introducing a shader uniform for it.</summary>
    bool DoubleSided;
};

} // namespace helengine::ps3
