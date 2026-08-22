#include "PhysicsDemoApp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>

#include "Aster/Core/GlobalTime.h"
#include "Aster/Core/Input.h"
#include "Aster/Resource/Material.h"
#include "Aster/Resource/MeshManager.h"
#include "Aster/Render/RenderAPI.h"
#include "Aster/Scene/Camera.h"
#include "Aster/Scene/Model.h"
#include "Aster/Scene/SceneManager.h"

using namespace aster;

// ============================================================================
// 程序化网格
// ============================================================================

// 圆盘（2D）：XY 平面（z=0），半径 radius，法线 +z
void PhysicsDemoApp::BuildDisc(float radius, int segments,
                               std::vector<float> &positions,
                               std::vector<float> &normals,
                               std::vector<unsigned int> &indices)
{
    positions.insert(positions.end(), {0.0f, 0.0f, 0.0f});
    normals.insert(normals.end(), {0.0f, 0.0f, 1.0f});
    const double kTwoPi = 6.28318530717958647692;
    for (int i = 0; i < segments; i++)
    {
        double a = kTwoPi * (double)i / (double)segments;
        positions.push_back((float)(std::cos(a) * radius));
        positions.push_back((float)(std::sin(a) * radius));
        positions.push_back(0.0f);
        normals.push_back(0.0f);
        normals.push_back(0.0f);
        normals.push_back(1.0f);
    }
    for (int i = 1; i < segments - 1; i++)
    {
        indices.push_back(0);
        indices.push_back((unsigned int)i);
        indices.push_back((unsigned int)(i + 1));
    }
    indices.push_back(0);
    indices.push_back((unsigned int)(segments - 1));
    indices.push_back(1);
}

// 单位矩形（2D）：XY 平面（z=0），中心原点，边长 1，法线 +z
void PhysicsDemoApp::BuildQuad(std::vector<float> &positions,
                               std::vector<float> &normals,
                               std::vector<unsigned int> &indices)
{
    const float h = 0.5f;
    const float P[4][3] = {{-h, -h, 0.0f}, {h, -h, 0.0f}, {h, h, 0.0f}, {-h, h, 0.0f}};
    for (int i = 0; i < 4; i++)
    {
        positions.push_back(P[i][0]);
        positions.push_back(P[i][1]);
        positions.push_back(P[i][2]);
        normals.push_back(0.0f);
        normals.push_back(0.0f);
        normals.push_back(1.0f);
    }
    indices = {0, 1, 2, 0, 2, 3};
}

// UV 球（3D）：半径 radius，法线 = 归一化方向
void PhysicsDemoApp::BuildSphere(float radius, int stacks, int slices,
                                 std::vector<float> &positions,
                                 std::vector<float> &normals,
                                 std::vector<unsigned int> &indices)
{
    const double kPi = 3.14159265358979323846;
    const double kTwoPi = 2.0 * kPi;
    positions.clear();
    normals.clear();
    indices.clear();
    for (int i = 0; i <= stacks; i++)
    {
        const double phi = kPi * (double)i / (double)stacks;
        const double sy = std::cos(phi), sr = std::sin(phi);
        for (int j = 0; j <= slices; j++)
        {
            const double theta = kTwoPi * (double)j / (double)slices;
            const float nx = (float)(sr * std::cos(theta));
            const float ny = (float)sy;
            const float nz = (float)(sr * std::sin(theta));
            positions.push_back(nx * radius);
            positions.push_back(ny * radius);
            positions.push_back(nz * radius);
            normals.push_back(nx);
            normals.push_back(ny);
            normals.push_back(nz);
        }
    }
    for (int i = 0; i < stacks; i++)
    {
        for (int j = 0; j < slices; j++)
        {
            const unsigned int a = (unsigned int)(i * (slices + 1) + j);
            const unsigned int b = a + (unsigned int)(slices + 1);
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
            indices.push_back(a + 1);
        }
    }
}

