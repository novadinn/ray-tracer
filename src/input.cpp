#include "input.h"

std::unordered_map<SDL_Keycode, bool> Input::pressed_keys;
std::unordered_map<SDL_Keycode, bool> Input::released_keys;
std::unordered_map<SDL_Keycode, bool> Input::held_keys;
std::unordered_map<uint8_t, bool> Input::pressed_mouse_buttons;
std::unordered_map<uint8_t, bool> Input::released_mouse_buttons;
std::unordered_map<uint8_t, bool> Input::held_mouse_buttons;
int Input::wheel_x;
int Input::wheel_y;

void Input::Begin() {
  pressed_keys.clear();
  released_keys.clear();

  pressed_mouse_buttons.clear();
  released_mouse_buttons.clear();

  wheel_x = 0;
  wheel_y = 0;
}

void Input::KeyDownEvent(const SDL_Event &event) {
  pressed_keys[event.key.keysym.sym] = true;
  held_keys[event.key.keysym.sym] = true;
}

void Input::KeyUpEvent(const SDL_Event &event) {
  released_keys[event.key.keysym.sym] = true;
  held_keys[event.key.keysym.sym] = false;
}

bool Input::WasKeyPressed(SDL_Keycode key) { return pressed_keys[key]; }

bool Input::WasKeyReleased(SDL_Keycode key) { return released_keys[key]; }

bool Input::WasKeyHeld(SDL_Keycode key) { return held_keys[key]; }

void Input::MouseButtonDownEvent(const SDL_Event &event) {
  pressed_mouse_buttons[event.button.button] = true;
  held_mouse_buttons[event.button.button] = true;
}

void Input::MouseButtonUpEvent(const SDL_Event &event) {
  released_mouse_buttons[event.button.button] = true;
  held_mouse_buttons[event.button.button] = false;
}

bool Input::WasMouseButtonPressed(uint8_t button) {
  return pressed_mouse_buttons[button];
}

bool Input::WasMouseButtonReleased(uint8_t button) {
  return released_mouse_buttons[button];
}

bool Input::WasMouseButtonHeld(uint8_t button) {
  return held_mouse_buttons[button];
}

void Input::WheelEvent(const SDL_Event &event) {
  wheel_x = event.wheel.x;
  wheel_y = event.wheel.y;
}

void Input::GetWheelMovement(int *x, int *y) {
  *x = wheel_x;
  *y = wheel_y;
}

void Input::GetMousePosition(int *x, int *y) { SDL_GetMouseState(x, y); }