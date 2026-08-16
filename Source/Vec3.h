#pragma once
#include <cmath>

/**
    Einfacher 3D-Vektor (x,y,z). Ersetzt juce::Vector3D bewusst durch einen
    eigenen Typ: juce::Vector3D steckt im juce_opengl-Modul, das fuer einen
    reinen Positions-/Geschwindigkeits-Metadatentyp ohne jeden Grafikbezug
    eine unnoetige OpenGL-Abhaengigkeit waere.
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
};
