// TOUCH CONTROLS IMPLEMENTATION FOR ANDROID
// Virtual joystick + action buttons for Bugdom, backed by a SDL3 virtual gamepad.
// All touch input is injected into an SDL virtual joystick so the existing
// gamepad code paths in Input.c handle movement, buttons, and camera automatically.

#ifdef __ANDROID__

#include "TouchControls.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, "Bugdom", __VA_ARGS__)

// -------------------------------------------------------------------------
// Layout constants (normalised window coords 0..1, origin = top-left)
// -------------------------------------------------------------------------

// Left joystick
#define JOY_CX_NORM      0.12f
#define JOY_CY_NORM      0.65f
#define JOY_RADIUS_NORM  0.09f

// Action buttons (right diamond)
#define BTN_CX_NORM      0.82f
#define BTN_CY_NORM      0.65f
#define BTN_RADIUS_NORM  0.048f
#define BTN_SPACING      0.075f

// Pause button – top-right corner
#define PAUSE_CX_NORM      0.95f
#define PAUSE_CY_NORM      0.08f
#define PAUSE_RADIUS_NORM  0.04f

// Right-stick mini joystick – same size as pause, to its left
#define RSTICK_CX_NORM      0.85f
#define RSTICK_CY_NORM      PAUSE_CY_NORM
#define RSTICK_RADIUS_NORM  PAUSE_RADIUS_NORM

// Zoom buttons – top-left corner
#define ZOOM_CY_NORM       0.06f
#define ZOOMIN_CX_NORM     0.05f
#define ZOOMOUT_CX_NORM    0.12f
#define ZOOM_RADIUS_NORM   PAUSE_RADIUS_NORM

#define DEAD_ZONE           0.15f
#define BTN_HIT_MULTIPLIER  1.3f
#define JOY_HIT_MULTIPLIER  1.4f

// -------------------------------------------------------------------------
// State
// -------------------------------------------------------------------------

static int   gWindowW = 1;
static int   gWindowH = 1;

// Left joystick
static bool         gJoyActive   = false;
static float        gJoyTouchX   = 0;
static float        gJoyTouchY   = 0;
static float        gJoyCenterX  = 0;
static float        gJoyCenterY  = 0;
static SDL_FingerID gJoyFinger   = (SDL_FingerID)-1;
static float        gJoyAnalogX  = 0;
static float        gJoyAnalogY  = 0;

// Right-stick mini joystick
static bool         gRStickActive  = false;
static float        gRStickTouchX  = 0;
static float        gRStickTouchY  = 0;
static float        gRStickCenterX = 0;
static float        gRStickCenterY = 0;
static SDL_FingerID gRStickFinger  = (SDL_FingerID)-1;
static float        gRStickAnalogX = 0;
static float        gRStickAnalogY = 0;

// Buttons
static bool         gBtnDown[kTouchBtn_COUNT];
static float        gBtnCX[kTouchBtn_COUNT];
static float        gBtnCY[kTouchBtn_COUNT];
static SDL_FingerID gBtnFinger[kTouchBtn_COUNT];

// SDL virtual gamepad
static SDL_JoystickID gVirtualJoystickID = 0;
static SDL_Joystick  *gVirtualJoystick   = NULL;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static float NormX(float nx) { return nx * (float)gWindowW; }
static float NormY(float ny) { return ny * (float)gWindowH; }

