#include "Dialogs.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabletEvent>
#include <QVBoxLayout>

using namespace ink;

// ------------------------------------------------------------------ library
static QString libraryFile()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/signatures.json";
}

QVector<InkData> SignatureLibrary::load()
{
    QVector<InkData> out;
    QFile f(libraryFile());
    if (!f.open(QIODevice::ReadOnly)) return out;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) out.push_back(inkFromJson(v.toObject()));
    return out;
}

void SignatureLibrary::save(const QVector<InkData> &sigs)
{
    QJsonArray arr;
    for (const InkData &d : sigs) arr.append(inkToJson(d));
    QFile f(libraryFile());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QList<QByteArray> SignatureLibrary::trustedCerts()
{
    QList<QByteArray> out;
    for (const QString &s : QSettings().value("trustedCerts").toStringList()) out.push_back(QByteArray::fromBase64(s.toLatin1()));
    return out;
}

void SignatureLibrary::addTrustedCert(const QByteArray &der)
{
    QSettings st;
    QStringList l = st.value("trustedCerts").toStringList();
    QString b = QString::fromLatin1(der.toBase64());
    if (!l.contains(b)) l.push_back(b);
    st.setValue("trustedCerts", l);
}

// ------------------------------------------------------------------ pad
SignaturePad::SignaturePad(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(460, 170);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
    data_.color = QColor(0x10, 0x20, 0x70);
    data_.width = 2.4f;
    clock_.start();
}

void SignaturePad::clear()
{
    data_.strokes.clear();
    update();
}

void SignaturePad::feed(int phase, QPointF pos, float pressure)
{
    if (phase == 0) {
        down_ = true;
        builder_.begin(pos, pressure, clock_.elapsed(), data_.width, true, 1.0);
    } else if (phase == 1 && down_) {
        builder_.add(pos, pressure, clock_.elapsed());
    } else if (phase == 2 && down_) {
        down_ = false;
        data_.strokes.push_back(builder_.result());
    }
    update();
}

void SignaturePad::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    p.setPen(QPen(QColor(190, 190, 190), 1, Qt::DashLine));
    p.drawLine(20, height() - 45, width() - 20, height() - 45);
    p.setPen(QColor(150, 150, 150));
    if (isEmpty() && !down_) p.drawText(rect().adjusted(0, 0, 0, -60), Qt::AlignCenter, tr("Sign here with the pen or the mouse"));
    paintInk(p, data_);
    if (down_) paintInk(p, InkStroke{builder_.points(), builder_.widths()}, data_.color, data_.width, false);
    p.setPen(QColor(160, 160, 160));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void SignaturePad::mousePressEvent(QMouseEvent *e) { if (!pen_) feed(0, e->position(), -1); }
void SignaturePad::mouseMoveEvent(QMouseEvent *e) { if (!pen_) feed(1, e->position(), -1); }
void SignaturePad::mouseReleaseEvent(QMouseEvent *e) { if (!pen_) feed(2, e->position(), -1); }
void SignaturePad::tabletEvent(QTabletEvent *e)
{
    if (e->type() == QEvent::TabletPress) { pen_ = true; feed(0, e->position(), float(e->pressure())); }
    else if (e->type() == QEvent::TabletMove) feed(1, e->position(), float(e->pressure()));
    else if (e->type() == QEvent::TabletRelease) { feed(2, e->position(), 0); pen_ = false; }
    e->accept();
}

// ------------------------------------------------------------------ handwritten signature dialog
HandSignatureDialog::HandSignatureDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Handwritten signature"));
    auto *lay = new QHBoxLayout(this);
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Saved signatures")));
    list_ = new QListWidget;
    list_->setIconSize(QSize(200, 70));
    list_->setFixedWidth(240);
    left->addWidget(list_);
    auto *del = new QPushButton(tr("Delete selected"));
    left->addWidget(del);
    lay->addLayout(left);

    auto *right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Draw a new signature")));
    pad_ = new SignaturePad;
    right->addWidget(pad_, 1);
    auto *row = new QHBoxLayout;
    auto *clear = new QPushButton(tr("Clear"));
    auto *keep = new QCheckBox(tr("Save to my signatures"));
    keep->setChecked(true);
    row->addWidget(clear);
    row->addWidget(keep);
    row->addStretch();
    right->addLayout(row);
    auto *bb = new QDialogButtonBox;
    QPushButton *place = bb->addButton(tr("Place on page"), QDialogButtonBox::AcceptRole);
    bb->addButton(QDialogButtonBox::Cancel);
    right->addWidget(bb);
    lay->addLayout(right, 1);

    connect(clear, &QPushButton::clicked, pad_, &SignaturePad::clear);
    connect(del, &QPushButton::clicked, this, [this] {
        int r = list_->currentRow();
        if (r < 0) return;
        sigs_.removeAt(r);
        SignatureLibrary::save(sigs_);
        reload();
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] {
        result_ = sigs_.value(list_->currentRow());
        QDialog::accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(place, &QPushButton::clicked, this, [this, keep] {
        if (!pad_->isEmpty()) {
            result_ = pad_->ink();
            if (keep->isChecked()) {
                sigs_.push_back(result_);
                SignatureLibrary::save(sigs_);
            }
        } else if (list_->currentRow() >= 0)
            result_ = sigs_.value(list_->currentRow());
        else {
            QMessageBox::information(this, windowTitle(), tr("Draw a signature or pick a saved one."));
            return;
        }
        QDialog::accept();
    });
    reload();
}

void HandSignatureDialog::reload()
{
    sigs_ = SignatureLibrary::load();
    list_->clear();
    for (const InkData &d : std::as_const(sigs_)) {
        QImage img = inkImage(d, 400, Qt::white);
        list_->addItem(new QListWidgetItem(QIcon(QPixmap::fromImage(img)), QString()));
    }
    if (list_->count()) list_->setCurrentRow(0);
}

#ifdef PDFABLE_SIGNING
// ------------------------------------------------------------------ new digital ID
NewIdDialog::NewIdDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Create a self-signed digital ID"));
    auto *form = new QFormLayout(this);
    auto *name = new QLineEdit, *org = new QLineEdit, *email = new QLineEdit, *country = new QLineEdit("DE");
    auto *pw = new QLineEdit, *pw2 = new QLineEdit;
    pw->setEchoMode(QLineEdit::Password);
    pw2->setEchoMode(QLineEdit::Password);
    country->setMaxLength(2);
    auto *years = new QSpinBox;
    years->setRange(1, 30);
    years->setValue(5);
    auto *note = new QLabel(tr("A self-signed ID proves that a document was not changed after signing. Recipients must "
                               "trust your certificate manually; for legally qualified signatures use an ID issued by a "
                               "certificate authority (.p12/.pfx)."));
    note->setWordWrap(true);
    form->addRow(note);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Organisation"), org);
    form->addRow(tr("E-mail"), email);
    form->addRow(tr("Country (2 letters)"), country);
    form->addRow(tr("Valid for (years)"), years);
    form->addRow(tr("Password"), pw);
    form->addRow(tr("Repeat password"), pw2);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, this, [=] {
        if (name->text().trimmed().isEmpty() || pw->text().size() < 4 || pw->text() != pw2->text()) {
            QMessageBox::warning(this, windowTitle(), tr("Enter a name and two matching passwords (at least 4 characters)."));
            return;
        }
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString file = QFileDialog::getSaveFileName(this, tr("Save digital ID"), dir + "/" + name->text().trimmed().replace(' ', '_') + ".p12",
                                                    tr("Digital ID (*.p12 *.pfx)"));
        if (file.isEmpty()) return;
        QString err;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool ok = createSelfSignedId(file, pw->text(), name->text(), org->text(), email->text(), country->text(), years->value(), &err);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::critical(this, windowTitle(), err);
            return;
        }
        path_ = file;
        password_ = pw->text();
        QDialog::accept();
    });
}

