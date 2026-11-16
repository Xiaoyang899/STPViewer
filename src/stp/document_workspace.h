#pragma once
#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QSet>

class QAction;
class QMenu;
class QMenuBar;
class QStackedWidget;
class QTabWidget;

namespace Stp {
class CadJobQueue;
class MainWindow;

// The shell owns the native menu; each tab owns an entire document UI/session.
class DocumentWorkspace final : public QMainWindow {
    Q_OBJECT
    Q_PROPERTY(bool viewportFullscreenTransitioning READ isViewportFullscreenTransitioning)
public:
    explicit DocumentWorkspace(QWidget* parent = nullptr);
    ~DocumentWorkspace() override;
    void openFiles(const QStringList& paths);
    void closeCurrent();
    void closeAll();
    void closeDocument(MainWindow* page);
    int documentCount() const;
    MainWindow* currentDocument() const;
    CadJobQueue* jobQueue() const { return m_jobs; }
    QMenuBar* nativeMenuBar() const { return m_nativeMenu; }
    bool isViewportFullscreen() const { return !m_fullscreenDocument.isNull(); }
    bool isViewportFullscreenTransitioning() const;
    void toggleViewportFullscreen();
    void exitViewportFullscreen();
signals:
    void viewportFullscreenChanged(bool active);
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;
private:
    void chooseFiles();
    void activateDocument(MainWindow*);
    void updateActiveDocument();
    void updateActions();
    void updateLabels();
    void finishQuit();
    void finishViewportWindowRestore();
#ifdef Q_OS_MACOS
    void ensureNativeFullscreenObserver();
    void nativeFullscreenChanged(bool active, bool completed);
#endif
    CadJobQueue* m_jobs = nullptr;
    QStackedWidget* m_pages = nullptr;
    QTabWidget* m_tabs = nullptr;
    QMenu *m_viewMenu = nullptr, *m_helpMenu = nullptr;
    QMenuBar* m_nativeMenu = nullptr;
    QAction *m_open = nullptr, *m_closeCurrent = nullptr, *m_closeAll = nullptr;
    QAction *m_bom = nullptr, *m_parts = nullptr, *m_assembly = nullptr;
    QHash<QString, QPointer<MainWindow>> m_documents;
    QHash<MainWindow*, QString> m_paths;
    QSet<MainWindow*> m_closing;
    QPointer<MainWindow> m_previous;
    QPointer<MainWindow> m_fullscreenDocument;
    QPointer<QWidget> m_fullscreenFocus;
    QByteArray m_fullscreenGeometry;
    Qt::WindowStates m_previousWindowState;
    bool m_tabBarWasVisible = false, m_cornerWasVisible = false;
    bool m_menuWasVisible = false, m_statusWasVisible = false;
    bool m_fullscreenChanging = false;
    bool m_restoreWindowPending = false;
#ifdef Q_OS_MACOS
    QObject* m_nativeFullscreenObserver = nullptr;
    bool m_nativeFullscreenTransition = false, m_nativeFullscreenActive = false;
#endif
    quint64 m_nextOwner = 0;
    bool m_quitting = false, m_quitScheduled = false;
};
} // namespace Stp
