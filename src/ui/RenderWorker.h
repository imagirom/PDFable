// Background page renderer: one thread per open document view.
#pragma once
#include "Document.h"
#include <QColor>
#include <QImage>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <memory>

// How pages are shown. Dark keeps hues and inverts only the lightness (blue stays blue,
// white paper becomes dark grey); Sepia tints the paper. The PDF itself is never changed.
enum class PageTheme { Light = 0, Sepia = 1, Dark = 2 };
namespace PageColors {
PageTheme theme();
void setTheme(PageTheme t);
void apply(QImage &img, PageTheme t);     // recolour a rendered page (Format_RGB32)
QColor map(const QColor &c);              // the same mapping for a single colour (live ink)
QColor paper();
QColor backdrop();
}

struct RenderJob {
    int kind = 0;        // 0 = page tile, 1 = thumbnail
    int page = 0;
    double scale = 1;
    QRect clip;
    quint64 gen = 0;
    int theme = 0;
};
Q_DECLARE_METATYPE(RenderJob)

class RenderWorker : public QThread {
    Q_OBJECT
public:
    explicit RenderWorker(std::shared_ptr<ink::Document> doc, QObject *parent = nullptr);
    ~RenderWorker() override;
    void request(const RenderJob &job);   // replaces a queued job for the same page/kind
    void clear(int kind);
signals:
    void rendered(const RenderJob &job, const QImage &img);
protected:
    void run() override;
private:
    std::shared_ptr<ink::Document> doc_;
    QMutex mu_;
    QWaitCondition cv_;
    QVector<RenderJob> jobs_;
    bool quit_ = false;
};