static void UpdateButtonPositions(void)
{
    float cx = NormX(BTN_CX_NORM);
    float cy = NormY(BTN_CY_NORM);
    float sp = NormX(BTN_SPACING);

    // Diamond: top=Jump, right=Attack, bottom=Kick, left=Pickup
    gBtnCX[kTouchBtn_Jump]    = cx;       gBtnCY[kTouchBtn_Jump]    = cy - sp;
    gBtnCX[kTouchBtn_Attack]  = cx + sp;  gBtnCY[kTouchBtn_Attack]  = cy;
    gBtnCX[kTouchBtn_Kick]    = cx;       gBtnCY[kTouchBtn_Kick]    = cy + sp;
    gBtnCX[kTouchBtn_Pickup]  = cx - sp;  gBtnCY[kTouchBtn_Pickup]  = cy;

    gBtnCX[kTouchBtn_Pause]   = NormX(PAUSE_CX_NORM);
    gBtnCY[kTouchBtn_Pause]   = NormY(PAUSE_CY_NORM);

    gBtnCX[kTouchBtn_ZoomIn]  = NormX(ZOOMIN_CX_NORM);
    gBtnCY[kTouchBtn_ZoomIn]  = NormY(ZOOM_CY_NORM);

    gBtnCX[kTouchBtn_ZoomOut] = NormX(ZOOMOUT_CX_NORM);
    gBtnCY[kTouchBtn_ZoomOut] = NormY(ZOOM_CY_NORM);
}

static float BtnRadius(TouchButtonID btn)
{
    if (btn == kTouchBtn_Pause)
        return NormX(PAUSE_RADIUS_NORM);
    if (btn == kTouchBtn_ZoomIn || btn == kTouchBtn_ZoomOut)
        return NormX(ZOOM_RADIUS_NORM);
    return NormX(BTN_RADIUS_NORM);
}

static float JoyRadius(void)    { return NormX(JOY_RADIUS_NORM); }
static float RStickRadius(void) { return NormX(RSTICK_RADIUS_NORM); }

static int HitButton(float x, float y)
{
    for (int i = 0; i < kTouchBtn_COUNT; i++)
    {
        float dx = x - gBtnCX[i];
        float dy = y - gBtnCY[i];
        float r  = BtnRadius(i) * BTN_HIT_MULTIPLIER;
        if (dx*dx + dy*dy <= r*r)
            return i;
    }
    return -1;
}

static bool HitJoystick(float x, float y)
{
    float jcx = NormX(JOY_CX_NORM);
    float jcy = NormY(JOY_CY_NORM);
    float r   = JoyRadius() * JOY_HIT_MULTIPLIER;
    float dx  = x - jcx;
    float dy  = y - jcy;
    return dx*dx + dy*dy <= r*r;
}

static bool HitRightStick(float x, float y)
{
    float rcx = NormX(RSTICK_CX_NORM);
    float rcy = NormY(RSTICK_CY_NORM);
    float r   = RStickRadius() * JOY_HIT_MULTIPLIER;
    float dx  = x - rcx;
    float dy  = y - rcy;
    return dx*dx + dy*dy <= r*r;
}

