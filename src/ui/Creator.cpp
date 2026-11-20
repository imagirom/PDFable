#include "Creator.h"

#include <QApplication>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

using namespace ink;

static const QStringList kImageSuffixes{"png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"};
static const QColor kTags[] = {QColor(0xb0, 0x1e, 0x2f), QColor(0x1f, 0x6f, 0xb5), QColor(0x2e, 0x8b, 0x57), QColor(0xc7, 0x7d, 0x0a),
                               QColor(0x7b, 0x3f, 0xa0), QColor(0x0f, 0x8a, 0x8a)};

CreatorWindow *CreatorWindow::showCreator()
{
    static QPointer<CreatorWindow> instance;
    if (!instance) {
        instance = new CreatorWindow;
        instance->setAttribute(Qt::WA_DeleteOnClose);
    }
    instance->show();
    instance->raise();
    instance->activateWindow();
    return instance;
}

CreatorWindow::CreatorWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("PDFable Creator"));
    setAcceptDrops(true);
    resize(1100, 760);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 10);

    auto *top = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add files…"));
    auto *remove = new QPushButton(tr("Remove file"));
    top->addWidget(add);
    top->addWidget(remove);
    top->addSpacing(18);
    top->addWidget(new QLabel(tr("Combine:")));
    const QStringList names{tr("Whole files"), tr("Arrange pages"), tr("Zipper")};
    const QStringList tips{tr("One file after the other, in the order of the list on the left."),
                           tr("All pages in one grid: drag them into any order, rotate or delete single pages."),
                           tr("Alternate the pages: A1, B1, A2, B2, … For example front sides in one scan, back sides in another.")};
    for (int i = 0; i < 3; ++i) {
        modeBtn_[i] = new QRadioButton(names[i]);
        modeBtn_[i]->setToolTip(tips[i]);
        top->addWidget(modeBtn_[i]);
        connect(modeBtn_[i], &QRadioButton::clicked, this, [this, i] { setMode(Mode(i)); });
    }
    modeBtn_[0]->setChecked(true);
    reverse_ = new QCheckBox(tr("Second file is in reverse order"));
    reverse_->setToolTip(tr("For back sides that were scanned from the last page to the first."));
    top->addWidget(reverse_);
    top->addStretch();
    root->addLayout(top);

    auto *split = new QSplitter;
    files_ = new QListWidget;
    files_->setDragDropMode(QAbstractItemView::InternalMove);
    files_->setDefaultDropAction(Qt::MoveAction);
    files_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files_->setToolTip(tr("Drag to change the order of the files."));
    grid_ = new QListWidget;
    grid_->setFlow(QListView::LeftToRight);
    grid_->setWrapping(true);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setIconSize(QSize(120, 160));
    grid_->setSpacing(6);
    grid_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grid_->setDefaultDropAction(Qt::MoveAction);
    grid_->setWordWrap(true);
    split->addWidget(files_);
    split->addWidget(grid_);
    split->setStretchFactor(1, 1);
    split->setSizes({270, 800});
    root->addWidget(split, 1);

    auto *bottom = new QHBoxLayout;
    rotL_ = new QPushButton(tr("Rotate left"));
    rotR_ = new QPushButton(tr("Rotate right"));
    delPage_ = new QPushButton(tr("Delete page"));
    bottom->addWidget(rotL_);
    bottom->addWidget(rotR_);
    bottom->addWidget(delPage_);
    hint_ = new QLabel;
    bottom->addWidget(hint_, 1);
    openAfter_ = new QCheckBox(tr("Open the result in PDFable"));
    openAfter_->setChecked(QSettings().value("creator/openAfter", true).toBool());
    bottom->addWidget(openAfter_);
    create_ = new QPushButton(tr("Create PDF…"));
    create_->setDefault(true);
    bottom->addWidget(create_);
    root->addLayout(bottom);

    connect(add, &QPushButton::clicked, this, [this] {
        QSettings st;
        const QStringList f = QFileDialog::getOpenFileNames(this, tr("Add PDFs or images"), st.value("lastDir").toString(),
                                                            tr("PDFs and images (*.pdf *.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All files (*)"));
        if (!f.isEmpty()) st.setValue("lastDir", QFileInfo(f.first()).absolutePath());
        addFiles(f);
    });
    connect(remove, &QPushButton::clicked, this, &CreatorWindow::removeSelectedFiles);
    connect(reverse_, &QCheckBox::toggled, this, [this] { regenerate(); });
    connect(rotL_, &QPushButton::clicked, this, [this] { rotateSelected(-90); });
    connect(rotR_, &QPushButton::clicked, this, [this] { rotateSelected(90); });
    connect(delPage_, &QPushButton::clicked, this, &CreatorWindow::removeSelectedPages);
    connect(create_, &QPushButton::clicked, this, &CreatorWindow::createPdf);
    connect(files_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        QTimer::singleShot(0, this, [this] {
            order_.clear();
            for (int i = 0; i < files_->count(); ++i) order_.push_back(files_->item(i)->data(Qt::UserRole).toInt());
            refreshFiles();
            regenerate();
        });
    });
    connect(grid_->model(), &QAbstractItemModel::rowsMoved, this, [this] { QTimer::singleShot(0, this, &CreatorWindow::syncFromGrid); });
    connect(&thumbTimer_, &QTimer::timeout, this, &CreatorWindow::thumbStep);
    updateControls();
}

void CreatorWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void CreatorWindow::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths.push_back(u.toLocalFile());
    paths.sort(Qt::CaseInsensitive);
    addFiles(paths);
}

void CreatorWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete) {
        if (grid_->hasFocus()) removeSelectedPages();
        else if (files_->hasFocus()) removeSelectedFiles();
        return;
    }
    QMainWindow::keyPressEvent(e);
}

void CreatorWindow::addFiles(const QStringList &paths)
{
    QStringList problems;
    for (const QString &p : paths) {
        Source s;
        s.path = QFileInfo(p).absoluteFilePath();
        s.image = kImageSuffixes.contains(QFileInfo(p).suffix().toLower());
        if (s.image) {
            if (!QImageReader(s.path).canRead()) { problems << tr("%1: not a readable image").arg(QFileInfo(p).fileName()); continue; }
        } else {
            try {
                s.doc = std::make_shared<Document>();
                s.doc->open(s.path);
                if (s.doc->needsPassword()) { problems << tr("%1: password protected").arg(QFileInfo(p).fileName()); continue; }
                s.pages = s.doc->pageCount();
                if (s.pages < 1) continue;
            } catch (const PdfError &err) {
                problems << QString("%1: %2").arg(QFileInfo(p).fileName(), err.message());
                continue;
            }
        }
        int id = nextId_++;
        sources_.insert(id, s);
        order_.push_back(id);
        if (mode_ == ArrangePages)
            for (int i = 0; i < s.pages; ++i) pages_.push_back({id, i, 0});
    }
    refreshFiles();
    regenerate();
    if (!problems.isEmpty()) QMessageBox::warning(this, windowTitle(), tr("Some files were skipped:\n%1").arg(problems.join('\n')));
}

void CreatorWindow::setMode(Mode m)
{
    mode_ = m;
    modeBtn_[m]->setChecked(true);
    regenerate();
}

void CreatorWindow::setReverseSecond(bool on) { reverse_->setChecked(on); }

// Rebuild the page sequence from the file list (the hand-arranged mode keeps what the user made).
void CreatorWindow::regenerate()
{
    if (mode_ == WholeFiles) {
        pages_.clear();
        for (int id : std::as_const(order_))
            for (int i = 0; i < sources_[id].pages; ++i) pages_.push_back({id, i, 0});
    } else if (mode_ == Zipper) {
        pages_.clear();
        int longest = 0;
        for (int id : std::as_const(order_)) longest = qMax(longest, sources_[id].pages);
        for (int i = 0; i < longest; ++i)
            for (int k = 0; k < order_.size(); ++k) {
                const Source &s = sources_[order_[k]];
                if (i >= s.pages) continue;
                bool rev = reverse_->isChecked() && (k % 2 == 1);
                pages_.push_back({order_[k], rev ? s.pages - 1 - i : i, 0});
            }
    }
    refreshGrid();
    updateControls();
}