// ------------------------------------------------------------------ digital signature dialog
DigitalSignDialog::DigitalSignDialog(bool alreadySigned, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Sign with a digital ID"));
    QSettings st;
    auto *lay = new QVBoxLayout(this);

    auto *idBox = new QGroupBox(tr("Digital ID (PKCS#12)"));
    auto *idForm = new QFormLayout(idBox);
    idPath_ = new QLineEdit(st.value("sign/idPath").toString());
    auto *browse = new QPushButton(tr("Browse…"));
    auto *create = new QPushButton(tr("Create new…"));
    auto *row = new QHBoxLayout;
    row->addWidget(idPath_, 1);
    row->addWidget(browse);
    row->addWidget(create);
    idForm->addRow(tr("ID file"), row);
    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    idForm->addRow(tr("Password"), password_);
    lay->addWidget(idBox);

    auto *box = new QGroupBox(tr("Signature"));
    auto *form = new QFormLayout(box);
    type_ = new QComboBox;
    type_->addItems({tr("Approval signature"), tr("Certify: no changes allowed"), tr("Certify: form filling and signing allowed"),
                     tr("Certify: commenting, form filling and signing allowed")});
    if (alreadySigned) {
        type_->setEnabled(false);
        type_->setToolTip(tr("A document can only be certified by its first signature."));
    }
    form->addRow(tr("Type"), type_);
    reason_ = new QComboBox;
    reason_->setEditable(true);
    reason_->addItems({QString(), tr("I approve this document"), tr("I am the author of this document"), tr("I have reviewed this document")});
    form->addRow(tr("Reason"), reason_);
    location_ = new QLineEdit(st.value("sign/location").toString());
    form->addRow(tr("Location"), location_);
    contact_ = new QLineEdit(st.value("sign/contact").toString());
    form->addRow(tr("Contact"), contact_);
    tsa_ = new QComboBox;
    tsa_->setEditable(true);
    tsa_->addItems({QString(), "http://timestamp.digicert.com", "http://timestamp.sectigo.com"});
    tsa_->setCurrentText(st.value("sign/tsa").toString());
    tsa_->setToolTip(tr("Optional RFC 3161 timestamp authority. With a timestamp the signing time is proven by a third party (PAdES B-T)."));
    form->addRow(tr("Timestamp server"), tsa_);
    lay->addWidget(box);

    auto *ap = new QGroupBox(tr("Appearance"));
    auto *apLay = new QVBoxLayout(ap);
    visibleBtn_ = new QRadioButton(tr("Visible: I will draw a box on the page"));
    auto *invisible = new QRadioButton(tr("Invisible"));
    visibleBtn_->setChecked(true);
    apLay->addWidget(visibleBtn_);
    auto *grow = new QHBoxLayout;
    useGraphic_ = new QCheckBox(tr("Show my handwritten signature"));
    graphic_ = new QComboBox;
    graphic_->setIconSize(QSize(120, 40));
    sigs_ = SignatureLibrary::load();
    for (int i = 0; i < sigs_.size(); ++i)
        graphic_->addItem(QIcon(QPixmap::fromImage(inkImage(sigs_[i], 240, Qt::white))), tr("Signature %1").arg(i + 1));
    useGraphic_->setEnabled(!sigs_.isEmpty());
    graphic_->setEnabled(!sigs_.isEmpty());
    grow->addSpacing(20);
    grow->addWidget(useGraphic_);
    grow->addWidget(graphic_, 1);
    apLay->addLayout(grow);
    apLay->addWidget(invisible);
    lay->addWidget(ap);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Continue"));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &DigitalSignDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getOpenFileName(this, tr("Digital ID"), idPath_->text(), tr("Digital ID (*.p12 *.pfx);;All files (*)"));
        if (!f.isEmpty()) idPath_->setText(f);
    });
    connect(create, &QPushButton::clicked, this, [this] {
        NewIdDialog d(this);
        if (d.exec() == QDialog::Accepted) {
            idPath_->setText(d.path());
            password_->setText(d.password());
        }
    });
}