static void UpdateJoyAnalog(void)
{
    if (!gJoyActive) { gJoyAnalogX = gJoyAnalogY = 0; return; }
    float dx  = (gJoyTouchX - gJoyCenterX) / JoyRadius();
    float dy  = (gJoyTouchY - gJoyCenterY) / JoyRadius();
    float len = sqrtf(dx*dx + dy*dy);
    if (len > 1.0f) { dx /= len; dy /= len; len = 1.0f; }
    if (len < DEAD_ZONE) { gJoyAnalogX = gJoyAnalogY = 0; return; }
    float norm = (len - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    gJoyAnalogX =  dx * norm;
    gJoyAnalogY = -dy * norm;  // SDL Y down → game Y up
}

static void UpdateRStickAnalog(void)
{
    if (!gRStickActive) { gRStickAnalogX = gRStickAnalogY = 0; return; }
    float dx  = (gRStickTouchX - gRStickCenterX) / RStickRadius();
    float dy  = (gRStickTouchY - gRStickCenterY) / RStickRadius();
    float len = sqrtf(dx*dx + dy*dy);
    if (len > 1.0f) { dx /= len; dy /= len; len = 1.0f; }
    if (len < DEAD_ZONE) { gRStickAnalogX = gRStickAnalogY = 0; return; }
    float norm = (len - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    gRStickAnalogX =  dx * norm;
    gRStickAnalogY =  dy * norm;  // camera: SDL Y convention matches here
}

// -------------------------------------------------------------------------
// Init / Shutdown
// -------------------------------------------------------------------------

void TouchControls_Init(void)
{
    memset(gBtnDown,   0, sizeof(gBtnDown));
    for (int i = 0; i < kTouchBtn_COUNT; i++)
        gBtnFinger[i] = (SDL_FingerID)-1;

    gJoyFinger    = (SDL_FingerID)-1;
    gJoyActive    = false;
    gJoyAnalogX   = gJoyAnalogY = 0;

    gRStickFinger  = (SDL_FingerID)-1;
    gRStickActive  = false;
    gRStickAnalogX = gRStickAnalogY = 0;

    // Create a SDL3 virtual joystick of type GAMEPAD.
    // SDL will auto-generate a standard gamepad mapping for it:
    //   naxes=6: axis 0=LEFTX, 1=LEFTY, 2=RIGHTX, 3=RIGHTY, 4=L_TRIG, 5=R_TRIG
    //   nbuttons=SDL_GAMEPAD_BUTTON_COUNT: button N = SDL_GAMEPAD_BUTTON enum value N
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type     = (Uint16)SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes    = (Uint16)SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = (Uint16)SDL_GAMEPAD_BUTTON_COUNT;
    desc.name     = "Bugdom Virtual Controller";

    gVirtualJoystickID = SDL_AttachVirtualJoystick(&desc);
    if (gVirtualJoystickID == 0)
    {
        LOGI("TouchControls_Init: SDL_AttachVirtualJoystick failed: %s", SDL_GetError());
        return;
    }

    gVirtualJoystick = SDL_OpenJoystick(gVirtualJoystickID);
    if (!gVirtualJoystick)
    {
        LOGI("TouchControls_Init: SDL_OpenJoystick failed: %s", SDL_GetError());
        return;
    }

    LOGI("TouchControls_Init: virtual gamepad attached, JoystickID=%d", (int)gVirtualJoystickID);
}

void TouchControls_Shutdown(void)
{
    if (gVirtualJoystick)
    {
        SDL_CloseJoystick(gVirtualJoystick);
        gVirtualJoystick = NULL;
    }
    if (gVirtualJoystickID)
    {
        SDL_DetachVirtualJoystick(gVirtualJoystickID);
        gVirtualJoystickID = 0;
    }
}

// -------------------------------------------------------------------------
// Virtual gamepad update (call every frame before UpdateInput)
// -------------------------------------------------------------------------

void TouchControls_UpdateVirtualGamepad(void)
{
    if (!gVirtualJoystick) return;

    // Left stick: gJoyAnalogY is +forward in game-space; SDL LEFTY=-1 is up/forward
    Sint16 lx = (Sint16)(gJoyAnalogX  * 32767.0f);
    Sint16 ly = (Sint16)(-gJoyAnalogY * 32767.0f);
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_LEFTX,  lx);
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_LEFTY,  ly);

    // Right stick: camera – SDL Y convention, positive Y = down
    Sint16 rx = (Sint16)(gRStickAnalogX * 32767.0f);
    Sint16 ry = (Sint16)(gRStickAnalogY * 32767.0f);
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_RIGHTX, rx);
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_RIGHTY, ry);

    // Face buttons: indices match SDL_GAMEPAD_BUTTON_* enum values
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_SOUTH,          gBtnDown[kTouchBtn_Jump]);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_NORTH,          gBtnDown[kTouchBtn_Attack]);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_WEST,           gBtnDown[kTouchBtn_Kick]);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_EAST,           gBtnDown[kTouchBtn_Pickup]);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_START,          gBtnDown[kTouchBtn_Pause]);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  gBtnDown[kTouchBtn_ZoomIn]);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, gBtnDown[kTouchBtn_ZoomOut]);

    // D-pad from left stick so menu navigation works
    Sint16 threshold = 16384;  // ~0.5
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_DPAD_UP,    ly < -threshold);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN,  ly >  threshold);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_DPAD_LEFT,  lx < -threshold);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, lx >  threshold);
}

