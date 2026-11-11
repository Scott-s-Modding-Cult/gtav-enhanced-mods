#pragma once
// Small float vector + entity helpers. The SDK's Vector3 has padding between its fields,
// so maths is done on V3 and converted at the native boundary.
#include <cmath>

#include "natives.h"

struct V3 { float x = 0, y = 0, z = 0; };

inline V3 From(const Vector3& v) { return {v.x, v.y, v.z}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline float Len2D(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline V3 Flat(V3 a) { return {a.x, a.y, 0.0f}; }
inline V3 Norm2D(V3 a) {
    const float l = Len2D(a);
    return l > 1e-4f ? V3{a.x / l, a.y / l, 0.0f} : V3{};
}
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

inline V3 Pos(Entity e) { return From(ENTITY::GET_ENTITY_COORDS(e, TRUE)); }
inline V3 Vel(Entity e) { return From(ENTITY::GET_ENTITY_VELOCITY(e)); }
inline void SetVel(Entity e, V3 v) { ENTITY::SET_ENTITY_VELOCITY(e, v.x, v.y, v.z); }
inline V3 ToWorld(Entity e, V3 local) {
    return From(ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(e, local.x, local.y, local.z));
}
inline V3 ToLocal(Entity e, V3 world) {
    return From(ENTITY::GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS(e, world.x, world.y, world.z));
}
