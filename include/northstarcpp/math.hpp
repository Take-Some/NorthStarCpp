// NorthStarCpp
// Public math types adapted from the original engine declarations by Kayla Verner.
// Maintainer: Kayla Verner
#pragma once

#include <cstdint>

namespace northstarcpp::math {

inline constexpr float epsilon_3 = .001f;
inline constexpr float epsilon_5 = .00001f;
inline constexpr float epsilon_6 = .000001f;
inline constexpr float epsilon_7 = .0000001f;

inline constexpr float pi = 3.1415926535897932384626433832795f;
inline constexpr float pi_x2 = 6.2831853071795864769252867665590f;
inline constexpr float pi_x3 = 9.4247779607693797153879301498385f;
inline constexpr float pi_x4 = 12.566370614359172953850573533118f;
inline constexpr float pi_x6 = 18.849555921538759430775860299677f;
inline constexpr float pi_x8 = 25.132741228718345907701147066236f;
inline constexpr float pi_d2 = 1.5707963267948966192313216916398f;
inline constexpr float pi_d3 = 1.0471975511965977461542144610932f;
inline constexpr float pi_d4 = 0.7853981633974483096156608458199f;
inline constexpr float pi_d6 = 0.5235987755982988730771072305466f;
inline constexpr float pi_d8 = 0.3926990816987241548078304229099f;

enum intersection {
    intersection_none,
    intersection_inside,
    intersection_outside,
    intersection_intersect,
};

class float4x4;

class float3;

class float3_pod {
public:
    typedef float type;

    union {
        struct {
            type x;
            type y;
            type z;
        };
        float elements[3];
    };

    float3 operator-() const;

    float3& operator+=(float3_pod const& other);
    float3& operator+=(type value);

    float3& operator-=(float3_pod const& other);
    float3& operator-=(type value);

    float3& operator*=(type value);
    float3& operator*=(float3_pod const& value);

    float3& operator/=(type value);
    float3& operator/=(float3_pod const& value);

    float3& operator^=(float3_pod const& other);

    type& operator[](int index);
    const type& operator[](int index) const;
    float3& set(type vx, type vy, type vz);

    type dot_product(float3_pod const& other) const;
    float3& cross_product(float3_pod const& other);

    float3& normalize();
    type normalize_r(); // returns old magnitude

    float3& normalize_safe(float3_pod const& result_in_case_of_zero);
    type normalize_safe_r(float3_pod const& result_in_case_of_zero); // returns old magnitude

    float3& abs();

    float3& min(float3_pod const& other);
    float3& max(float3_pod const& other);

    type magnitude() const;
    type square_magnitude() const;

    bool similar(float3_pod const& other, float epsilon = epsilon_5) const;
    bool valid() const;
}; // struct float3_pod

class float3 : public float3_pod {
public:
    float3();
    float3(type x, type y, type z);
    float3(float3_pod const& other);
};

float3 operator+(float3_pod const& left, float3_pod const& right);
float3 operator+(float3_pod const& left, float3_pod::type const& right);
float3 operator-(float3_pod const& left, float3_pod const& right);
float3 operator-(float3_pod const& left, float3_pod::type const& right);
float3 operator*(float3_pod const& left, float3_pod::type const& right);
float3 operator*(float3_pod::type const& left, float3_pod const& right);
float3 operator*(float3_pod const& left, float3_pod const& right);
float3 operator/(float3_pod const& left, float3_pod::type const& right);
float3 operator/(float3_pod::type const& left, float3_pod const& right);
float3 operator/(float3_pod const& left, float3_pod const& right);
float3::type operator|(float3_pod const& left, float3_pod const& right);
float3 operator^(float3_pod const& left, float3_pod const& right);
bool operator<(float3_pod const& left, float3_pod const& right);
bool operator<=(float3_pod const& left, float3_pod const& right);
bool operator>(float3_pod const& left, float3_pod const& right);
bool operator>=(float3_pod const& left, float3_pod const& right);
bool operator!=(float3_pod const& left, float3_pod const& right);
bool operator==(float3_pod const& left, float3_pod const& right);
float3::type dot_product(float3_pod const& left, float3_pod const& right);
float3 cross_product(float3_pod const& left, float3_pod const& right);
float3 normalize(float3_pod const& object);
float3 normalize_safe(float3_pod const& object, float3_pod const& result_in_case_of_zero = float3(0.f, 0.f, 1.f));

float3 abs(float3_pod const& object);
float3 min(float3_pod const& left, float3_pod const& right);
float3 max(float3_pod const& left, float3_pod const& right);
float magnitude(float3_pod const& object);
float square_magnitude(float3_pod const& object);
bool similar(float3_pod const& left, float3_pod const& right, float epsilon = epsilon_5);
bool valid(float3_pod const& object);

float3 abs(float3 const& object);
float3 min(float3 const& left, float3 const& right);
float3 max(float3 const& left, float3 const& right);
float magnitude(float3 const& object);
float square_magnitude(float3 const& object);
bool similar(float3 const& left, float3 const& right, float epsilon = epsilon_5);
bool valid(float3 const& object);

void generate_orthonormal_basis(float3 const& direction, float3& up, float3& right);
float3 normalize_precise(float3 const& object);

class float4;

class float4_pod {
public:
    typedef float type;

