#pragma once

#include <SDL2/SDL.h>
#include <stdint.h>
#include <unordered_map>

class Input {
public:
  static void Begin();
  static void KeyDownEvent(const SDL_Event &event);
  static void KeyUpEvent(const SDL_Event &event);

  static bool WasKeyPressed(SDL_Keycode key);
  static bool WasKeyReleased(SDL_Keycode key);
  static bool WasKeyHeld(SDL_Keycode key);

  static void MouseButtonDownEvent(const SDL_Event &event);
  static void MouseButtonUpEvent(const SDL_Event &event);

  static bool WasMouseButtonPressed(uint8_t button);
  static bool WasMouseButtonReleased(uint8_t button);
  static bool WasMouseButtonHeld(uint8_t button);

  static void WheelEvent(const SDL_Event &event);

  static void GetWheelMovement(int *x, int *y);
  static void GetMousePosition(int *x, int *y);

private:
  static std::unordered_map<SDL_Keycode, bool> pressed_keys;
  static std::unordered_map<SDL_Keycode, bool> released_keys;
  static std::unordered_map<SDL_Keycode, bool> held_keys;

  static std::unordered_map<uint8_t, bool> pressed_mouse_buttons;
  static std::unordered_map<uint8_t, bool> released_mouse_buttons;
  static std::unordered_map<uint8_t, bool> held_mouse_buttons;

  static int wheel_x;
  static int wheel_y;
};