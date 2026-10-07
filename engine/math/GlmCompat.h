#pragma once

#if __has_include(<glm/glm.hpp>)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#else

#include <cmath>
#include <cstddef>

namespace glm {

struct vec3 final {
    float x{}, y{}, z{};
    constexpr vec3() noexcept = default;
    constexpr vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}
};

struct vec4 final {
    float x{}, y{}, z{}, w{};
    constexpr vec4() noexcept = default;
    constexpr vec4(float x_, float y_, float z_, float w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}
    constexpr explicit vec4(const vec3& v, float w_) noexcept : x(v.x), y(v.y), z(v.z), w(w_) {}
};

struct quat final {
    float w{1.0f}, x{}, y{}, z{};
    constexpr quat() noexcept = default;
    constexpr quat(float w_, float x_, float y_, float z_) noexcept : w(w_), x(x_), y(y_), z(z_) {}
};

struct mat4 final {
    float m[16]{};
    explicit constexpr mat4(float diagonal = 1.0f) noexcept : m{} {
        m[0]=diagonal; m[5]=diagonal; m[10]=diagonal; m[15]=diagonal;
    }
    float* operator[](std::size_t column) noexcept { return &m[column*4u]; }
    const float* operator[](std::size_t column) const noexcept { return &m[column*4u]; }
};

inline constexpr vec3 operator+(const vec3& a,const vec3& b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline constexpr vec3 operator-(const vec3& a,const vec3& b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline constexpr vec3 operator-(const vec3& a) noexcept { return {-a.x,-a.y,-a.z}; }
inline constexpr vec3 operator*(const vec3& a,float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
inline constexpr vec3 operator*(float s,const vec3& a) noexcept { return a*s; }
inline constexpr vec3 operator/(const vec3& a,float s) noexcept { return {a.x/s,a.y/s,a.z/s}; }

inline constexpr float dot(const vec3& a,const vec3& b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline constexpr vec3 cross(const vec3& a,const vec3& b) noexcept {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline float length(const vec3& v) noexcept { return std::sqrt(dot(v,v)); }
inline vec3 normalize(const vec3& v) noexcept {
    const float l=length(v);
    return l>1e-8f ? v/l : vec3{0,0,1};
}

inline constexpr mat4 operator*(const mat4& a,const mat4& b) noexcept {
    mat4 r(0.0f);
    for (std::size_t c=0;c<4;++c)
        for (std::size_t row=0;row<4;++row)
            for (std::size_t k=0;k<4;++k)
                r[c][row]+=a[k][row]*b[c][k];
    return r;
}

inline constexpr vec4 operator*(const mat4& a,const vec4& v) noexcept {
    return {
        a[0][0]*v.x+a[1][0]*v.y+a[2][0]*v.z+a[3][0]*v.w,
        a[0][1]*v.x+a[1][1]*v.y+a[2][1]*v.z+a[3][1]*v.w,
        a[0][2]*v.x+a[1][2]*v.y+a[2][2]*v.z+a[3][2]*v.w,
        a[0][3]*v.x+a[1][3]*v.y+a[2][3]*v.z+a[3][3]*v.w
    };
}

inline mat4 inverse(const mat4& a) noexcept {
    const float* m=a.m;
    mat4 r(0.0f);
    float* inv=r.m;

    inv[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
    inv[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
    inv[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
    inv[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
    inv[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
    inv[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
    inv[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
    inv[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
    inv[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
    inv[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
    inv[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
    inv[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
    inv[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
    inv[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
    inv[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
    inv[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];

    const float det=m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
    if (std::fabs(det)<=1e-8f) return mat4(1.0f);
    for (float& v:r.m) v/=det;
    return r;
}

inline quat normalize(const quat& q) noexcept {
    const float l=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    return l>1e-8f ? quat{q.w/l,q.x/l,q.y/l,q.z/l} : quat{};
}

inline constexpr quat operator*(const quat& a,const quat& b) noexcept {
    return {
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w
    };
}

inline vec3 operator*(const quat& q,const vec3& v) noexcept {
    const vec3 u{q.x,q.y,q.z};
    const float s=q.w;
    return u*(2.0f*dot(u,v))+v*(s*s-dot(u,u))+cross(u,v)*(2.0f*s);
}

inline mat4 toMat4(const quat& qIn) noexcept {
    const quat q=normalize(qIn);
    mat4 r(1.0f);
    r[0][0]=1-2*(q.y*q.y+q.z*q.z); r[0][1]=2*(q.x*q.y+q.z*q.w); r[0][2]=2*(q.x*q.z-q.y*q.w);
    r[1][0]=2*(q.x*q.y-q.z*q.w); r[1][1]=1-2*(q.x*q.x+q.z*q.z); r[1][2]=2*(q.y*q.z+q.x*q.w);
    r[2][0]=2*(q.x*q.z+q.y*q.w); r[2][1]=2*(q.y*q.z-q.x*q.w); r[2][2]=1-2*(q.x*q.x+q.y*q.y);
    return r;
}

inline quat angleAxis(float angle,const vec3& axisIn) noexcept {
    const vec3 axis=normalize(axisIn);
    const float half=angle*0.5f;
    const float s=std::sin(half);
    return normalize(quat{std::cos(half),axis.x*s,axis.y*s,axis.z*s});
}

} // namespace glm

#endif

#if __has_include(<glm/glm.hpp>)
namespace aetheris_glm_compat {
inline float MatElement(const glm::mat4& m, std::size_t index) noexcept {
    return m[index / 4u][index % 4u];
}
inline void SetMatElement(glm::mat4& m, std::size_t index, float value) noexcept {
    m[index / 4u][index % 4u] = value;
}
}
#else
namespace aetheris_glm_compat {
inline float MatElement(const glm::mat4& m, std::size_t index) noexcept {
    return m.m[index];
}
inline void SetMatElement(glm::mat4& m, std::size_t index, float value) noexcept {
    m.m[index] = value;
}
}
#endif