    union {
        struct {
            type x;
            type y;
            type z;
            type w;
        };
        struct {
            type r;
            type g;
            type b;
            type a;
        };
        float elements[4];
    };

    float4& operator-();

    float4& operator+=(float4_pod const& other);
    float4& operator+=(type value);

    float4& operator-=(float4_pod const& other);
    float4& operator-=(type value);

    float4& operator*=(type value);
    float4& operator/=(type value);

    float3& xyz();
    float3 const& xyz() const;

    type& operator[](int index);
    const type& operator[](int index) const;
    float4& set(type vx, type vy, type vz, type vw);

    float4& normalize();
    type normalize_r(); // returns old magnitude

    float4& normalize_safe(float4_pod const& result_in_case_of_zero);
    type normalize_safe_r(float4_pod const& result_in_case_of_zero); // returns old magnitude

    float4& abs();

    float4& min(float4_pod const& other);
    float4& max(float4_pod const& other);

    type magnitude() const;
    type square_magnitude() const;

    bool similar(float4_pod const& other, float epsilon = epsilon_5) const;

    bool valid() const;
}; // struct float4_pod

class float4 : public float4_pod {
public:
    float4();
    float4(type x, type y, type z, type w);
    float4(float3 const& xyz, type w);
    float4(float4_pod const& other);
}; // struct float4

float4 operator+(float4_pod const& left, float4_pod const& right);
float4 operator+(float4_pod const& left, float4_pod::type const& right);
float4 operator-(float4_pod const& left, float4_pod const& right);
float4 operator-(float4_pod const& left, float4_pod::type const& right);
float4 operator*(float4_pod const& left, float4_pod::type const& right);
float4 operator*(float4_pod::type const& left, float4_pod const& right);
float4 operator/(float4_pod const& left, float4_pod::type const& right);
bool operator<(float4_pod const& left, float4_pod const& right);
bool operator<=(float4_pod const& left, float4_pod const& right);
bool operator>(float4_pod const& left, float4_pod const& right);
bool operator>=(float4_pod const& left, float4_pod const& right);
bool operator!=(float4_pod const& left, float4_pod const& right);
bool operator==(float4_pod const& left, float4_pod const& right);
float dot_product(float4_pod const& left, float4_pod const& right);
float4 normalize(float4_pod const& object);
float4 normalize_safe(float4_pod const& object, float4_pod const& result_in_case_of_zero = float4(0.f, 0.f, 0.f, 1.f));

class quaternion;

// Matrices store four rows. Points are transformed as row vectors.
class float4x4 {
public:
    typedef float type;

    union {
        struct {
            float4_pod i, j, k, c;
        };
        float elements[4][4];
        struct {
            float e00, e01, e02, e03;
            float e10, e11, e12, e13;
            float e20, e21, e22, e23;
            float e30, e31, e32, e33;
        };
        float4_pod lines[4];
    };

    float4x4();
    float4x4(float4 const& other_i, float4 const& other_j, float4 const& other_k, float4 const& other_c);
    float4x4& identity();

    float4x4& operator*=(float4x4::type value);
    float4x4& operator/=(float4x4::type value);

    float3 transform_position(float3 const& vector) const; // Affine transform including translation.
    float3 transform_direction(float3 const& direction) const; // Linear transform without translation.
    float3 transform(float3 const& vector) const;

    float4 transform(float4 const& vector) const;

    float determinant() const;
    bool try_invert(float4x4 const& other);

    float3 get_angles_xyz_INCORRECT() const;
    float3 get_angles_xyz() const;
    bool valid() const;
}; // struct float4

float4x4 operator*(float4x4 const& left, float4x4::type right);
float4x4 operator*(float4x4::type left, float4x4 const& right);

float4x4 operator/(float4x4 const& left, float4x4::type right);

float3 operator*(float3 const& left, float4x4 const& right);
float4 operator*(float4 const& left, float4x4 const& right);

// 4x3 operations
float4x4 operator*(float4x4 const& left, float4x4 const& right);
float4x4 mul4x3(float4x4 const& left, float4x4 const& right);
float4x4 invert4x3(float4x4 const& other);

// 4x4 operations
float4x4 mul4x4(float4x4 const& left, float4x4 const& right);

float4x4 transpose(float4x4 const& other);

// matrix construction

float4x4 create_translation(float3 const& position);

float4x4 create_rotation(float3 const& angles);
float4x4 create_rotation_INCORRECT(float3 const& angles);
float4x4 create_rotation_x(float4x4::type angle);
float4x4 create_rotation_y(float4x4::type angle);
float4x4 create_rotation_z(float4x4::type angle);
float4x4 create_rotation(float3 const& direction, float3 const& normal);
float4x4 create_rotation(float3 const& axis, float angle);
float4x4 create_matrix(quaternion const& rotation, float3 const& translation);

float4x4 create_scale(float3 const& scale);

float4x4 create_rotation(quaternion const& q);
float4x4 create_matrix(quaternion const& q, float3 const& position);

float4x4 create_projection(float4x4::type field_of_view, float4x4::type aspect_ratio, float4x4::type near_plane, float4x4::type far_plane);
float4x4 create_projection_hat(float4x4::type hat, float4x4::type aspect_ratio, float4x4::type near_plane, float4x4::type far_plane);
float4x4 create_projection_orthogonal(float4x4::type hat, float4x4::type aspect_ratio, float4x4::type near_plane, float4x4::type far_plane);

float4x4 create_camera_at(float3 const& from, float3 const& at, float3 const& up);
float4x4 create_camera_direction(float3 const& from, float3 const& direction, float3 const& up);

class sphere {
public:
    typedef float type;

