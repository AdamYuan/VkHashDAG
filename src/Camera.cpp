#include "Camera.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

void Camera::move_forward(float dist, float dir) {
	m_position.x -= glm::sin(m_yaw + dir) * dist;
	m_position.z -= glm::cos(m_yaw + dir) * dist;
}

void Camera::DragControl(GLFWwindow *window, double delta) {
	glm::dvec2 cur_pos;
	glfwGetCursorPos(window, &cur_pos.x, &cur_pos.y);

	if (!ImGui::GetCurrentContext()->NavWindow ||
	    (ImGui::GetCurrentContext()->NavWindow->Flags & ImGuiWindowFlags_NoBringToFrontOnFocus)) {
		auto speed = float(delta * m_speed);
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
			move_forward(speed, 0.0f);
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
			move_forward(speed, glm::pi<float>() * 0.5f);
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
			move_forward(speed, -glm::pi<float>() * 0.5f);
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
			move_forward(speed, glm::pi<float>());
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			m_position.y += speed;
		if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
			m_position.y -= speed;

		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) {
			glfwGetCursorPos(window, &cur_pos.x, &cur_pos.y);
			float offset_x = float(cur_pos.x - m_last_mouse_pos.x) * m_sensitive;
			float offset_y = float(cur_pos.y - m_last_mouse_pos.y) * m_sensitive;

			m_yaw -= offset_x;
			m_pitch -= offset_y;

			m_pitch = glm::clamp(m_pitch, -glm::pi<float>() * 0.5f, glm::pi<float>() * 0.5f);
			m_yaw = glm::mod(m_yaw, glm::pi<float>() * 2);
		}
	}
	m_last_mouse_pos = cur_pos;
}

void Camera::MoveControl(GLFWwindow *window, double delta) {
	auto speed = float(delta * m_speed);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		move_forward(speed, 0.0f);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		move_forward(speed, glm::pi<float>() * 0.5f);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		move_forward(speed, -glm::pi<float>() * 0.5f);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		move_forward(speed, glm::pi<float>());
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
		m_position.y += speed;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		m_position.y -= speed;

	glm::dvec2 cur_pos;
	glfwGetCursorPos(window, &cur_pos.x, &cur_pos.y);
	float offset_x = float(cur_pos.x - m_last_mouse_pos.x) * m_sensitive;
	float offset_y = float(cur_pos.y - m_last_mouse_pos.y) * m_sensitive;

	m_yaw -= offset_x;
	m_pitch -= offset_y;

	m_pitch = glm::clamp(m_pitch, -glm::pi<float>() * 0.5f, glm::pi<float>() * 0.5f);
	m_yaw = glm::mod(m_yaw, glm::pi<float>() * 2);

	int w, h;
	glfwGetWindowSize(window, &w, &h);
	glfwSetCursorPos(window, w * 0.5, h * 0.5);

	m_last_mouse_pos = {w * 0.5, h * 0.5};
}