// -------------------------------------------------------------------------
// Event processing
// -------------------------------------------------------------------------

bool TouchControls_ProcessEvent(const SDL_Event *event)
{
    // Update window dimensions each time (cheap)
    {
        int count = 0;
        SDL_Window **wins = SDL_GetWindows(&count);
        if (wins && count > 0)
            SDL_GetWindowSizeInPixels(wins[0], &gWindowW, &gWindowH);
        SDL_free(wins);
    }
    UpdateButtonPositions();

    if (event->type != SDL_EVENT_FINGER_DOWN &&
        event->type != SDL_EVENT_FINGER_UP   &&
        event->type != SDL_EVENT_FINGER_MOTION)
        return false;

    float       tx  = event->tfinger.x * (float)gWindowW;
    float       ty  = event->tfinger.y * (float)gWindowH;
    SDL_FingerID fid = event->tfinger.fingerID;

    if (event->type == SDL_EVENT_FINGER_DOWN)
    {
        // Buttons have priority
        int btn = HitButton(tx, ty);
        if (btn >= 0)
        {
            gBtnDown[btn]   = true;
            gBtnFinger[btn] = fid;
            return true;
        }

        // Left joystick
        if (HitJoystick(tx, ty) && !gJoyActive)
        {
            gJoyActive  = true;
            gJoyFinger  = fid;
            gJoyCenterX = NormX(JOY_CX_NORM);
            gJoyCenterY = NormY(JOY_CY_NORM);
            gJoyTouchX  = tx;
            gJoyTouchY  = ty;
            UpdateJoyAnalog();
            return true;
        }

        // Right-stick mini joystick
        if (HitRightStick(tx, ty) && !gRStickActive)
        {
            gRStickActive  = true;
            gRStickFinger  = fid;
            gRStickCenterX = NormX(RSTICK_CX_NORM);
            gRStickCenterY = NormY(RSTICK_CY_NORM);
            gRStickTouchX  = tx;
            gRStickTouchY  = ty;
            UpdateRStickAnalog();
            return true;
        }
    }
    else if (event->type == SDL_EVENT_FINGER_UP)
    {
        // Release button
        for (int i = 0; i < kTouchBtn_COUNT; i++)
        {
            if (gBtnFinger[i] == fid)
            {
                gBtnDown[i]   = false;
                gBtnFinger[i] = (SDL_FingerID)-1;
                return true;
            }
        }

        // Release left joystick
        if (gJoyFinger == fid)
        {
            gJoyActive  = false;
            gJoyFinger  = (SDL_FingerID)-1;
            gJoyAnalogX = gJoyAnalogY = 0;
            return true;
        }

        // Release right stick
        if (gRStickFinger == fid)
        {
            gRStickActive  = false;
            gRStickFinger  = (SDL_FingerID)-1;
            gRStickAnalogX = gRStickAnalogY = 0;
            return true;
        }
    }
    else if (event->type == SDL_EVENT_FINGER_MOTION)
    {
        if (gJoyFinger == fid && gJoyActive)
        {
            gJoyTouchX = tx;
            gJoyTouchY = ty;
            UpdateJoyAnalog();
            return true;
        }

        if (gRStickFinger == fid && gRStickActive)
        {
            gRStickTouchX = tx;
            gRStickTouchY = ty;
            UpdateRStickAnalog();
            return true;
        }
    }

    return false;
}

// -------------------------------------------------------------------------
// Drawing
// -------------------------------------------------------------------------

static GLuint gOvlShader   = 0;
static GLuint gOvlVBO      = 0;
static GLuint gOvlVAO      = 0;
static GLint  gOvlUniColor  = -1;
static GLint  gOvlUniMatrix = -1;
static int    gOvlW = 1, gOvlH = 1;

