// TOUCH CONTROLS FOR ANDROID
// Virtual joystick + action buttons for Bugdom on Android.
#pragma once

#ifdef __ANDROID__

#include <stdbool.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Button IDs for touch controls
typedef enum
{
    kTouchBtn_Jump     = 0,   // A / South (jump)
    kTouchBtn_Attack   = 1,   // Y / North (buddy attack)
    kTouchBtn_Kick     = 2,   // X / West  (kick)
    kTouchBtn_Pickup   = 3,   // B / East  (morph/pickup)
    kTouchBtn_Pause    = 4,   // Start     (pause)
    kTouchBtn_ZoomIn   = 5,   // LB        (zoom in)
    kTouchBtn_ZoomOut  = 6,   // RB        (zoom out)
    kTouchBtn_COUNT
} TouchButtonID;

// Initialize the touch control system (call after GL context is ready)
void TouchControls_Init(void);

// Shutdown the touch control system
void TouchControls_Shutdown(void);

// Process an SDL event (call from DoSDLMaintenance for touch events)
bool TouchControls_ProcessEvent(const SDL_Event *event);

// Push current touch state into the SDL virtual gamepad (call once per frame)
void TouchControls_UpdateVirtualGamepad(void);

// Draw the touch controls overlay (call at end of each frame)
void TouchControls_Draw(void);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