    union {
        float4_pod vector;
        struct {
            float3_pod center;
            type radius;
        };
    };

    sphere();
    sphere(float3 const& center, type radius);
    intersection intersect_ray(float3 const& position, float3 const& direction, type range, std::uint32_t& count, type (&results)[2]) const;
    intersection intersect_ray(float3 const& position, float3 const& direction, type& range) const;
    bool intersect_ray(float3 const& position, float3 const& direction) const;
    bool intersect(sphere const& other) const;
    bool contains(float3 const& position);
    bool contains(sphere const& other);
    bool similar(sphere const& other) const;
    bool valid() const;
}; // class sphere

class aabb {
public:
    typedef float type;

    float3 min;
    float3 max;

    aabb();

    aabb& zero();
    aabb& identity();
    aabb& invalidate();

    aabb& operator+=(type value);
    aabb& operator+=(float3 const& value);
    aabb& operator-=(type value);
    aabb& operator-=(float3 const& value);
    aabb& operator*=(type value);
    aabb& operator*=(float3 const& value);
    aabb& operator/=(type value);
    aabb& operator/=(float3 const& value);

    aabb& move(float3 const& value);

    aabb& modify(float3 const& position);
    aabb& modify(aabb const& other);
            aabb& modify(float4x4 const& transform);

    float3 extents() const;
    type volume() const;

    float3 center() const;
    float3 center(float3& size) const;

    math::sphere sphere() const;

    bool intersect(aabb const& other) const;

    bool contains(float3 const& position) const;
    bool contains(aabb const& other) const;

    bool similar(aabb const& other) const;

    float3 vertex(std::uint32_t index) const;
    void vertices(float3 (&result)[8]) const;

    bool valid() const;
}; // class aabb

aabb operator+(aabb const& aabb, aabb::type value);
aabb operator+(aabb::type value, aabb const& aabb);
aabb operator+(aabb const& aabb, float3 const& value);
aabb operator+(float3 const& value, aabb const& aabb);
aabb operator-(aabb const& aabb, aabb::type value);
aabb operator-(aabb::type value, aabb const& aabb);
aabb operator-(aabb const& aabb, float3 const& value);
aabb operator-(float3 const& value, aabb const& aabb);
aabb operator*(aabb const& aabb, aabb::type value);
aabb operator*(aabb::type value, aabb const& aabb);
aabb operator*(aabb const& aabb, float3 const& value);
aabb operator*(float3 const& value, aabb const& aabb);
aabb operator/(aabb const& aabb, aabb::type value);
aabb operator/(aabb::type value, aabb const& aabb);
aabb operator/(aabb const& aabb, float3 const& value);
aabb operator/(float3 const& value, aabb const& aabb);

class quaternion {
public:
    union {
        struct {
            float x;
            float y;
            float z;
            float w;
        };
        struct {
            float4_pod vector;
        };
    };

    quaternion();
    explicit quaternion(float4 const& vector);
    explicit quaternion(float4x4 const& matrix);
    explicit quaternion(float3 angles);
    quaternion(float3 const& direction, float angle);

    quaternion& operator+=(quaternion const& other);
    quaternion& operator-=(quaternion const& other);

    quaternion& normalize();
    quaternion& conjugate();
    quaternion& identity();

    float magnitude() const;

    bool axe_angle(float3& axe, float& angle) const;
    bool similar(quaternion const& other, float epsilon = epsilon_5) const;
    bool is_unit() const;

    bool valid() const;
}; // class quaternion

quaternion operator+(quaternion const& left, quaternion const& right);
quaternion operator-(quaternion const& left, quaternion const& right);
quaternion operator*(quaternion const& left, quaternion const& right);

void hemi_flip(quaternion& q, quaternion const& to);

quaternion conjugate(quaternion const& value);
quaternion logn(quaternion const& value);
quaternion exp(quaternion const& value);

quaternion slerp(quaternion const& left, quaternion const& right, float time_delta);

aabb create_min_max(float3 const& min, float3 const& max);
aabb create_center_radius(float3 const& center, float3 const& radius);

bool similar(float left, float right, float epsilon = epsilon_5);
bool is_zero(float value, float epsilon = epsilon_5);
float table_sin(int angle_in_degrees);
float table_cos(int angle_in_degrees);

} // namespace northstarcpp::math
