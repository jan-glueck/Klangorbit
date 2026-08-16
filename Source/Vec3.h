#pragma once
#include <cmath>

/**
    Simple 3D vector (x, y, z). Deliberately replaces juce::Vector3D with a
    custom type: juce::Vector3D lives in the juce_opengl module, which would
    be an unnecessary OpenGL dependency for a plain position/velocity
    metadata type with no graphics involvement at all.
*/
struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+ (Vec3 o) const noexcept { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator- (Vec3 o) const noexcept { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator* (float s) const noexcept { return { x * s, y * s, z * s }; }
    Vec3 operator/ (float s) const noexcept { return { x / s, y / s, z / s }; }
    Vec3 operator-() const noexcept { return { -x, -y, -z }; }

    Vec3& operator+= (Vec3 o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-= (Vec3 o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*= (float s) noexcept { x *= s; y *= s; z *= s; return *this; }

    float length() const noexcept { return std::sqrt (x * x + y * y + z * z); }
    float dot (Vec3 o) const noexcept { return x * o.x + y * o.y + z * o.z; }
};

inline Vec3 cross (Vec3 a, Vec3 b) noexcept
{
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}