// 单位立方体（3D）：中心原点，边长 1，6 面独立法线（24 顶点）
void PhysicsDemoApp::BuildCube(std::vector<float> &positions,
                               std::vector<float> &normals,
                               std::vector<unsigned int> &indices)
{
    const float h = 0.5f;
    // 面法线 → 4 个角（逆时针，从法线方向看）
    const float F[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0},
                           {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    // 每个面：法线 n，构造两个切向量 u, v 生成 4 角
    for (int fi = 0; fi < 6; fi++)
    {
        const float *n = F[fi];
        // 选 u、v（与 n 正交）
        float u[3], v[3];
        if (std::fabs(n[2]) < 0.999f)
        {
            u[0] = 0.0f; u[1] = 0.0f; u[2] = 1.0f;
        }
        else
        {
            u[0] = 1.0f; u[1] = 0.0f; u[2] = 0.0f;
        }
        // u = cross(n, u0)；v = cross(u, n)
        {
            const float c0 = n[1] * u[2] - n[2] * u[1];
            const float c1 = n[2] * u[0] - n[0] * u[2];
            const float c2 = n[0] * u[1] - n[1] * u[0];
            u[0] = c0; u[1] = c1; u[2] = c2;
        }
        v[0] = u[1] * n[2] - u[2] * n[1];
        v[1] = u[2] * n[0] - u[0] * n[2];
        v[2] = u[0] * n[1] - u[1] * n[0];

        const unsigned int base = (unsigned int)positions.size() / 3;
        for (int ci = 0; ci < 4; ci++)
        {
            const float s = (ci == 1 || ci == 2) ? 1.0f : -1.0f;
            const float t = (ci == 2 || ci == 3) ? 1.0f : -1.0f;
            const float px = n[0] * h + u[0] * s * h + v[0] * t * h;
            const float py = n[1] * h + u[1] * s * h + v[1] * t * h;
            const float pz = n[2] * h + u[2] * s * h + v[2] * t * h;
            positions.push_back(px);
            positions.push_back(py);
            positions.push_back(pz);
            normals.push_back(n[0]);
            normals.push_back(n[1]);
            normals.push_back(n[2]);
        }
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

static glm::vec4 RandomColor()
{
    return glm::vec4(0.35f + 0.65f * (float)(rand() % 100) / 100.0f,
                     0.35f + 0.65f * (float)(rand() % 100) / 100.0f,
                     0.35f + 0.65f * (float)(rand() % 100) / 100.0f,
                     1.0f);
}

// ============================================================================
// 生成 / 注册
// ============================================================================

PhysicsDemoApp::Body *PhysicsDemoApp::SpawnCircle(float x, float y, float radius,
                                                  const glm::vec4 &color)
{
    auto b = std::make_unique<Body>();
    b->kind = 0;
    b->radius = radius;

    auto model = std::make_shared<Model>();
    model->objName = "phy_body_" + std::to_string(bodies.size());
    model->meshes.push_back(MeshManager::Instance().Get("phy_disc"));
    auto mat = std::make_shared<Material>();
    mat->color = color;
    model->material = mat;
    b->model = model;
    b->color = color;

    auto shape = std::make_shared<CircleShape>();
    shape->radius = radius;
    b->shape = shape;

    b->body.SetMass(1.0f);
    b->body.position = Vec3(x, y, 0.0f);
    b->body.linearVelocity = Vec3((float)(rand() % 200 - 100) / 100.0f * 3.0f, 0.0f, 0.0f);
    b->body.linearDamping = linearDamping;

    b->collider2D.body = &b->body;
    b->collider2D.shape = shape;
    b->collider2D.restitution = restitution;
    b->collider2D.friction = friction;

    model->transform.SetPosition(b->body.position);
    model->transform.SetScale(Vec3(radius, radius, 1.0f));

    world2D_.AddCollider(&b->collider2D);
    SceneManager::Instance().AddObject(model);
    bodies.push_back(std::move(b));
    return bodies.back().get();
}

PhysicsDemoApp::Body *PhysicsDemoApp::SpawnRect(float x, float y,
                                                const glm::vec2 &halfExtents,
                                                const glm::vec4 &color)
{
    auto b = std::make_unique<Body>();
    b->kind = 1;
    b->half2 = halfExtents;

    auto model = std::make_shared<Model>();
    model->objName = "phy_body_" + std::to_string(bodies.size());
    model->meshes.push_back(MeshManager::Instance().Get("phy_quad"));
    auto mat = std::make_shared<Material>();
    mat->color = color;
    model->material = mat;
    b->model = model;
    b->color = color;

    auto shape = std::make_shared<RectShape2D>();
    shape->halfExtents = halfExtents;
    b->shape = shape;

    b->body.SetMass(1.0f);
    b->body.position = Vec3(x, y, 0.0f);
    b->body.linearVelocity = Vec3((float)(rand() % 200 - 100) / 100.0f * 3.0f, 0.0f, 0.0f);
    b->body.linearDamping = linearDamping;
    b->body.angularVelocity.z = (float)(rand() % 100 - 50) / 100.0f * 2.0f;

    b->collider2D.body = &b->body;
    b->collider2D.shape = shape;
    b->collider2D.restitution = restitution;
    b->collider2D.friction = friction;

    model->transform.SetPosition(b->body.position);
    model->transform.SetRotation(b->body.rotation);
    model->transform.SetScale(Vec3(halfExtents.x * 2.0f, halfExtents.y * 2.0f, 1.0f));

    world2D_.AddCollider(&b->collider2D);
    SceneManager::Instance().AddObject(model);
    bodies.push_back(std::move(b));
    return bodies.back().get();
}

PhysicsDemoApp::Body *PhysicsDemoApp::SpawnSphere(const Vec3 &pos, float radius,
                                                  const glm::vec4 &color)
{
    auto b = std::make_unique<Body>();
    b->kind = 2;
    b->radius = radius;

    auto model = std::make_shared<Model>();
    model->objName = "phy_body_" + std::to_string(bodies.size());
    model->meshes.push_back(MeshManager::Instance().Get("phy_sphere"));
    auto mat = std::make_shared<Material>();
    mat->color = color;
    model->material = mat;
    b->model = model;
    b->color = color;

    auto shape = std::make_shared<SphereShape>();
    shape->radius = radius;
    b->shape = shape;

    b->body.SetMass(1.0f);
    b->body.position = pos;
    b->body.linearVelocity = Vec3((float)(rand() % 200 - 100) / 100.0f * 2.0f, 0.0f,
                                  (float)(rand() % 200 - 100) / 100.0f * 2.0f);
    b->body.linearDamping = linearDamping;

    b->collider3D.body = &b->body;
    b->collider3D.shape = shape;
    b->collider3D.restitution = restitution;
    b->collider3D.friction = friction;

    model->transform.SetPosition(pos);
    model->transform.SetScale(Vec3(radius));

    world3D_.AddCollider(&b->collider3D);
    SceneManager::Instance().AddObject(model);
    bodies.push_back(std::move(b));
    return bodies.back().get();
}

PhysicsDemoApp::Body *PhysicsDemoApp::SpawnBox(const Vec3 &pos,
                                               const glm::vec3 &halfExtents,
                                               const glm::vec4 &color)
{
    auto b = std::make_unique<Body>();
    b->kind = 3;
    b->half3 = halfExtents;

    auto model = std::make_shared<Model>();
    model->objName = "phy_body_" + std::to_string(bodies.size());
    model->meshes.push_back(MeshManager::Instance().Get("phy_cube"));
    auto mat = std::make_shared<Material>();
    mat->color = color;
    model->material = mat;
    b->model = model;
    b->color = color;

    auto shape = std::make_shared<BoxShape>();
    shape->halfExtents = halfExtents;
    b->shape = shape;

    b->body.SetMass(1.0f);
    b->body.position = pos;
    b->body.linearVelocity = Vec3((float)(rand() % 200 - 100) / 100.0f * 2.0f, 0.0f,
                                  (float)(rand() % 200 - 100) / 100.0f * 2.0f);
    b->body.linearDamping = linearDamping;
    b->body.angularVelocity =
        Vec3((float)(rand() % 100 - 50) / 100.0f, (float)(rand() % 100 - 50) / 100.0f,
             (float)(rand() % 100 - 50) / 100.0f) *
        0.6f;

    b->collider3D.body = &b->body;
    b->collider3D.shape = shape;
    b->collider3D.restitution = restitution;
    b->collider3D.friction = friction;

    model->transform.SetPosition(pos);
    model->transform.SetRotation(b->body.rotation);
    model->transform.SetScale(halfExtents * 2.0f);

    world3D_.AddCollider(&b->collider3D);
    SceneManager::Instance().AddObject(model);
    bodies.push_back(std::move(b));
    return bodies.back().get();
}

void PhysicsDemoApp::AddWall2D(float halfW, float halfH, const Vec2 &center)
{
    auto w = std::make_unique<StaticCollider2D>();
    w->body.SetStatic(true);
    w->body.position = Vec3(center.x, center.y, 0.0f);
    auto shape = std::make_shared<RectShape2D>();
    shape->halfExtents = Vec2(halfW, halfH);
    w->shape = shape;
    w->collider.body = &w->body;
    w->collider.shape = shape;
    w->collider.restitution = 0.6f;
    w->collider.friction = 0.6f;
    world2D_.AddCollider(&w->collider);
    walls2D_.push_back(std::move(w));
}

void PhysicsDemoApp::AddStaticBox3D(const Vec3 &center, const glm::vec3 &halfExtents,
                                    const glm::quat &rot, const glm::vec4 &color)
{
    auto s = std::make_unique<StaticCollider3D>();
    s->body.SetStatic(true);
    s->body.position = center;
    s->body.rotation = rot;
    auto shape = std::make_shared<BoxShape>();
    shape->halfExtents = halfExtents;
    s->shape = shape;
    s->collider.body = &s->body;
    s->collider.shape = shape;
    s->collider.restitution = 0.6f;
    s->collider.friction = 0.6f;

    // 可见模型（cube 缩放 + 旋转）
    auto model = std::make_shared<Model>();
    model->objName = "phy_static_" + std::to_string(walls3D_.size());
    model->meshes.push_back(MeshManager::Instance().Get("phy_cube"));
    auto mat = std::make_shared<Material>();
    mat->color = color;
    model->material = mat;
    model->transform.SetPosition(center);
    model->transform.SetRotation(rot);
    model->transform.SetScale(halfExtents * 2.0f);
    s->model = model;

    world3D_.AddCollider(&s->collider);
    SceneManager::Instance().AddObject(model);
    walls3D_.push_back(std::move(s));
}

void PhysicsDemoApp::AddGround3D(float halfW, float halfH, float halfD, float y)
{
    AddStaticBox3D(Vec3(0.0f, y, 0.0f), glm::vec3(halfW, halfH, halfD),
                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.55f, 0.55f, 0.58f, 1.0f));
}

void PhysicsDemoApp::ResetBody(Body &b)
{
    b.body.ClearForces();
    if (mode2D_)
    {
        b.body.position = Vec3((float)(rand() % 200 - 100) / 100.0f * 9.0f,
                               (float)(rand() % 100) / 100.0f * 6.0f + 8.0f, 0.0f);
        b.body.linearVelocity = Vec3((float)(rand() % 200 - 100) / 100.0f * 4.0f, 0.0f, 0.0f);
        b.body.angularVelocity = Vec3(0.0f, 0.0f, (float)(rand() % 100 - 50) / 100.0f * 2.0f);
    }
    else
    {
        b.body.position = Vec3((float)(rand() % 200 - 100) / 100.0f * 8.0f,
                               (float)(rand() % 100) / 100.0f * 4.0f + 8.0f,
                               (float)(rand() % 200 - 100) / 100.0f * 8.0f);
        b.body.linearVelocity = Vec3((float)(rand() % 200 - 100) / 100.0f * 3.0f, 0.0f,
                                     (float)(rand() % 200 - 100) / 100.0f * 3.0f);
        b.body.angularVelocity =
            Vec3((float)(rand() % 100 - 50) / 100.0f, (float)(rand() % 100 - 50) / 100.0f,
                 (float)(rand() % 100 - 50) / 100.0f) *
            0.5f;
    }
    b.model->transform.SetPosition(b.body.position);
    b.model->transform.SetRotation(b.body.rotation);
    switch (b.kind)
    {
    case 0:
        b.model->transform.SetScale(Vec3(b.radius, b.radius, 1.0f));
        break;
    case 1:
        b.model->transform.SetScale(Vec3(b.half2.x * 2.0f, b.half2.y * 2.0f, 1.0f));
        break;
    case 2:
        b.model->transform.SetScale(Vec3(b.radius));
        break;
    case 3:
        b.model->transform.SetScale(b.half3 * 2.0f);
        break;
    default:
        break;
    }
}

void PhysicsDemoApp::ApplySurfaceParams()
{
    for (auto &b : bodies)
    {
        b->collider2D.restitution = restitution;
        b->collider2D.friction = friction;
        b->collider3D.restitution = restitution;
        b->collider3D.friction = friction;
    }
}

// ============================================================================
// M4 射线拾取辅助
// ============================================================================
void PhysicsDemoApp::BuildPickRay(float mx, float my, Ray &ray) const
{
    auto cam = SceneManager::Instance().GetMainCamera();
    if (!cam)
    {
        ray = Ray{};
        return;
    }
    // 用相机基向量构造射线（与 Vulkan y 翻转后的渲染像素一致）
    const Vec3 camPos = cam->transform.GetPosition();
    const Vec3 f = cam->GetForward();
    const Vec3 r = cam->GetRight();
    const Vec3 u = cam->GetUp();
    const float aspect = (float)width / (float)height;
    const float tanHalfFovY = std::tan(glm::radians(cam->fieldView * 0.5f));
    const float ndcX = 2.0f * mx / (float)width - 1.0f;
    const float ndcY = 1.0f - 2.0f * my / (float)height;
    ray.origin = camPos;
    ray.direction = glm::normalize(
        f + r * (ndcX * tanHalfFovY * aspect) + u * (ndcY * tanHalfFovY));
}

PhysicsDemoApp::Body *PhysicsDemoApp::FindBody2D(Collider2D *c) const
{
    if (!c)
        return nullptr;
    for (auto &b : bodies)
        if (&b->collider2D == c)
            return b.get();
    return nullptr;
}

PhysicsDemoApp::Body *PhysicsDemoApp::FindBody3D(Collider3D *c) const
{
    if (!c)
        return nullptr;
    for (auto &b : bodies)
        if (&b->collider3D == c)
            return b.get();
    return nullptr;
}

// ============================================================================
// 世界构建 / 拆除
// ============================================================================

void PhysicsDemoApp::TeardownWorld()
{
    // 先从世界移除碰撞体 / 刚体（避免悬垂指针）
    world2D_.ClearColliders();
    world2D_.ClearBodies();
    world3D_.ClearColliders();
    world3D_.ClearBodies();
    // 模型延迟销毁（下一帧 Update 移除，避免渲染提交中悬垂）
    for (auto &b : bodies)
        if (b->model)
            SceneManager::Instance().Destroy(b->model);
    bodies.clear();
    walls2D_.clear();
    walls3D_.clear();
}

void PhysicsDemoApp::Setup2D()
{
    // 边框墙（静态矩形）：±12 x ±8 碰撞容器
    AddWall2D(12.0f, 0.5f, Vec2(0.0f, -8.0f));
    AddWall2D(12.0f, 0.5f, Vec2(0.0f, 8.0f));
    AddWall2D(0.5f, 8.0f, Vec2(-12.0f, 0.0f));
    AddWall2D(0.5f, 8.0f, Vec2(12.0f, 0.0f));

    for (int i = 0; i < circleCount; i++)
    {
        float x = (float)(rand() % 200 - 100) / 100.0f * 9.0f;
        float y = 6.0f + (float)(rand() % 100) / 100.0f * 2.0f;
        float r = 0.5f + (float)(rand() % 100) / 100.0f * 0.7f;
        SpawnCircle(x, y, r, RandomColor());
    }
    for (int i = 0; i < rectCount; i++)
    {
        float x = (float)(rand() % 200 - 100) / 100.0f * 9.0f;
        float y = 4.0f + (float)(rand() % 100) / 100.0f * 3.0f;
        glm::vec2 h(0.6f + (float)(rand() % 100) / 100.0f * 0.5f,
                    0.6f + (float)(rand() % 100) / 100.0f * 0.5f);
        SpawnRect(x, y, h, RandomColor());
    }
}

void PhysicsDemoApp::Setup3D()
{
    // 地面 slab（静态盒）
    AddGround3D(20.0f, 0.5f, 20.0f, -0.5f);
    // 斜坡（绕 z 旋转的静态盒，演示摩擦滑落）
    AddStaticBox3D(Vec3(4.0f, 2.0f, 0.0f), glm::vec3(3.0f, 0.3f, 4.0f),
                   glm::angleAxis(glm::radians(-22.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
                   glm::vec4(0.45f, 0.60f, 0.85f, 1.0f));

    // M5：静态立柱阵（BVH 加速演示）—— 7x7 网格，跳过部分位置留空隙
    const int grid = 7;
    for (int ix = 0; ix < grid; ix++)
    {
        for (int iz = 0; iz < grid; iz++)
        {
            if ((ix + iz) % 3 == 0)
                continue; // 留出空隙，球可在其间穿行
            const float x = -7.2f + ix * 2.4f;
            const float z = -7.2f + iz * 2.4f;
            AddStaticBox3D(Vec3(x, 1.0f, z), glm::vec3(0.4f, 1.0f, 0.4f),
                           glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                           glm::vec4(0.30f, 0.55f, 0.80f, 1.0f));
        }
    }

    for (int i = 0; i < sphereCount; i++)
    {
        float x = (float)(rand() % 200 - 100) / 100.0f * 8.0f;
        float z = (float)(rand() % 200 - 100) / 100.0f * 8.0f;
        float y = 8.0f + (float)(rand() % 100) / 100.0f * 4.0f;
        float r = 0.5f + (float)(rand() % 100) / 100.0f * 0.6f;
        SpawnSphere(Vec3(x, y, z), r, RandomColor());
    }
    for (int i = 0; i < boxCount; i++)
    {
        float x = (float)(rand() % 200 - 100) / 100.0f * 8.0f;
        float z = (float)(rand() % 200 - 100) / 100.0f * 8.0f;
        float y = 7.0f + (float)(rand() % 100) / 100.0f * 4.0f;
        glm::vec3 h(0.5f + (float)(rand() % 100) / 100.0f * 0.4f);
        SpawnBox(Vec3(x, y, z), h, RandomColor());
    }
}

void PhysicsDemoApp::SetupWorld()
{
    TeardownWorld();

    // 相机按模式配置
    if (auto cam = SceneManager::Instance().GetMainCamera())
    {
        cam->nearPlane = 0.1f;
        cam->farPlane = 200.0f;
        if (mode2D_)
        {
            cam->transform.SetPosition(Vec3(0.0f, 0.0f, 30.0f));
            cam->transform.SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            cam->fieldView = 40.0f;
        }
        else
        {
            cam->transform.SetPosition(Vec3(14.0f, 9.0f, 16.0f));
            Vec3 lookTarget(0.0f, 1.0f, 0.0f);
            Vec3 dir = glm::normalize(lookTarget - cam->transform.GetPosition());
            cam->transform.Rotate(Vec3(0.0f, 0.0f, -1.0f), dir);
            cam->fieldView = 50.0f;
        }
    }

    if (mode2D_)
        Setup2D();
    else
        Setup3D();
}

// ============================================================================
// App 框架回调
// ============================================================================
bool PhysicsDemoApp::InitScene()
{
    if (auto cam = SceneManager::Instance().GetMainCamera())
        cam->backgroundColor = glm::vec3(0.08f, 0.09f, 0.12f);

    world2D_.SetGravity(Vec2(0.0f, gravityY));
    world3D_.SetGravity(Vec3(0.0f, gravityY, 0.0f));
    world2D_.debugDraw = showShapeDebug;
    world2D_.debugDrawAABB = showAABB;
    world3D_.debugDraw = showShapeDebug;
    world3D_.debugDrawAABB = showAABB;

    // 注册程序化网格
    std::vector<float> pos, nor;
    std::vector<unsigned int> idx;
    BuildDisc(1.0f, 32, pos, nor, idx);
    MeshManager::Instance().LoadMeshFromRawData("phy_disc", pos, idx, nor);
    BuildQuad(pos, nor, idx);
    MeshManager::Instance().LoadMeshFromRawData("phy_quad", pos, idx, nor);
    BuildSphere(1.0f, 12, 24, pos, nor, idx);
    MeshManager::Instance().LoadMeshFromRawData("phy_sphere", pos, idx, nor);
    BuildCube(pos, nor, idx);
    MeshManager::Instance().LoadMeshFromRawData("phy_cube", pos, idx, nor);

    // ASTER_PHYSICS_3D=1 时启动即进入 3D 模式（否则 2D；ImGui 可随时切换）
    if (std::getenv("ASTER_PHYSICS_3D"))
        mode2D_ = false;

    SetupWorld();
    return true;
}

void PhysicsDemoApp::FixedUpdate()
{
    // 物理按固定步长推进（App::Run 以 fixedTimeStep 恒定频率调用本方法）
    if (mode2D_)
        world2D_.Step(fixedTimeStep);
    else
        world3D_.Step(fixedTimeStep);

    // 组件驱动集成：刚体状态 → 场景 Transform
    for (auto &b : bodies)
    {
        b->model->transform.SetPosition(b->body.position);
        b->model->transform.SetRotation(b->body.rotation);
    }
}

void PhysicsDemoApp::Update()
{
    App::Update();

    // 同步可调参数到世界 / 刚体
    world2D_.SetGravity(Vec2(0.0f, gravityY));
    world3D_.SetGravity(Vec3(0.0f, gravityY, 0.0f));
    world2D_.debugDraw = showShapeDebug;
    world2D_.debugDrawAABB = showAABB;
    world3D_.debugDraw = showShapeDebug;
    world3D_.debugDrawAABB = showAABB;
    world2D_.useBVH = useBVH; // M5：BVH 宽相开关（false = 全暴力对比）
    world3D_.useBVH = useBVH;
    for (auto &b : bodies)
        b->body.linearDamping = linearDamping;
    ApplySurfaceParams();

    // 空格：给所有动态体一个向上速度冲量（重新抛起来）
    if (Input::isKeyPressed(GLFW_KEY_SPACE))
    {
        for (auto &b : bodies)
        {
            b->body.linearVelocity.x += (float)(rand() % 200 - 100) / 100.0f * 4.0f;
            b->body.linearVelocity.y += 6.0f + (float)(rand() % 100) / 100.0f * 4.0f;
            if (!mode2D_)
                b->body.linearVelocity.z += (float)(rand() % 200 - 100) / 100.0f * 4.0f;
        }
    }

    // 安全复位：掉出容器 / 数值异常时恢复
    for (auto &b : bodies)
    {
        if (std::isnan(b->body.position.x) || std::isnan(b->body.position.y) ||
            std::isnan(b->body.position.z))
            ResetBody(*b);
        if (b->body.position.y < -30.0f)
            ResetBody(*b);
    }

    // ---- M4 射线拾取：左键点击 → 相机射线 → Raycast ----
    const bool leftDown = Input::isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    const bool clicked = leftDown && !prevLeftDown_ && !Input::isMouseCapturedByImGui();
    prevLeftDown_ = leftDown;
    if (clicked)
    {
        auto [mx, my] = Input::getMousePosition();
        Ray ray;
        BuildPickRay((float)mx, (float)my, ray);
        rayOrigin_ = ray.origin;
        rayDir_ = ray.direction;
        rayLength_ = 100.0f;
        rayValid_ = true;
        pickedBody_ = nullptr;
        lastHit_ = RaycastHit{};
        if (mode2D_)
        {
            if (world2D_.Raycast(ray, rayLength_, lastHit_))
            {
                rayLength_ = lastHit_.t;
                pickedBody_ = FindBody2D(lastHit_.collider2D);
            }
        }
        else
        {
            if (world3D_.Raycast(ray, rayLength_, lastHit_))
            {
                rayLength_ = lastHit_.t;
                pickedBody_ = FindBody3D(lastHit_.collider3D);
            }
        }
        if (pickedBody_)
            std::cout << "[Pick] raycast -> " << pickedBody_->model->objName << std::endl;
    }
    // 恢复基础色 + 高亮拾取体
    for (auto &b : bodies)
        if (b->model && b->model->material)
            b->model->material->color = b->color;
    if (pickedBody_ && pickedBody_->model && pickedBody_->model->material)
        pickedBody_->model->material->color = glm::vec4(1.0f, 0.85f, 0.20f, 1.0f);
}

void PhysicsDemoApp::Render()
{
    App::Render(); // 场景绘制（OpenGL / Vulkan 后端）

    // 物理调试线 → RenderAPI（需在 BeginFrame 之后，即 RenderClear 之后；此处安全）
    SubmitDebugLines();
}

void PhysicsDemoApp::SubmitDebugLines()
{
    if (!renderAPI)
        return;
    const auto &lines = mode2D_ ? world2D_.DebugLines() : world3D_.DebugLines();

    // 世界调试线（形状 / AABB / 接触点）按颜色分组
    if (!lines.empty())
    {
        std::vector<glm::vec3> segs;
        glm::vec4 lastColor(0.0f);
        bool haveLast = false;
        for (const auto &l : lines)
        {
            if (haveLast && (l.color != lastColor))
            {
                renderAPI->DebugDrawLines(segs, lastColor);
                segs.clear();
            }
            segs.push_back(l.a);
            segs.push_back(l.b);
            lastColor = l.color;
            haveLast = true;
        }
        if (!segs.empty())
            renderAPI->DebugDrawLines(segs, lastColor);
    }

    // M4：射线可视化（黄色射线 + 命中点红色十字）
    if (rayValid_)
    {
        const glm::vec3 end = rayOrigin_ + rayDir_ * rayLength_;
        std::vector<glm::vec3> seg = {rayOrigin_, end};
        renderAPI->DebugDrawLines(seg, glm::vec4(1.0f, 0.90f, 0.10f, 1.0f));
        if (lastHit_.hit)
        {
            const glm::vec3 p = lastHit_.point;
            const float s = 0.2f;
            std::vector<glm::vec3> cross = {p - glm::vec3(s, 0.0f, 0.0f), p + glm::vec3(s, 0.0f, 0.0f),
                                            p - glm::vec3(0.0f, s, 0.0f), p + glm::vec3(0.0f, s, 0.0f),
                                            p - glm::vec3(0.0f, 0.0f, s), p + glm::vec3(0.0f, 0.0f, s)};
            renderAPI->DebugDrawLines(cross, glm::vec4(1.0f, 0.20f, 0.20f, 1.0f));
        }
    }
}

void PhysicsDemoApp::RenderImGui()
{
    ImGui::Begin("Physics Demo");

    ImGui::Text("Active backend: %s", renderAPI->Name());
    const size_t staticCount = mode2D_ ? walls2D_.size() : walls3D_.size();
    const size_t contactCount = mode2D_ ? world2D_.Contacts().size() : world3D_.Contacts().size();
    ImGui::Text("%s | Bodies: %zu (dyn) + %zu (static)   Contacts: %zu   Fixed dt: %.0f Hz",
                mode2D_ ? "2D" : "3D", bodies.size(), staticCount, contactCount,
                1.0f / fixedTimeStep);
    ImGui::Separator();

    // ---- 模式切换（重建场景） ----
    bool want2D = mode2D_;
    ImGui::Text("Mode");
    if (ImGui::RadioButton("2D (circle / rect)", want2D))
        want2D = true;
    if (ImGui::RadioButton("3D (sphere / box, SAT)", !want2D))
        want2D = false;
    if (want2D != mode2D_)
    {
        mode2D_ = want2D;
        SetupWorld();
    }

    ImGui::Separator();
    ImGui::Text(mode2D_ ? "M2 Collision (2D circles + rects)"
                        : "M3 Collision (3D spheres + boxes)");
    ImGui::TextWrapped(mode2D_
                           ? "Broad O(n^2) AABB -> narrow (circle/rect) -> impulse solve. "
                             "Rects are OBB (rotate). White=shape, cyan=AABB, red=contact."
                           : "Broad O(n^2) AABB -> narrow (sphere/box SAT, 15 axes) -> impulse. "
                             "Ground slab + tilted ramp (friction). Space = rethrow.");
    ImGui::SliderFloat("Gravity Y", &gravityY, -20.0f, 0.0f, "%.2f");
    ImGui::SliderFloat("Linear Damping", &linearDamping, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f, "%.2f");
    ImGui::Checkbox("Shape wireframe", &showShapeDebug);
    ImGui::Checkbox("AABB (cyan)", &showAABB);

    if (ImGui::Button("Respawn all"))
        for (auto &b : bodies)
            ResetBody(*b);
    ImGui::TextWrapped("Space = velocity kick.");

    // ---- M4 射线拾取 ----
    ImGui::Separator();
    ImGui::Text("M4 Raycast picking");
    ImGui::TextWrapped("Left-click casts a camera ray and highlights the hit body "
                       "(yellow ray / red cross debug). Static geometry via BVH.");
    if (pickedBody_)
        ImGui::Text("Picked: %s (kind %d)", pickedBody_->model->objName.c_str(),
                    pickedBody_->kind);
    else if (lastHit_.hit)
        ImGui::Text("Picked: (static geometry)");
    else
        ImGui::Text("Picked: (none)");

    // ---- M5：BVH 宽相加速 ----
    ImGui::Separator();
    ImGui::Text("M5 BVH broad phase");
    ImGui::Checkbox("Use BVH (static colliders)", &useBVH);
    if (mode2D_)
    {
        ImGui::Text("Pairs: %d   BVH queries: %d   BVH nodes: %d",
                    world2D_.broadPhasePairs, world2D_.bvhQueries, world2D_.bvhNodes);
        ImGui::Text("BVH build: %.3f ms", world2D_.bvhBuildMs);
    }
    else
    {
        ImGui::Text("Pairs: %d   BVH queries: %d   BVH nodes: %d",
                    world3D_.broadPhasePairs, world3D_.bvhQueries, world3D_.bvhNodes);
        ImGui::Text("BVH build: %.3f ms", world3D_.bvhBuildMs);
    }
    ImGui::TextWrapped("Dynamic-static pairs via BVH AABB query; dynamic-dynamic brute. "
                       "Static pillar grid (3D) shows the speedup.");

    ImGui::Separator();
    ImGui::Text("RigidBody states (pos / vel):");
    const size_t shown = std::min<size_t>(bodies.size(), 16);
    for (size_t i = 0; i < shown; i++)
    {
        const auto &p = bodies[i]->body.position;
        const auto &v = bodies[i]->body.linearVelocity;
        static const char *kinds[] = {"circ", "rect", "sphr", "box "};
        ImGui::Text("[%2zu] %s pos=(%6.2f,%6.2f,%6.2f) vel=(%6.2f,%6.2f,%6.2f)",
                    i, kinds[bodies[i]->kind], p.x, p.y, p.z, v.x, v.y, v.z);
    }
    if (bodies.size() > shown)
        ImGui::Text("... and %zu more", bodies.size() - shown);

    ImGui::End();
}

// ============================================================================
// 入口（main_demo.cpp 通过 ASTER_PHYSICS_DEMO=1 调用）
// ============================================================================
int RunPhysicsDemo()
{
    RenderAPIType api = ResolveRenderAPIType(RenderAPIType::Vulkan);
    std::cout << "[PhysicsDemo] Requested backend: "
              << (api == RenderAPIType::Vulkan ? "Vulkan" : "OpenGL") << std::endl;

    auto app = std::make_shared<PhysicsDemoApp>(1200, 800, "Aster Physics Demo", api);
    if (!app->Init())
    {
        std::cerr << "[PhysicsDemo] App init failed" << std::endl;
        return -1;
    }
    app->Run();
    app->Destroy();
    return 0;
}