static const char *kOvlVS =
    "#version 300 es\n"
    "in vec2 a_pos;\n"
    "uniform mat4 u_matrix;\n"
    "void main() { gl_Position = u_matrix * vec4(a_pos, 0.0, 1.0); }\n";

static const char *kOvlFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = u_color; }\n";

static GLuint OvlCompileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static void EnsureOvlShader(void)
{
    if (gOvlShader) return;

    GLuint vs = OvlCompileShader(GL_VERTEX_SHADER,   kOvlVS);
    GLuint fs = OvlCompileShader(GL_FRAGMENT_SHADER, kOvlFS);
    gOvlShader = glCreateProgram();
    glAttachShader(gOvlShader, vs);
    glAttachShader(gOvlShader, fs);
    glBindAttribLocation(gOvlShader, 0, "a_pos");
    glLinkProgram(gOvlShader);
    glDeleteShader(vs);
    glDeleteShader(fs);

    gOvlUniColor  = glGetUniformLocation(gOvlShader, "u_color");
    gOvlUniMatrix = glGetUniformLocation(gOvlShader, "u_matrix");

    glGenBuffers(1, &gOvlVBO);
    glGenVertexArrays(1, &gOvlVAO);
}

// Build orthographic matrix (pixel coords, origin top-left)
static void MakeOrtho2D(float *m, float w, float h)
{
    memset(m, 0, 64);
    m[0]  =  2.0f / w;
    m[5]  = -2.0f / h;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] =  1.0f;
    m[15] =  1.0f;
}

static void SetupDraw(void)
{
    float m[16];
    MakeOrtho2D(m, (float)gOvlW, (float)gOvlH);
    glUseProgram(gOvlShader);
    glUniformMatrix4fv(gOvlUniMatrix, 1, GL_FALSE, m);
    glBindVertexArray(gOvlVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gOvlVBO);
}

static void DrawPrimitives(GLenum mode, const float *verts, int nVerts,
                            float r, float g, float b, float a)
{
    glUniform4f(gOvlUniColor, r, g, b, a);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nVerts * 2 * sizeof(float)), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(mode, 0, nVerts);
}

static void DrawFilledCircle(float cx, float cy, float radius, int segs,
                              float r, float g, float b, float a)
{
    float verts[2 + 2 * 64];
    verts[0] = cx; verts[1] = cy;
    for (int i = 0; i <= segs; i++)
    {
        float angle = (float)i / (float)segs * 6.28318f;
        verts[2 + i*2 + 0] = cx + cosf(angle) * radius;
        verts[2 + i*2 + 1] = cy + sinf(angle) * radius;
    }
    DrawPrimitives(GL_TRIANGLE_FAN, verts, segs + 2, r, g, b, a);
}

static void DrawCircleOutline(float cx, float cy, float radius, int segs,
                               float r, float g, float b, float a)
{
    float verts[2 * 64];
    for (int i = 0; i < segs; i++)
    {
        float angle = (float)i / (float)segs * 6.28318f;
        verts[i*2 + 0] = cx + cosf(angle) * radius;
        verts[i*2 + 1] = cy + sinf(angle) * radius;
    }
    DrawPrimitives(GL_LINE_LOOP, verts, segs, r, g, b, a);
}

// ---- Icon drawing helpers (local coords [-1..1] scaled by radius) --------

typedef struct { float x, y; } Vec2;

static void DrawLocalLines(const Vec2 *pts, int npts,
                            float cx, float cy, float radius,
                            float r, float g, float b, float a)
{
    // npts must be even (pairs of endpoints for GL_LINES)
    if (npts > 32) npts = 32;
    float verts[2 * 32];
    for (int i = 0; i < npts; i++)
    {
        verts[i*2+0] = cx + pts[i].x * radius;
        verts[i*2+1] = cy + pts[i].y * radius;
    }
    DrawPrimitives(GL_LINES, verts, npts, r, g, b, a);
}

