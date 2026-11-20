#include "Creator.h"
#include "MainWindow.h"
#include "Theme.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>

// One running PDFable: a second start (e.g. double-clicking a PDF in Explorer) hands its
// files to the first one, which opens them as tabs.
static QString serverName()
{
    QString user = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", "user"));
    return "PDFable-" + user;
}

static void raiseWindow(MainWindow *w)
{
    w->setWindowState((w->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    w->show();
    w->raise();
    w->activateWindow();
}

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName("PDFable");
    QCoreApplication::setApplicationName("PDFable");
    QCoreApplication::setApplicationVersion(PDFABLE_VERSION);
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);   // deliver every pen sample
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
    QApplication app(argc, argv);

    QStringList files;
    const QStringList args = app.arguments();
    bool creator = false;
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--creator") creator = true;
        else files.push_back(QFileInfo(args[i]).absoluteFilePath());
    }
    auto openCreator = [](const QStringList &paths) {
        CreatorWindow *c = CreatorWindow::showCreator();
        QObject::connect(c, &CreatorWindow::openInReader, c, [](const QString &p) {
            MainWindow *w = MainWindow::windows().isEmpty() ? new MainWindow : MainWindow::windows().last();
            raiseWindow(w);
            w->openFile(p);
        }, Qt::UniqueConnection);
        c->addFiles(paths);
    };

    QLocalSocket probe;
    probe.connectToServer(serverName());
    if (probe.waitForConnected(300)) {
        probe.write(((creator ? QStringList{"--creator"} : QStringList()) + files).join('\n').toUtf8());
        probe.waitForBytesWritten(1000);
        probe.disconnectFromServer();
        return 0;
    }
    QLocalServer server;
    QLocalServer::removeServer(serverName());
    server.listen(serverName());
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, openCreator] {
        QLocalSocket *s = server.nextPendingConnection();
        QObject::connect(s, &QLocalSocket::disconnected, s, [s, openCreator] {
            QStringList paths = QString::fromUtf8(s->readAll()).split('\n', Qt::SkipEmptyParts);
            s->deleteLater();
            if (paths.value(0) == "--creator") {
                openCreator(paths.mid(1));
                return;
            }
            MainWindow *w = MainWindow::windows().isEmpty() ? new MainWindow : MainWindow::windows().last();
            raiseWindow(w);
            for (const QString &p : paths) w->openFile(p);
        });
    });

    Theme::apply(app);
    app.setWindowIcon(QIcon(":/app.png"));
    PdfView::setPageTheme(PageTheme(qBound(0, QSettings().value("view/pageTheme", 0).toInt(), 2)));
    PdfView::setScrollSpeed(QSettings().value("view/scrollSpeed", 0.8).toDouble());
    PdfView::setNoteAfterMarking(QSettings().value("notes/afterMarking", true).toBool());
    if (creator) {          // started as "PDFable Creator": only the creator window
        openCreator(files);
        return app.exec();
    }
    auto *w = new MainWindow;
    w->show();
    if (files.isEmpty()) MainWindow::restoreSession();
    for (const QString &f : files) w->openFile(f);
    return app.exec();
}
