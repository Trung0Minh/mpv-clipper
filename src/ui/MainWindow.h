#pragma once

#include <QMainWindow>
#include "app/LaunchPayload.h"
#include "media/MediaProbe.h"
#include "export/ExportJob.h"
#include <QElapsedTimer>
class QComboBox; class QSpinBox; class QCheckBox; class QLineEdit;
class QPushButton; class QLabel; class QProgressBar; class RangeTimeline;
class MpvPlayer; class MpvRenderWidget; class QListWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void open(const LaunchPayload &payload);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    friend class BatchTest;
    void buildUi();
    void setStatus(const QString &message, const char *color = nullptr);
    void showError(const QString &message, const QString &details = {});
    void loaded();
    void refreshTracks();
    void updateTracks();
    void updateSettings();
    void updateRange();
    void setBoundary(int boundary, double time, bool exact = true);
    void frameStep(int boundary, bool backward);
    void updateFilename();
    void validate();
    void beginExport();
    ExportSettings settings() const;
    struct Clip {
        QVariantMap controls;
        ExportSettings output;
        QString state = "Pending", error;
        bool replace = false;
    };
    QList<Clip> clips;
    QList<int> queue;
    int activeClip = -1, exportingClip = -1;
    bool batching = false, cancelBatch = false;
    QComboBox *mode;
    QListWidget *clipList;
    QWidget *clipPanel;
    QPushButton *addClip, *deleteClip, *retryClips, *cancelAll;
    void saveClip();
    void selectClip(int index);
    void refreshClipList();
    void beginBatch(bool retry = false);
    void nextExport();
    MediaProbe probe;
    ExportJob job;
    Capabilities caps;
    MediaInfo media;
    LaunchPayload pending;
    MpvPlayer *player = nullptr;
    MpvRenderWidget *preview = nullptr;
    bool renderReady = false, probing = false, playing = false, manualName = false;
    bool syncing = false, tracksInitialized = false, closing = false;
    bool wantPlaying = false, wrapping = false;
    bool stoppedAtEnd = false;
    bool playbackReady = false;
    QTimer loadTimeout;
    QTimer exportStatus;
    QElapsedTimer exportElapsed;
    int stepping = 0;
    double start = 0, end = 0, position = 0;
    QString completedPath;
    QWidget *destinationPanel = nullptr;
    QWidget *editor = nullptr, *previewContainer = nullptr, *settingsPanel = nullptr;
    RangeTimeline *timeline;
    QComboBox *format, *resolution, *quality, *fps, *audio, *subtitle, *subtitleMode;
    QComboBox *gifWidth, *gifFps, *gifQuality;
    QSpinBox *customWidth, *customHeight, *gifCustom;
    QCheckBox *aspectLock, *loop;
    QLineEdit *startText, *endText, *filename, *directory;
    QPushButton *play, *exportButton, *cancelButton, *openFile, *openFolder, *copyPath;
    QList<QPushButton *> stepButtons;
    QLabel *current, *status, *subtitleHint, *statusDot;
    QProgressBar *progress;
};