void DigitalSignDialog::accept()
{
    QString err;
    auto id = loadPkcs12(idPath_->text(), password_->text(), &err);
    if (!id) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot use this digital ID: %1").arg(err));
        return;
    }
    if (id->notAfter.isValid() && id->notAfter < QDateTime::currentDateTimeUtc() &&
        QMessageBox::question(this, windowTitle(), tr("This digital ID expired on %1. Sign anyway?").arg(id->notAfter.toString(Qt::ISODate))) != QMessageBox::Yes)
        return;
    params_ = SignParams();
    params_.id = id;
    params_.reason = reason_->currentText().trimmed();
    params_.location = location_->text().trimmed();
    params_.contact = contact_->text().trimmed();
    params_.tsaUrl = tsa_->currentText().trimmed();
    params_.certifyLevel = type_->isEnabled() ? type_->currentIndex() : 0;
    visible_ = visibleBtn_->isChecked();
    if (visible_ && useGraphic_->isChecked() && graphic_->currentIndex() >= 0)
        params_.graphic = inkImage(sigs_[graphic_->currentIndex()], 600);
    QSettings st;
    st.setValue("sign/idPath", idPath_->text());
    st.setValue("sign/location", params_.location);
    st.setValue("sign/contact", params_.contact);
    st.setValue("sign/tsa", params_.tsaUrl);
    QDialog::accept();
}
#endif
