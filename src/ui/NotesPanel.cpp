#include "NotesPanel.h"
#include "PdfView.h"

#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>

using namespace ink;

static const int kSide = 10;    // card margin left/right inside the panel
static const int kGapY = 8;     // minimum gap between cards

// ------------------------------------------------------------------ NoteCard
NoteCard::NoteCard(const MarkupNote &n, const QString &excerpt, QWidget *parent) : QFrame(parent), note_(n)
{
    setCursor(Qt::PointingHandCursor);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 7, 9, 8);
    lay->setSpacing(4);
    excerpt_ = new QLabel(this);
    excerpt_->setWordWrap(true);
    QFont f = excerpt_->font();
    f.setItalic(true);
    f.setPointSizeF(f.pointSizeF() * 0.9);
    excerpt_->setFont(f);
    excerpt_->setForegroundRole(QPalette::PlaceholderText);
    QString ex = excerpt.size() > 140 ? excerpt.left(137) + QStringLiteral("…") : excerpt;
    excerpt_->setText(ex.isEmpty() ? QString() : QStringLiteral("“%1”").arg(ex));
    excerpt_->setVisible(!ex.isEmpty());
    text_ = new QLabel(this);
    text_->setWordWrap(true);
    text_->setTextInteractionFlags(Qt::NoTextInteraction);
    lay->addWidget(excerpt_);
    lay->addWidget(text_);
    setNote(n);
}

void NoteCard::setNote(const MarkupNote &n)
{
    note_ = n;
    text_->setText(n.text.toHtmlEscaped().replace('\n', "<br>"));
    if (!editor_) text_->setVisible(true);
    update();
}

void NoteCard::fitWidth(int w)
{
    setFixedWidth(w);
    int h = layout()->hasHeightForWidth() ? layout()->heightForWidth(w) : sizeHint().height();
    if (editor_) {
        int docH = int(editor_->document()->size().height()) + 8;
        editor_->setFixedHeight(qMax(48, docH));
        h = layout()->heightForWidth(w);
    }
    setFixedHeight(qMax(h, 30));
}

void NoteCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(editor_ ? note_.color.darker(140) : palette().color(QPalette::Mid), editor_ ? 1.5 : 1));
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(r, 7, 7);
    p.setPen(Qt::NoPen);
    p.setBrush(note_.color);
    p.drawRoundedRect(QRectF(r.left() + 3, r.top() + 6, 4, r.height() - 12), 2, 2);   // colour of the marking
}

void NoteCard::startEditing()
{
    if (editor_) {
        editor_->setFocus();
        return;
    }
    editor_ = new QPlainTextEdit(this);
    editor_->setPlainText(note_.text);
    editor_->setPlaceholderText(tr("Type a note…  (Ctrl+Enter to finish, Esc to cancel)"));
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_->setStyleSheet("QPlainTextEdit{background:transparent;border:none;padding:0;}");
    editor_->document()->setDocumentMargin(0);
    editor_->installEventFilter(this);
    static_cast<QVBoxLayout *>(layout())->addWidget(editor_);
    text_->hide();
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        fitWidth(width());
        emit heightChanged();
    });
    fitWidth(width());
    emit heightChanged();
    editor_->setFocus();
    editor_->moveCursor(QTextCursor::End);
    update();
}

void NoteCard::finish(bool keep)
{
    if (!editor_) return;
    QPlainTextEdit *ed = editor_;
    editor_ = nullptr;
    ed->removeEventFilter(this);
    QString text = ed->toPlainText().trimmed();
    ed->hide();
    ed->deleteLater();
    text_->show();
    fitWidth(width());
    update();
    if (keep && text != note_.text) emit committed(note_.page, note_.id, text);
    else emit cancelled(note_.id);
}

bool NoteCard::eventFilter(QObject *o, QEvent *e)
{
    if (o == editor_) {
        if (e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if (ke->key() == Qt::Key_Escape) { finish(false); return true; }
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ControlModifier)) {
                finish(true);
                return true;
            }
        }
        if (e->type() == QEvent::FocusOut && static_cast<QFocusEvent *>(e)->reason() != Qt::PopupFocusReason) {
            QPointer<NoteCard> self(this);
            QTimer::singleShot(0, this, [self] { if (self) self->finish(true); });
        }
    }
    return QFrame::eventFilter(o, e);
}

void NoteCard::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) startEditing();
    QFrame::mousePressEvent(e);
}

void NoteCard::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu m(this);
    m.addAction(tr("Edit note"), this, &NoteCard::startEditing);
    m.addAction(tr("Delete note"), this, [this] {
        if (editor_) finish(false);
        emit committed(note_.page, note_.id, QString());
    });
    m.addAction(tr("Delete marking and note"), this, [this] { emit deleteMarking(note_.page, note_.id); });
    m.exec(e->globalPos());
}

// ------------------------------------------------------------------ NotesPanel
NotesPanel::NotesPanel(std::shared_ptr<Document> doc, PdfView *view, QWidget *parent)
    : QWidget(parent), doc_(std::move(doc)), view_(view)
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setMinimumWidth(170);
    connect(view_, &PdfView::viewChanged, this, &NotesPanel::relayout);
}