void CreatorWindow::updateControls()
{
    bool arrange = mode_ == ArrangePages;
    grid_->setDragDropMode(arrange ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    rotL_->setEnabled(arrange);
    rotR_->setEnabled(arrange);
    delPage_->setEnabled(arrange);
    reverse_->setVisible(mode_ == Zipper);
    create_->setEnabled(!pages_.isEmpty());
    if (order_.isEmpty()) hint_->setText(tr("Drop PDF files and images anywhere in this window."));
    else hint_->setText(tr("%1 file(s), %2 page(s) in the result").arg(order_.size()).arg(pages_.size()));
}

void CreatorWindow::refreshFiles()
{
    rebuilding_ = true;
    files_->clear();
    for (int k = 0; k < order_.size(); ++k) {
        const Source &s = sources_[order_[k]];
        QPixmap tag(14, 14);
        tag.fill(kTags[order_[k] % 6]);
        auto *it = new QListWidgetItem(QIcon(tag), QString("%1   (%2)").arg(QFileInfo(s.path).fileName(), s.image ? tr("image") : tr("%1 p.").arg(s.pages)));
        it->setData(Qt::UserRole, order_[k]);
        it->setToolTip(s.path);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        files_->addItem(it);
    }
    rebuilding_ = false;
}

void CreatorWindow::refreshGrid()
{
    rebuilding_ = true;
    grid_->clear();
    for (int i = 0; i < pages_.size(); ++i) {
        const PageRef &p = pages_[i];
        const Source &s = sources_[p.src];
        auto *it = new QListWidgetItem;
        it->setData(Qt::UserRole, i);
        it->setSizeHint(QSize(132, 176));
        it->setToolTip(QString("%1, %2").arg(QFileInfo(s.path).fileName(), s.image ? tr("image") : tr("page %1").arg(p.page + 1)));
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        grid_->addItem(it);
    }
    rebuilding_ = false;
    thumbTimer_.start(0);
}

void CreatorWindow::syncFromGrid()
{
    if (rebuilding_ || mode_ != ArrangePages) return;
    QVector<PageRef> next;
    for (int i = 0; i < grid_->count(); ++i) {
        next.push_back(pages_.value(grid_->item(i)->data(Qt::UserRole).toInt()));
        grid_->item(i)->setData(Qt::UserRole, i);
    }
    pages_ = next;
}

void CreatorWindow::movePage(int from, int to)
{
    if (mode_ != ArrangePages || from < 0 || from >= pages_.size() || to < 0 || to >= pages_.size()) return;
    pages_.move(from, to);
    refreshGrid();
}

QPixmap CreatorWindow::thumbnail(const PageRef &p)
{
    QString key = QString("%1/%2/%3").arg(p.src).arg(p.page).arg(p.rot);
    auto hit = thumbs_.constFind(key);
    if (hit != thumbs_.constEnd()) return hit.value();
    const Source &s = sources_[p.src];
    QImage img;
    if (s.image) {
        QImageReader rd(s.path);
        rd.setAutoTransform(true);
        QSize sz = rd.size();
        if (sz.isValid()) rd.setScaledSize(sz.scaled(480, 640, Qt::KeepAspectRatio));
        img = rd.read();
    } else {
        QSizeF ps = s.doc->pageSize(p.page);
        double sc = qMin(240.0 / ps.width(), 320.0 / ps.height());
        try { img = s.doc->render(p.page, sc, QRect(0, 0, int(ps.width() * sc), int(ps.height() * sc))); } catch (const PdfError &) {}
    }
    if (p.rot % 360) img = img.transformed(QTransform().rotate(p.rot));
    QPixmap pm(grid_->iconSize() * 2);
    pm.fill(Qt::transparent);
    if (!img.isNull()) {
        QPainter painter(&pm);
        const int cap = 40;      // caption strip under the page: file name and page number
        QImage scaled = img.scaled(pm.size() - QSize(4, 4 + cap), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect r(QPoint((pm.width() - scaled.width()) / 2, (pm.height() - cap - scaled.height()) / 2), scaled.size());
        QFont f = font();
        f.setPixelSize(22);
        painter.setFont(f);
        painter.setPen(palette().color(QPalette::Text));
        QString name = QFileInfo(s.path).completeBaseName();
        QString label = s.image ? name : QString("%1 · %2").arg(name.size() > 12 ? name.left(11) + "…" : name).arg(p.page + 1);
        painter.drawText(QRect(0, pm.height() - cap, pm.width(), cap), Qt::AlignCenter, painter.fontMetrics().elidedText(label, Qt::ElideMiddle, pm.width() - 8));
        painter.drawImage(r, scaled);
        painter.setPen(QPen(QColor(120, 120, 120), 2));
        painter.drawRect(r.adjusted(0, 0, -1, -1));
        painter.fillRect(QRect(r.left(), r.top(), r.width(), 8), kTags[p.src % 6]);    // which file the page comes from
    }
    pm.setDevicePixelRatio(2);
    thumbs_.insert(key, pm);
    return pm;
}

void CreatorWindow::thumbStep()
{
    int done = 0;
    for (int i = 0; i < grid_->count() && done < 4; ++i) {
        QListWidgetItem *it = grid_->item(i);
        if (!it->icon().isNull()) continue;
        it->setIcon(QIcon(thumbnail(pages_.value(it->data(Qt::UserRole).toInt()))));
        ++done;
    }
    if (done == 0) thumbTimer_.stop();
}

void CreatorWindow::removeSelectedFiles()
{
    QVector<int> ids;
    for (QListWidgetItem *it : files_->selectedItems()) ids.push_back(it->data(Qt::UserRole).toInt());
    if (ids.isEmpty()) return;
    for (int id : std::as_const(ids)) {
        order_.removeAll(id);
        sources_.remove(id);
    }
    for (int i = pages_.size() - 1; i >= 0; --i)
        if (ids.contains(pages_[i].src)) pages_.removeAt(i);
    refreshFiles();
    regenerate();
}

void CreatorWindow::removeSelectedPages()
{
    if (mode_ != ArrangePages) return;
    QVector<int> rows;
    for (QListWidgetItem *it : grid_->selectedItems()) rows.push_back(grid_->row(it));
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : std::as_const(rows)) pages_.removeAt(r);
    refreshGrid();
    updateControls();
}

void CreatorWindow::rotateSelected(int delta)
{
    if (mode_ != ArrangePages) return;
    QVector<int> rows;
    for (QListWidgetItem *it : grid_->selectedItems()) rows.push_back(grid_->row(it));
    for (int r : std::as_const(rows)) pages_[r].rot = (((pages_[r].rot + delta) % 360) + 360) % 360;
    refreshGrid();
    for (int r : std::as_const(rows)) grid_->item(r)->setSelected(true);
}

QVector<AssemblyItem> CreatorWindow::items() const
{
    QVector<AssemblyItem> out;
    for (const PageRef &p : pages_) {
        const Source &s = sources_[p.src];
        out.push_back(AssemblyItem{s.path, p.page, p.rot, s.image});
    }
    return out;
}

bool CreatorWindow::build(const QString &outPath)
{
    for (const Source &s : std::as_const(sources_))
        if (QFileInfo(s.path) == QFileInfo(outPath)) {
            QMessageBox::warning(this, windowTitle(), tr("Choose a different name: the result cannot overwrite one of its source files."));
            return false;
        }
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        Document::assemble(items(), outPath);
        QApplication::restoreOverrideCursor();
        return true;
    } catch (const PdfError &err) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, windowTitle(), err.message());
        return false;
    }
}

void CreatorWindow::createPdf()
{
    if (pages_.isEmpty()) return;
    QString dir = QFileInfo(sources_[order_.first()].path).absolutePath();
    QString out = QFileDialog::getSaveFileName(this, tr("Save the new PDF"), dir + "/combined.pdf", tr("PDF (*.pdf)"));
    if (out.isEmpty()) return;
    if (!build(out)) return;
    QSettings().setValue("creator/openAfter", openAfter_->isChecked());
    hint_->setText(tr("Saved %1").arg(out));
    if (openAfter_->isChecked()) emit openInReader(out);
}
