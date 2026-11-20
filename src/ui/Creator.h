// PDF Creator: drop PDFs and images, then join them as whole files, arrange single pages by
// hand, or interleave them page by page (zipper, e.g. for front and back side scans).
#pragma once
#include "Document.h"
#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <QTimer>
#include <memory>

class QListWidget;
class QCheckBox;
class QLabel;
class QRadioButton;
class QPushButton;

class CreatorWindow : public QMainWindow {
    Q_OBJECT
public:
    enum Mode { WholeFiles = 0, ArrangePages = 1, Zipper = 2 };
    explicit CreatorWindow(QWidget *parent = nullptr);
    static CreatorWindow *showCreator();     // one creator window per process

    void addFiles(const QStringList &paths);
    void setMode(Mode m);
    void setReverseSecond(bool on);
    int pageCount() const { return pages_.size(); }
    QVector<ink::AssemblyItem> items() const;
    void movePage(int from, int to);         // ArrangePages mode
    bool build(const QString &outPath);

signals:
    void openInReader(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    struct Source {
        QString path;
        bool image = false;
        int pages = 1;
        std::shared_ptr<ink::Document> doc;
    };
    struct PageRef { int src; int page; int rot; };
    void regenerate();
    void refreshFiles();
    void refreshGrid();
    void syncFromGrid();
    void thumbStep();
    QPixmap thumbnail(const PageRef &p);
    void removeSelectedFiles();
    void removeSelectedPages();
    void rotateSelected(int delta);
    void createPdf();
    void updateControls();

    QHash<int, Source> sources_;
    QVector<int> order_;       // source ids in list order
    QVector<PageRef> pages_;   // the resulting page sequence
    int nextId_ = 0;
    Mode mode_ = WholeFiles;
    bool rebuilding_ = false;
    QHash<QString, QPixmap> thumbs_;
    QTimer thumbTimer_;

    QListWidget *files_, *grid_;
    QRadioButton *modeBtn_[3];
    QCheckBox *reverse_, *openAfter_;
    QLabel *hint_;
    QPushButton *rotL_, *rotR_, *delPage_, *create_;
};