static void DrawLocalTriangleFan(const Vec2 *pts, int npts,
                                  float cx, float cy, float radius,
                                  float r, float g, float b, float a)
{
    if (npts > 32) npts = 32;
    float verts[2 * 32];
    for (int i = 0; i < npts; i++)
    {
        verts[i*2+0] = cx + pts[i].x * radius;
        verts[i*2+1] = cy + pts[i].y * radius;
    }
    DrawPrimitives(GL_TRIANGLE_FAN, verts, npts, r, g, b, a);
}

// Jump icon: upward-pointing triangle (▲)
static void DrawIcon_Jump(float cx, float cy, float r, float a)
{
    static const Vec2 pts[] = {
        {0, 0}, {0, -0.65f}, {-0.55f, 0.45f}, {0.55f, 0.45f}, {0, -0.65f}
    };
    DrawLocalTriangleFan(pts, 5, cx, cy, r, 1.0f, 1.0f, 1.0f, a);
}

// Attack icon: starburst (6 radiating lines)
static void DrawIcon_Attack(float cx, float cy, float r, float a)
{
    float verts[24];
    for (int i = 0; i < 6; i++)
    {
        float angle = (float)i / 6.0f * 6.28318f;
        verts[i*4+0] = cx;
        verts[i*4+1] = cy;
        verts[i*4+2] = cx + cosf(angle) * r * 0.65f;
        verts[i*4+3] = cy + sinf(angle) * r * 0.65f;
    }
    glUniform4f(gOvlUniColor, 1, 1, 1, a);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(24 * sizeof(float)), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_LINES, 0, 12);
}

// Kick icon: X shape
static void DrawIcon_Kick(float cx, float cy, float r, float a)
{
    static const Vec2 pts[] = {
        {-0.6f, -0.6f}, {0.6f,  0.6f},
        {-0.6f,  0.6f}, {0.6f, -0.6f}
    };
    DrawLocalLines(pts, 4, cx, cy, r, 1, 1, 1, a);
}

// Morph/Pickup icon: small filled circle (ball shape)
static void DrawIcon_Pickup(float cx, float cy, float r, float a)
{
    DrawFilledCircle(cx, cy, r * 0.45f, 16, 1, 1, 1, a);
}

// Pause icon: two vertical bars
static void DrawIcon_Pause(float cx, float cy, float r, float a)
{
    static const Vec2 pts[] = {
        {-0.3f, -0.6f}, {-0.3f, 0.6f},
        { 0.3f, -0.6f}, { 0.3f, 0.6f}
    };
    DrawLocalLines(pts, 4, cx, cy, r, 1, 1, 1, a);
}

// ZoomIn icon: + sign
static void DrawIcon_ZoomIn(float cx, float cy, float r, float a)
{
    static const Vec2 pts[] = {
        {0, -0.6f}, {0,  0.6f},
        {-0.6f, 0}, {0.6f, 0}
    };
    DrawLocalLines(pts, 4, cx, cy, r, 1, 1, 1, a);
}

// ZoomOut icon: − sign
static void DrawIcon_ZoomOut(float cx, float cy, float r, float a)
{
    static const Vec2 pts[] = {
        {-0.6f, 0}, {0.6f, 0}
    };
    DrawLocalLines(pts, 2, cx, cy, r, 1, 1, 1, a);
}

// -------------------------------------------------------------------------
// Draw
// -------------------------------------------------------------------------

