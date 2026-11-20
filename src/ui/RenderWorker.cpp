#include "RenderWorker.h"

namespace PageColors {
static PageTheme g_theme = PageTheme::Light;
PageTheme theme() { return g_theme; }
void setTheme(PageTheme t) { g_theme = t; }

static const int kDarkLo = 30, kDarkHi = 232;     // paper and text grey levels of the dark theme
static const int kSepia[3] = {247, 238, 216};

static inline QRgb mapPixel(QRgb px, PageTheme t)
{
    int r = qRed(px), g = qGreen(px), b = qBlue(px);
    if (t == PageTheme::Dark) {
        // HSL lightness inversion: shifting all channels by the same amount keeps hue and chroma
        int mx = qMax(r, qMax(g, b)), mn = qMin(r, qMin(g, b));
        int shift = 255 - mx - mn;
        r += shift; g += shift; b += shift;
        // dark colours (blue links, dark red ink) would vanish on dark paper: lift them a little
        int lum = (r * 299 + g * 587 + b * 114) / 1000;
        if (lum < 110 && mx - mn > 40) {
            int lift = (110 - lum) * 2 / 3;
            r = qMin(255, r + lift); g = qMin(255, g + lift); b = qMin(255, b + lift);
        }
        r = kDarkLo + r * (kDarkHi - kDarkLo) / 255;
        g = kDarkLo + g * (kDarkHi - kDarkLo) / 255;
        b = kDarkLo + 2 + b * (kDarkHi - kDarkLo) / 255;
    } else if (t == PageTheme::Sepia) {
        r = r * kSepia[0] / 255; g = g * kSepia[1] / 255; b = b * kSepia[2] / 255;
    }
    return qRgb(r, g, b);
}

void apply(QImage &img, PageTheme t)
{
    if (t == PageTheme::Light || img.isNull()) return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32) img = img.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        QRgb last = 0xffffffff, lastOut = mapPixel(0xffffffff, t);     // pages are mostly runs of one colour
        for (int x = 0; x < img.width(); ++x) {
            if (line[x] != last) { last = line[x]; lastOut = mapPixel(last, t); }
            line[x] = lastOut;
        }
    }
}

QColor map(const QColor &c)
{
    QColor out = QColor::fromRgb(mapPixel(c.rgb(), g_theme));
    out.setAlpha(c.alpha());
    return out;
}
QColor paper() { return QColor::fromRgb(mapPixel(0xffffffff, g_theme)); }
QColor backdrop() { return g_theme == PageTheme::Dark ? QColor(0x12, 0x13, 0x15) : QColor(0x40, 0x43, 0x48); }
}

RenderWorker::RenderWorker(std::shared_ptr<ink::Document> doc, QObject *parent) : QThread(parent), doc_(std::move(doc))
{
    qRegisterMetaType<RenderJob>("RenderJob");
    start();
}

RenderWorker::~RenderWorker()
{
    {
        QMutexLocker l(&mu_);
        quit_ = true;
        jobs_.clear();
    }
    cv_.wakeAll();
    wait();
}

void RenderWorker::request(const RenderJob &job)
{
    QMutexLocker l(&mu_);
    for (int i = jobs_.size() - 1; i >= 0; --i)
        if (jobs_[i].kind == job.kind && jobs_[i].page == job.page) jobs_.removeAt(i);
    jobs_.push_back(job);
    cv_.wakeOne();
}

void RenderWorker::clear(int kind)
{
    QMutexLocker l(&mu_);
    for (int i = jobs_.size() - 1; i >= 0; --i)
        if (jobs_[i].kind == kind) jobs_.removeAt(i);
}

void RenderWorker::run()
{
    for (;;) {
        RenderJob job;
        {
            QMutexLocker l(&mu_);
            while (jobs_.isEmpty() && !quit_) cv_.wait(&mu_);
            if (quit_) return;
            // visible page tiles first (most recent request wins), thumbnails in order
            int pick = -1;
            for (int i = jobs_.size() - 1; i >= 0 && pick < 0; --i)
                if (jobs_[i].kind == 0) pick = i;
            if (pick < 0) pick = 0;
            job = jobs_.takeAt(pick);
        }
        QImage img;
        try {
            img = doc_->render(job.page, job.scale, job.clip);
        } catch (const ink::PdfError &) {}
        if (job.kind == 0) PageColors::apply(img, PageTheme(job.theme));
        emit rendered(job, img);
    }
}
