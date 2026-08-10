#include "Transform.h"
#include "SceneObject.h"
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>

namespace aster
{

void Transform::UpdateLocal() const
{
    if (!localDirty)
        return;

    localMatrix =
        glm::translate(Mat4(1), position) *
        glm::mat4_cast(rotation) *
        glm::scale(Mat4(1), scale);

    localDirty = false;
}

void Transform::UpdateWorld() const
{
    if (!worldDirty)
        return;

    UpdateLocal();

    if (owner->parent)
    {
        auto pt = owner->parent->transform;
        pt.UpdateWorld();

        worldRotation = pt.worldRotation * rotation;
        worldScale = pt.worldScale * scale;
        worldPosition =
            pt.worldRotation *
                (pt.worldScale * position) +
            pt.worldPosition;
        localToWorld = pt.localToWorld * localMatrix;
    }
    else
    {
        worldRotation = rotation;
        worldScale = scale;
        worldPosition = position;
        localToWorld = localMatrix;
    }

    worldDirty = false;
}

void Transform::MarkLocalDirty()
{
    // if (localDirty && worldDirty)
    //     return;

    localDirty = true;
    worldDirty = true;

    for (auto c : owner->children)
        c->transform.MarkWorldDirty();
}

void Transform::MarkWorldDirty()
{
    // if (worldDirty)
    //     return;

    worldDirty = true;

    for (auto c : owner->children)
        c->transform.MarkWorldDirty();
}

// set local euler angles in degree
void Transform::SetEuler(const float yaw, const float pitch, const float roll)
{
    Vec3 eulerangle(pitch, yaw, roll);
    rotation = glm::quat(glm::radians(eulerangle));
    MarkLocalDirty();
}

// get local euler angles in degree
// reutrn (pitch as x, yaw as y, roll as z)
Vec3 Transform::GetEuler() const
{
    return glm::degrees(glm::eulerAngles(rotation));
}

void Transform::SetRotation(const Quat &rot, Space space)
{
    if (space == Space::World && owner->parent)
    {
        auto parentRotationWorld = owner->parent->transform.GetRotation(Space::World);
        rotation = inverse(parentRotationWorld) * rot * parentRotationWorld * rotation;
    }
    else
    {
        rotation = rot;
    }

    MarkLocalDirty();
}

Quat Transform::GetRotation(Space space) const
{
    if (space == Space::World)
    {
        UpdateWorld();
        return worldRotation;
    }
    return rotation;
}

// rotate from originDir to targetDir, both are normalized vectors
void Transform::Rotate(const Vec3 &originDir, const Vec3 &targetDir, Space space)
{
    Vec3 from = glm::normalize(originDir);
    Vec3 to = glm::normalize(targetDir);
    float cosTheta = glm::dot(from, to);
    if (cosTheta >= 1.0f - 1e-6f)
    {
        // Vectors are the same, no rotation needed
        return;
    }
    Quat q = glm::rotation(originDir, targetDir);
    if (space == Space::World && owner->parent)
    {
        auto parentRotationWorld = owner->parent->transform.GetRotation(Space::World);
        rotation = inverse(parentRotationWorld) * q * parentRotationWorld * rotation;
    }
    else
    {
        rotation = q * rotation;
    }

    MarkLocalDirty();
}

// rotate around axis by angle (degree)
void Transform::Rotate(float angle, const Vec3 &axis, Space space)
{
    Quat q = glm::angleAxis(glm::radians(angle), glm::normalize(axis));
    if (space == Space::World && owner->parent)
    {
        auto parentRotationWorld = owner->parent->transform.GetRotation(Space::World);
        rotation = inverse(parentRotationWorld) * q * parentRotationWorld * rotation;
    }
    else
    {
        rotation = q * rotation;
    }

    MarkLocalDirty();
}

void Transform::SetPosition(const Vec3 &pos, Space space)
{
    if (space == Space::World && owner->parent)
    {
        auto pt = owner->parent->transform;
        Vec3 delta = pos - pt.GetPosition(Space::World);
        delta = inverse(pt.GetRotation(Space::World)) * delta;
        delta /= pt.GetScale(Space::World);

        position = delta;
    }
    else
    {
        position = pos;
    }

    MarkLocalDirty();
}

Vec3 Transform::GetPosition(Space space) const
{
    if (space == Space::World && owner->parent)
    {
        UpdateWorld();
        return worldPosition;
    }
    return position;
}

void Transform::Translate(const Vec3 &delta, Space space)
{
    if (space == Space::Local || !owner->parent)
    {
        position += delta;
    }
    else
    {
        auto pt = owner->parent->transform;

        Vec3 d = inverse(pt.GetRotation(Space::World)) * delta;
        d /= pt.GetScale(Space::World);

        position += d;
    }

    MarkLocalDirty();
}

// only local space
void Transform::SetScale(const Vec3 &s)
{
    scale = s;
    MarkLocalDirty();
}

Vec3 Transform::GetScale(Space space) const
{
    if (space == Space::World && owner->parent)
    {
        UpdateWorld();
        return worldScale;
    }
    return scale;
}

Mat4 Transform::GetLocalToWorld() const
{
    UpdateWorld();
    return localToWorld;
}

// defualt space: world
Vec3 Transform::Forward(Space space) const
{
    return glm::normalize(
        glm::vec3(GetRotation(space) * glm::vec4(0, 0, -1, 0)));
}

// defualt space: world
Vec3 Transform::Right(Space space) const
{
    return glm::normalize(
        glm::vec3(GetRotation(space) * glm::vec4(1, 0, 0, 0)));
}

// defualt space: world
Vec3 Transform::Up(Space space) const
{
    return glm::normalize(
        glm::vec3(GetRotation(space) * glm::vec4(0, 1, 0, 0)));
}

} // namespace aster