void TouchControls_Draw(void)
{
    {
        int count = 0;
        SDL_Window **wins = SDL_GetWindows(&count);
        if (!wins || count == 0) { SDL_free(wins); return; }
        SDL_GetWindowSizeInPixels(wins[0], &gOvlW, &gOvlH);
        SDL_free(wins);
    }
    UpdateButtonPositions();
    EnsureOvlShader();

    // Save/restore GL state
    GLboolean depthTest, blend, cullFace;
    glGetBooleanv(GL_DEPTH_TEST, &depthTest);
    glGetBooleanv(GL_BLEND,      &blend);
    glGetBooleanv(GL_CULL_FACE,  &cullFace);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    SetupDraw();

    // ---- Left joystick ----
    float jcx = NormX(JOY_CX_NORM);
    float jcy = NormY(JOY_CY_NORM);
    float jr  = JoyRadius();
    DrawFilledCircle(jcx, jcy, jr, 32, 0.3f, 0.3f, 0.3f, 0.15f);
    DrawCircleOutline(jcx, jcy, jr, 32, 0.7f, 0.7f, 0.7f, 0.4f);
    if (gJoyActive)
    {
        float tx = gJoyTouchX, ty = gJoyTouchY;
        float dx = tx - jcx, dy = ty - jcy;
        float len = sqrtf(dx*dx + dy*dy);
        if (len > jr) { tx = jcx + dx/len*jr; ty = jcy + dy/len*jr; }
        DrawFilledCircle(tx, ty, jr * 0.35f, 16, 0.6f, 0.6f, 0.6f, 0.5f);
    }

    // ---- Right-stick mini joystick ----
    float rcx = NormX(RSTICK_CX_NORM);
    float rcy = NormY(RSTICK_CY_NORM);
    float rr  = RStickRadius();
    DrawFilledCircle(rcx, rcy, rr, 24, 0.3f, 0.3f, 0.3f, 0.20f);
    DrawCircleOutline(rcx, rcy, rr, 24, 0.7f, 0.7f, 0.7f, 0.45f);
    if (gRStickActive)
    {
        float tx = gRStickTouchX, ty = gRStickTouchY;
        float dx = tx - rcx, dy = ty - rcy;
        float len = sqrtf(dx*dx + dy*dy);
        if (len > rr) { tx = rcx + dx/len*rr; ty = rcy + dy/len*rr; }
        DrawFilledCircle(tx, ty, rr * 0.35f, 12, 0.6f, 0.6f, 0.6f, 0.5f);
    }
    else
    {
        // Small dot in center to indicate it's a joystick
        DrawFilledCircle(rcx, rcy, rr * 0.2f, 12, 0.6f, 0.6f, 0.6f, 0.3f);
    }

    // ---- Action buttons ----
    for (int i = 0; i < kTouchBtn_COUNT; i++)
    {
        float br    = BtnRadius(i);
        float alpha = gBtnDown[i] ? 0.55f : 0.22f;

        DrawFilledCircle(gBtnCX[i], gBtnCY[i], br, 20, 0.4f, 0.4f, 0.8f, alpha);
        DrawCircleOutline(gBtnCX[i], gBtnCY[i], br, 20, 0.6f, 0.6f, 1.0f, 0.55f);

        // Icon
        float iconAlpha = gBtnDown[i] ? 0.95f : 0.7f;
        float ir = br * 0.55f;
        switch (i)
        {
            case kTouchBtn_Jump:    DrawIcon_Jump(gBtnCX[i],    gBtnCY[i],    ir, iconAlpha); break;
            case kTouchBtn_Attack:  DrawIcon_Attack(gBtnCX[i],  gBtnCY[i],    ir, iconAlpha); break;
            case kTouchBtn_Kick:    DrawIcon_Kick(gBtnCX[i],    gBtnCY[i],    ir, iconAlpha); break;
            case kTouchBtn_Pickup:  DrawIcon_Pickup(gBtnCX[i],  gBtnCY[i],    ir, iconAlpha); break;
            case kTouchBtn_Pause:   DrawIcon_Pause(gBtnCX[i],   gBtnCY[i],    ir, iconAlpha); break;
            case kTouchBtn_ZoomIn:  DrawIcon_ZoomIn(gBtnCX[i],  gBtnCY[i],    ir, iconAlpha); break;
            case kTouchBtn_ZoomOut: DrawIcon_ZoomOut(gBtnCX[i], gBtnCY[i],    ir, iconAlpha); break;
        }
    }

    glBindVertexArray(0);

    // Restore GL state
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend)     glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (cullFace)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
}

#endif // __ANDROID__
