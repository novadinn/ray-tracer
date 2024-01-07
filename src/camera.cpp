#include "camera.h"

#include <algorithm>

void createCamera(float start_fov, float start_aspect_ratio, float start_near,
                  float start_far, Camera *out_camera) {
  out_camera->fov = start_fov;
  out_camera->aspect_ratio = start_aspect_ratio;
  out_camera->near = start_near;
  out_camera->far = start_far;

  cameraGetViewMatrix(out_camera);
}

void cameraRotate(Camera *camera, const glm::vec2 &delta) {
  float yaw_sign = cameraGetUp(camera).y < 0 ? 1.0f : -1.0f;
  camera->yaw += yaw_sign * delta.x * cameraGetRotationSpeed(camera);
  camera->pitch += delta.y * cameraGetRotationSpeed(camera);
}

glm::mat4 cameraGetProjectionMatrix(Camera *camera) {
  camera->aspect_ratio = camera->viewport_width / camera->viewport_height;
  return glm::perspective(glm::radians(camera->fov), camera->aspect_ratio,
                          camera->near, camera->far);
}

glm::mat4 cameraGetViewMatrix(Camera *camera) {
  glm::quat orientation = cameraGetOrientation(camera);
  glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0)) *
                   glm::toMat4(orientation);
  view = glm::inverse(view);

  return view;
}

glm::vec3 cameraGetUp(Camera *camera) {
  return glm::rotate(cameraGetOrientation(camera), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::vec3 cameraGetRight(Camera *camera) {
  return glm::rotate(cameraGetOrientation(camera), glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 cameraGetForward(Camera *camera) {
  return glm::rotate(cameraGetOrientation(camera), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::quat cameraGetOrientation(Camera *camera) {
  return glm::quat(glm::vec3(-camera->pitch, -camera->yaw, 0.0f));
}

glm::vec2 cameraGetPanSpeed(Camera *camera) {
  float x = std::min(camera->viewport_width / 1000.0f, 2.4f);
  float x_factor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

  float y = std::min(camera->viewport_height / 1000.0f, 2.4f);
  float y_factor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

  return {x_factor, y_factor};
}

float cameraGetRotationSpeed(Camera *camera) { return 0.8f; }

float cameraGetZoomSpeed(Camera *camera) {
  float dst = camera->distance * 0.2f;
  dst = std::max(dst, 0.0f);
  float speed = dst * dst;
  speed = std::min(speed, 100.0f);

  return speed;
}
