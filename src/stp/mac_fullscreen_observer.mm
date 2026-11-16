#include "mac_fullscreen_observer.h"
#include <QObject>
#include <QWidget>
#include <utility>
#import <AppKit/AppKit.h>

namespace Stp {
namespace {
class FullscreenObserver final : public QObject {
public:
    FullscreenObserver(NSWindow* window, QObject* parent,
        std::function<void(NativeFullscreenPhase)> callback)
        : QObject(parent), m_callback(std::move(callback))
    {
        m_tokens = [[NSMutableArray alloc] init];
        add(window, NSWindowWillEnterFullScreenNotification, NativeFullscreenPhase::WillEnter);
        add(window, NSWindowDidEnterFullScreenNotification, NativeFullscreenPhase::DidEnter);
        add(window, NSWindowWillExitFullScreenNotification, NativeFullscreenPhase::WillExit);
        add(window, NSWindowDidExitFullScreenNotification, NativeFullscreenPhase::DidExit);
    }
    ~FullscreenObserver() override
    {
        for (id token in m_tokens) [[NSNotificationCenter defaultCenter] removeObserver:token];
#if !__has_feature(objc_arc)
        [m_tokens release];
#endif
    }
private:
    void add(NSWindow* window, NSNotificationName name, NativeFullscreenPhase phase)
    {
        const auto token = [[NSNotificationCenter defaultCenter]
            addObserverForName:name object:window queue:nil usingBlock:^(NSNotification*) {
                if (m_callback) m_callback(phase);
            }];
        [m_tokens addObject:token];
    }
    NSMutableArray* m_tokens = nil;
    std::function<void(NativeFullscreenPhase)> m_callback;
};
}

QObject* observeNativeFullscreen(QWidget* window, QObject* parent,
    std::function<void(NativeFullscreenPhase)> callback)
{
    const auto nativeView = reinterpret_cast<NSView*>(window->winId());
    const auto nativeWindow = [nativeView window];
    if (!nativeWindow) return nullptr;
    return new FullscreenObserver(nativeWindow, parent, std::move(callback));
}
} // namespace Stp
