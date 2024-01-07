#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"

struct Camera {
  float fov = 45.0f, near = 0.1f, far = 1000.0f;
  float pitch = 0.0f, yaw = 0.0f;
  float aspect_ratio = 1.778f;
  float distance = 10.0f;
  float viewport_width = 800, viewport_height = 600;
};

void createCamera(float start_fov, float start_aspect_ratio, float start_near,
                  float start_far, Camera *out_camera);

void cameraRotate(Camera *camera, const glm::vec2 &delta);

glm::mat4 cameraGetProjectionMatrix(Camera *camera);
glm::mat4 cameraGetViewMatrix(Camera *camera);
glm::vec3 cameraGetUp(Camera *camera);
glm::vec3 cameraGetRight(Camera *camera);
glm::vec3 cameraGetForward(Camera *camera);
glm::quat cameraGetOrientation(Camera *camera);
glm::vec2 cameraGetPanSpeed(Camera *camera);
float cameraGetRotationSpeed(Camera *camera);
float cameraGetZoomSpeed(Camera *camera);