int NotesPanel::noteCount() const
{
    int n = 0;
    for (const MarkupNote &m : notes_) n += m.text.isEmpty() ? 0 : 1;
    return n;
}

QString NotesPanel::excerptFor(const MarkupNote &n)
{
    auto it = excerpts_.find(n.id);
    if (it == excerpts_.end()) it = excerpts_.insert(n.id, doc_->textInQuads(n.page, n.quads));
    return it.value();
}

void NotesPanel::reload()
{
    QVector<MarkupNote> all = doc_->markups();
    notes_.clear();
    QSet<int> keep;
    for (const MarkupNote &m : std::as_const(all)) {
        if (m.text.isEmpty() && m.id != editingId_) continue;
        notes_.push_back(m);
        keep.insert(m.id);
        NoteCard *c = cards_.value(m.id);
        if (!c) {
            c = new NoteCard(m, excerptFor(m), this);
            cards_.insert(m.id, c);
            connect(c, &NoteCard::heightChanged, this, &NotesPanel::relayout);
            connect(c, &NoteCard::committed, this, [this](int page, int id, const QString &text) {
                if (editingId_ == id) editingId_ = 0;
                try {
                    view_->flushInk();
                    doc_->setAnnotContents(page, id, text);
                } catch (const PdfError &) {}
                reload();
            });
            connect(c, &NoteCard::cancelled, this, [this](int id) {
                if (editingId_ == id) editingId_ = 0;
                QTimer::singleShot(0, this, &NotesPanel::reload);
            });
            connect(c, &NoteCard::deleteMarking, this, [this](int page, int id) {
                try {
                    view_->flushInk();
                    doc_->removeAnnots(page, {id});
                } catch (const PdfError &) {}
                reload();
            });
        } else if (!c->isEditing())
            c->setNote(m);
    }
    for (auto it = cards_.begin(); it != cards_.end();) {
        if (!keep.contains(it.key()) && !it.value()->isEditing()) {
            it.value()->deleteLater();
            it = cards_.erase(it);
        } else
            ++it;
    }
    relayout();
}

void NotesPanel::editNote(int page, int id)
{
    editingId_ = id;
    reload();
    NoteCard *c = cards_.value(id);
    if (!c) return;     // the marking does not exist (any more)
    Q_UNUSED(page);
    c->show();
    c->startEditing();
    relayout();
}

QPointF NotesPanel::anchorOf(const MarkupNote &n) const
{
    // bottom-left corner of the first line of the marking: the line then runs along the bottom
    // of the text line instead of striking through the words next to the marking
    const QPolygonF &q = n.quads.isEmpty() ? QPolygonF(n.rect) : n.quads.first();
    QRectF r = q.boundingRect();
    return QPointF(r.left(), r.bottom());
}

void NotesPanel::relayout()
{
    placed_.clear();
    const int w = qMax(60, width() - 2 * kSide);
    QWidget *vp = view_->viewport();
    struct Item { qreal y; NoteCard *c; };
    QVector<Item> items;
    for (const MarkupNote &m : std::as_const(notes_)) {
        NoteCard *c = cards_.value(m.id);
        if (!c) continue;
        QPointF a = view_->pageToView(m.page, anchorOf(m));
        qreal y = mapFromGlobal(vp->mapToGlobal(a.toPoint())).y();
        items.push_back({y, c});
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.y < b.y; });
    qreal bottom = -1e9;
    for (const Item &it : std::as_const(items)) {
        it.c->fitWidth(w);
        qreal top = qMax(it.y - 14, bottom + kGapY);
        bottom = top + it.c->height();
        bool visible = bottom > -200 && top < height() + 200;
        if (visible || it.c->isEditing()) {
            it.c->move(kSide, int(top));
            it.c->show();
            placed_.push_back({it.y, it.c});
        } else
            it.c->hide();
    }
    updateLinks();
    update();
}

void NotesPanel::updateLinks()
{
    QVector<NoteLink> links;
    for (const MarkupNote &m : std::as_const(notes_))
        if (cards_.contains(m.id)) links.push_back({m.page, anchorOf(m), m.color});
    view_->setNoteLinks(links);
    view_->setNoteLinksVisible(active_ && isVisible());
}

void NotesPanel::setActive(bool on)
{
    active_ = on;
    if (on) relayout();
    view_->setNoteLinksVisible(on && isVisible());
}

void NotesPanel::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayout();
}

void NotesPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (placed_.isEmpty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect().adjusted(16, 20, -16, -20), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   notes_.isEmpty() ? tr("No notes yet.\n\nSelect text, pick a colour in the small bar and type your note.")
                                    : tr("No notes in this part of the document."));
        return;
    }
    // continue the line from the page: right edge at the marking's height, then to the card
    for (const auto &pc : std::as_const(placed_)) {
        NoteCard *c = pc.second;
        QColor col = c->note().color.darker(135);
        p.setPen(QPen(col, 1.2));
        qreal y = pc.first, cy = c->y() + 14, right = width();
        QPointF elbow(right - kSide / 2.0, y);
        p.drawLine(QPointF(right, y), elbow);
        p.drawLine(elbow, QPointF(c->geometry().right() + 1, cy));
    }
}
