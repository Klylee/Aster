#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "SceneObject.h"
#include "Event.h"

#define Color(r, g, b) glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f)

namespace aster
{

class Camera : public SceneObject
{
public:
	REGISTER_SCENE_OBJECT(Camera)

	glm::vec3 backgroundColor = Color(255, 255, 255);

	int width = 800;
	int height = 600;
	float fieldView = 45.0f;
	float nearPlane = 0.001f;
	float farPlane = 100.0f;
	float speed = 0.1f;

	glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);

	float yaw = 0.0f;
	float pitch = 0.0f;
	float roll = 0.0f;
	double sensitivity = 0.1;

	// for mouse left button
	double lastX = 0, lastY = 0;
	bool firstMouse = true;

	void awake() override
	{
		transform.SetEuler(yaw, pitch, 0.0f);
	}
	void update() override;
	void draw() override {}

	glm::vec3 GetForward() const
	{
		return glm::normalize(transform.GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::vec3 GetRight() const
	{
		return glm::normalize(transform.GetRotation() * glm::vec3(1.0f, 0.0f, 0.0f));
	}

	glm::vec3 GetUp() const
	{
		return glm::normalize(transform.GetRotation() * glm::vec3(0.0f, 1.0f, 0.0f));
	}
	Mat4 GetViewMatrix() const
	{
		Vec3 camPos = transform.GetPosition();
		return glm::lookAt(camPos, camPos + GetForward(), GetUp());
	}
	Mat4 GetProjectionMatrix(float aspectRatio = 1) const
	{
		return glm::perspective(glm::radians(fieldView), aspectRatio, nearPlane, farPlane);
	}
	Mat4 Get3DGSSytleProjectionMatrix(float aspectRatio = 1) const
	{
		float fovY = fieldView * height / width;
		float tanHalfFovY = tan(glm::radians(fovY * 0.5f));
		float tanHalfFovX = tan(glm::radians(fieldView * 0.5f));
		float top = tanHalfFovY * nearPlane;
		float bottom = -top;
		float right = tanHalfFovX * nearPlane;
		float left = -right;
		glm::mat4 proj(0.0f);
		proj[0][0] = 2.0f * nearPlane / (right - left);
		proj[1][1] = 2.0f * nearPlane / (top - bottom);
		proj[2][0] = (right + left) / (right - left);
		proj[2][1] = (top + bottom) / (top - bottom);
		proj[2][2] = farPlane / (farPlane - nearPlane);
		proj[3][2] = -(farPlane * nearPlane) / (farPlane - nearPlane);
		proj[2][3] = 1.0f;
		return proj;
	}
};
} // namespace aster