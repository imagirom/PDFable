// Dialogs: handwritten signature pad + library, digital signing, digital ID creation.
#pragma once
#include "Document.h"
#ifdef PDFABLE_SIGNING
#include "Signer.h"
#endif
#include "Stroke.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QWidget>

class QListWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QRadioButton;

namespace SignatureLibrary {
QVector<ink::InkData> load();
void save(const QVector<ink::InkData> &sigs);
QList<QByteArray> trustedCerts();
void addTrustedCert(const QByteArray &der);
}

class SignaturePad : public QWidget {
    Q_OBJECT
public:
    explicit SignaturePad(QWidget *parent = nullptr);
    ink::InkData ink() const { return data_; }
    bool isEmpty() const { return data_.strokes.isEmpty(); }
    void clear();
    void feed(int phase, QPointF pos, float pressure);   // also used by tests
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void tabletEvent(QTabletEvent *e) override;
private:
    ink::InkData data_;
    ink::StrokeBuilder builder_;
    bool down_ = false, pen_ = false;
    QElapsedTimer clock_;
};

class HandSignatureDialog : public QDialog {
    Q_OBJECT
public:
    explicit HandSignatureDialog(QWidget *parent = nullptr);
    ink::InkData result() const { return result_; }
private:
    void reload();
    QListWidget *list_;
    SignaturePad *pad_;
    QVector<ink::InkData> sigs_;
    ink::InkData result_;
};

#ifdef PDFABLE_SIGNING
class NewIdDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewIdDialog(QWidget *parent = nullptr);
    QString path() const { return path_; }
    QString password() const { return password_; }
private:
    QString path_, password_;
};

class DigitalSignDialog : public QDialog {
    Q_OBJECT
public:
    explicit DigitalSignDialog(bool alreadySigned, QWidget *parent = nullptr);
    ink::SignParams params() const { return params_; }
    bool visibleSignature() const { return visible_; }
private:
    void accept() override;
    QLineEdit *idPath_, *password_, *location_, *contact_;
    QComboBox *reason_, *type_, *tsa_, *graphic_;
    QRadioButton *visibleBtn_;
    QCheckBox *useGraphic_;
    QVector<ink::InkData> sigs_;
    ink::SignParams params_;
    bool visible_ = true;
};
#endif
