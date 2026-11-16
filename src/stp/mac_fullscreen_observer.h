#pragma once

#include <functional>

class QObject;
class QWidget;

namespace Stp {
enum class NativeFullscreenPhase { WillEnter, DidEnter, WillExit, DidExit };

// The returned QObject removes the native observers when it is destroyed.
QObject* observeNativeFullscreen(QWidget* window, QObject* parent,
    std::function<void(NativeFullscreenPhase)> callback);
} // namespace Stp
