#pragma once
#include <memory>
#include <vector>
#include <glm/gtc/quaternion.hpp>

typedef glm::vec2 Vec2;
typedef glm::vec3 Vec3;
typedef glm::vec4 Vec4;
typedef glm::mat4 Mat4;
typedef glm::mat3x4 Mat3x4;
typedef glm::quat Quat;

namespace aster
{

enum class Space
{
    Local,
    World
};

struct SceneObject;
struct Transform
{
    friend struct SceneObject;

private:
    SceneObject *owner = nullptr;

    Vec3 position = Vec3(0, 0, 0);
    Quat rotation = Quat(1, 0, 0, 0);
    Vec3 scale = Vec3(1, 1, 1);

    mutable bool localDirty = true; // local TRS 改变
    mutable bool worldDirty = true; // parent 或 local 改变
    mutable Quat worldRotation;
    mutable Vec3 worldPosition;
    mutable Vec3 worldScale;
    mutable Mat4 localMatrix;
    mutable Mat4 localToWorld;

    void UpdateLocal() const;
    void UpdateWorld() const;
    void MarkLocalDirty();

protected:
    void MarkWorldDirty();
    void SetOwner(SceneObject *obj)
    {
        owner = obj;
        MarkWorldDirty();
    }

public:
    // set local euler angles in degree
    void SetEuler(const float yaw, const float pitch, const float roll);

    // get local euler angles in degree
    Vec3 GetEuler() const;

    void SetRotation(const Quat &rot, Space space = Space::Local);

    Quat GetRotation(Space space = Space::Local) const;

    // rotate from originDir to targetDir, both are normalized vectors
    void Rotate(const Vec3 &originDir, const Vec3 &targetDir, Space space = Space::Local);

    // rotate around axis by angle (degree)
    void Rotate(float angle, const Vec3 &axis, Space space = Space::Local);

    void SetPosition(const Vec3 &pos, Space space = Space::Local);

    Vec3 GetPosition(Space space = Space::Local) const;

    void Translate(const Vec3 &delta, Space space = Space::Local);

    // only local space
    void SetScale(const Vec3 &s);

    Vec3 GetScale(Space space) const;

    Mat4 GetLocalToWorld() const;

    // defualt space: world
    Vec3 Forward(Space space = Space::World) const;

    // defualt space: world
    Vec3 Right(Space space = Space::World) const;

    // defualt space: world
    Vec3 Up(Space space = Space::World) const;
};

} // namespace aster
