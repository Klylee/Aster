#include "Camera.h"
#include "Input.h"
#include "GlobalTime.h"
#include <iostream>
#include <algorithm>

namespace aster
{

void Camera::update()
{
	// Key Events
	Vec3 forward = GetForward();
	Vec3 forwardXZ = glm::normalize(Vec3(forward.x, 0, forward.z));
	Vec3 rightXZ = glm::normalize(Vec3(-forwardXZ.z, 0, forwardXZ.x));

	float dt = GlobalTime::GetFrameDeltaTime();
	if (Input::isKeyPressed(GLFW_KEY_W))
		transform.SetPosition(transform.GetPosition() - forwardXZ * speed * dt);
	if (Input::isKeyPressed(GLFW_KEY_S))
		transform.SetPosition(transform.GetPosition() + forwardXZ * speed * dt);
	if (Input::isKeyPressed(GLFW_KEY_A))
		transform.SetPosition(transform.GetPosition() - rightXZ * speed * dt);
	if (Input::isKeyPressed(GLFW_KEY_D))
		transform.SetPosition(transform.GetPosition() + rightXZ * speed * dt);
	if (Input::isKeyPressed(GLFW_KEY_E))
		transform.SetPosition(transform.GetPosition() + Vec3(0, speed * dt, 0));
	if (Input::isKeyPressed(GLFW_KEY_Q))
		transform.SetPosition(transform.GetPosition() - Vec3(0, speed * dt, 0));

	if (Input::isKeyPressed(GLFW_KEY_LEFT_BRACKET))
	{
		fieldView += 5.0f * dt;
		if (fieldView > 120.0f)
			fieldView = 120.0f;
	}

	if (Input::isKeyPressed(GLFW_KEY_RIGHT_BRACKET))
	{
		fieldView -= 5.0f * dt;
		if (fieldView < 15.0f)
			fieldView = 15.0f;
	}

	if (!Input::isMouseCapturedByImGui() && Input::isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT))
	{
		auto [xpos, ypos] = Input::getMousePosition();
		if (firstMouse)
		{
			lastX = xpos;
			lastY = ypos;
			firstMouse = false;
		}
		double dx = xpos - lastX;
		double dy = lastY - ypos;
		lastX = xpos;
		lastY = ypos;

		if (Input::isKeyPressed(GLFW_KEY_R))
		{

			double k = 0.1;
			double delta_yaw = glm::radians(k * -dx);
			double delta_pitch = glm::radians(k * dy);

			// 绕target旋转
			glm::vec3 camPos = transform.GetPosition();
			glm::vec3 offset = camPos - target;

			// 绕Y轴旋转 yaw
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), (float)delta_yaw, GetUp());
			offset = glm::vec3(rotY * glm::vec4(offset, 1.0f));

			// 绕相机右轴旋转 pitch
			glm::vec3 right = transform.GetRotation() * glm::vec3(1, 0, 0);
			glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), (float)delta_pitch, GetRight());
			offset = glm::vec3(rotX * glm::vec4(offset, 1.0f));

			camPos = target + offset;
			transform.SetPosition(camPos);
			glm::mat4 new_rot = rotX * rotY * glm::mat4_cast(transform.GetRotation());
			// 从新的旋转提取欧拉角
			glm::vec3 new_euler = glm::eulerAngles(glm::quat_cast(new_rot));
			yaw = glm::degrees(new_euler.y);
			pitch = glm::degrees(new_euler.x);
			roll = glm::degrees(new_euler.z);
			transform.SetEuler(yaw, pitch, roll);
		}
		else
		{
			dx *= sensitivity;
			dy *= sensitivity;
			float delta_yaw = glm::radians(dx);
			float delta_pitch = glm::radians(dy);
			glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), delta_yaw, GetUp());
			glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), delta_pitch, GetRight());
			glm::mat4 new_rot = rotX * rotY * glm::mat4_cast(transform.GetRotation());
			glm::vec3 new_euler = glm::eulerAngles(glm::quat_cast(new_rot));
			yaw = glm::degrees(new_euler.y);
			pitch = glm::degrees(new_euler.x);
			roll = glm::degrees(new_euler.z);
			transform.SetEuler(yaw, pitch, roll);
		}
	}
	else
	{
		firstMouse = true;
	}
#ifdef DEBUG
	std::cout << std::format("Camera Position: ({:.2f}, {:.2f}, {:.2f}), Yaw: {:.2f}, Pitch: {:.2f}, Roll: {:.2f}\n",
							 transform.GetPosition().x, transform.GetPosition().y, transform.GetPosition().z, yaw, pitch, roll);
#endif
}

} // namespace aster

