#ifndef HYPERCRAFT_CLIENT_CAMERA_HPP
#define HYPERCRAFT_CLIENT_CAMERA_HPP

#include <myvk/Buffer.hpp>
#include <myvk/DescriptorSet.hpp>

#include <cinttypes>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct GLFWwindow;

class Camera {
public:
	glm::vec3 m_position{0.0f, 0.0f, 0.0f};
	float m_yaw{0.0f}, m_pitch{0.0f};
	float m_sensitive{0.005f}, m_speed{.5f}, m_fov{glm::pi<float>() / 3.f};

	struct LookSideUp {
		glm::vec3 look, side, up;
	};

private:
	glm::dvec2 m_last_mouse_pos{0.0, 0.0};

	void move_forward(float dist, float dir);

public:
	void DragControl(GLFWwindow *window, double delta);
	void MoveControl(GLFWwindow *window, double delta);

	inline glm::vec3 GetLook() const {
		float xz_len = glm::cos(m_pitch);
		return glm::vec3{xz_len * glm::sin(m_yaw), glm::sin(m_pitch), xz_len * glm::cos(m_yaw)};
	}
	inline LookSideUp GetLookSideUp(float aspect_ratio) const {
		auto trans = glm::identity<glm::mat4>();
		trans = glm::rotate(trans, m_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
		trans = glm::rotate(trans, m_pitch, glm::vec3(-1.0f, 0.0f, 0.0f));
		float tg = glm::tan(m_fov * 0.5f);
		glm::vec3 look = (trans * glm::vec4(0.0, 0.0, 1.0, 0.0));
		glm::vec3 side = (trans * glm::vec4(1.0, 0.0, 0.0, 0.0));
		look = glm::normalize(look);
		side = glm::normalize(side) * tg * aspect_ratio;
		glm::vec3 up = glm::normalize(glm::cross(look, side)) * tg;

		return {
		    .look = look,
		    .side = side,
		    .up = up,
		};
	}
};

#endif
