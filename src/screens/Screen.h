// screens/Screen.h — Abstract Screen interface and ScreenStack navigation manager.
//
// All oscilloscope UI views implement Screen.  The ScreenStack manages a fixed
// depth-6 stack of Screen pointers (no dynamic allocation).  The main loop
// drives the top screen by calling handleEvent() then draw() every frame.
//
// AppContext bundles the two shared objects every screen needs: the scope
// state and the stack itself (so screens can push/pop sub-screens).
#pragma once

#include "../Input.h"
#include "../Renderer.h"
#include "../ScopeState.h"

// Forward declarations.
class ScreenStack;
struct Settings;

// Context bundle passed to every Screen method.
struct AppContext {
    ScopeState&  state;
    ScreenStack& screens;
    Settings&    settings;
};

// Abstract base class for all UI screens.
class Screen {
public:
    virtual ~Screen() {}

    // Called once when this screen becomes the top of the stack.
    virtual void onEnter(AppContext&) {}

    // Called once when this screen is removed from the top of the stack.
    virtual void onExit(AppContext&) {}

    // Handle one input event; mutate ctx.state as appropriate.
    virtual void handleEvent(const InputEvent& e, AppContext& ctx) = 0;

    // Draw the current state into the framebuffer via r.
    virtual void draw(Renderer& r, AppContext& ctx) = 0;
};

// Fixed-depth navigation stack (max 6 screens, no heap allocation).
class ScreenStack {
public:
    // Clear the stack, push root, and call root->onEnter().
    void reset(Screen* root, AppContext& ctx);

    // Push a new screen and call its onEnter().
    void push(Screen* s, AppContext& ctx);

    // Pop the top screen (call onExit) unless it is the only one remaining.
    void pop(AppContext& ctx);

    // Return the current top-of-stack screen (nullptr if empty).
    Screen* top();

    // Forward event to the top screen.
    void handleEvent(const InputEvent& e, AppContext& ctx);

    // Forward draw to the top screen.
    void draw(Renderer& r, AppContext& ctx);

private:
    static const uint8_t MAX_DEPTH = 6;
    Screen*  _stack[MAX_DEPTH];
    uint8_t  _n = 0;
};

// ---- ScreenStack inline implementation ----
// Kept inline here to avoid a separate .cpp translation unit for a handful of
// short methods.

inline void ScreenStack::reset(Screen* root, AppContext& ctx) {
    // Exit and clear any existing screens without calling onEnter again.
    while (_n > 0) {
        _stack[--_n]->onExit(ctx);
    }
    _stack[0] = root;
    _n        = 1;
    root->onEnter(ctx);
}

inline void ScreenStack::push(Screen* s, AppContext& ctx) {
    if (_n >= MAX_DEPTH) return;  // stack full — ignore silently
    _stack[_n++] = s;
    s->onEnter(ctx);
}

inline void ScreenStack::pop(AppContext& ctx) {
    if (_n <= 1) return;  // keep at least the root screen
    _stack[--_n]->onExit(ctx);
}

inline Screen* ScreenStack::top() {
    if (_n == 0) return nullptr;
    return _stack[_n - 1];
}

inline void ScreenStack::handleEvent(const InputEvent& e, AppContext& ctx) {
    Screen* s = top();
    if (s) s->handleEvent(e, ctx);
}

inline void ScreenStack::draw(Renderer& r, AppContext& ctx) {
    Screen* s = top();
    if (s) s->draw(r, ctx);
}
