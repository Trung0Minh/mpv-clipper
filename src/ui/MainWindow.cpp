#include "MainWindow.h"
#include "Theme.h"
#include "SettingsComboBox.h"
#include "RangeTimeline.h"
#include "media/MpvPlayer.h"
#include "media/MpvRenderWidget.h"
#include "media/Timecode.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <QFutureWatcher>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    applyTheme();
    setWindowTitle("mpv Clipper"); resize(1100, 720); setMinimumSize(900, 600);
    buildUi();
    exportStatus.setInterval(500);
    connect(&exportStatus, &QTimer::timeout, this, [this] {
        setStatus(QString("Exporting %1 (%2 s elapsed)")
            .arg(QFileInfo(completedPath).fileName()).arg(exportElapsed.elapsed() / 1000));
    });
    loadTimeout.setSingleShot(true);
    connect(&loadTimeout, &QTimer::timeout, this, [this] {
        probing = false; playbackReady = false; validate();
        showError("The video preview could not start. Check OpenGL support and the source file.");
    });
    QSettings config;
    restoreGeometry(config.value("geometry").toByteArray());
    if (isFullScreen() || isMaximized()) setWindowState(Qt::WindowNoState);
    loop->setChecked(config.value("loop", true).toBool());
    directory->setText(config.value("defaultDirectory", config.value("lastDirectory")).toString());
    syncing = true;
    format->setCurrentText(config.value("format", "MP4").toString());
    quality->setCurrentIndex(config.value("quality", 0).toInt());
    resolution->setCurrentIndex(config.value("resolution", 0).toInt());
    fps->setCurrentIndex(config.value("fps", 0).toInt());
    gifWidth->setCurrentIndex(config.value("gifWidth", 4).toInt());
    gifFps->setCurrentText(config.value("gifFps", "15").toString());
    gifQuality->setCurrentIndex(config.value("gifQuality", 0).toInt());
    syncing = false;
    auto *watcher = new QFutureWatcher<Capabilities>(this);
    connect(watcher, &QFutureWatcher<Capabilities>::finished, this, [this, watcher] {
        caps = watcher->result(); watcher->deleteLater();
        auto *model = qobject_cast<QStandardItemModel *>(format->model());
        for (int i = 0; i < format->count(); ++i) model->item(i)->setEnabled(caps.formatSupported(format->itemText(i).toLower()));
        if (!caps.formatSupported(format->currentText().toLower())) {
            for (int i = 0; i < format->count(); ++i) if (model->item(i)->isEnabled()) { format->setCurrentIndex(i); break; }
        }
        updateSettings();
    });
    watcher->setFuture(QtConcurrent::run(&Capabilities::detect));
    connect(&probe, &MediaProbe::ready, this, [this](MediaInfo info) {
        media = info; start = 0; end = media.duration; position = std::clamp(pending.time, 0.0, end);
        syncing = true; customWidth->setValue(media.width); customHeight->setValue(media.height); syncing = false;
        if (directory->text().isEmpty()) {
            QString folder = QFileInfo(media.source).absolutePath() + "/Clips";
            if (!QDir().mkpath(folder)) folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
            directory->setText(folder);
        }
        if (renderReady) player->load(media.source);
        updateRange(); updateFilename();
    });
    connect(&probe, &MediaProbe::failed, this, [this](QString error) { loadTimeout.stop(); probing = false; validate(); showError(error); });
    connect(&job, &ExportJob::progress, progress, &QProgressBar::setValue);
    connect(&job, &ExportJob::progress, this, [this](int percent) {
        if (batching && exportingClip >= 0) {
            clips[exportingClip].state = QString("Exporting %1%").arg(percent); refreshClipList();
        }
    });
    connect(&job, &ExportJob::finished, this, [this](bool success, QString message, QString details) {
        exportStatus.stop(); progress->hide();
        if (batching) {
            auto &clip = clips[exportingClip];
            clip.state = success ? "Done" : message == "Export canceled." ? "Cancelled" : "Failed";
            clip.error = details.isEmpty() ? message : details;
            refreshClipList(); QTimer::singleShot(0, this, &MainWindow::nextExport); return;
        }
        editor->setEnabled(true); destinationPanel->setEnabled(true); mode->setEnabled(true); cancelButton->setEnabled(false);
        openFile->setVisible(success); openFolder->setVisible(success); copyPath->setVisible(success);
        validate(); setStatus(message, success ? "#4ade80" : "#f87171");
        if (!success && !details.isEmpty() && !closing && message != "Export canceled.") showError(message, details);
        if (closing) close();
    });
    updateSettings(); validate();
}
MainWindow::~MainWindow()
{
    // The render context must be freed before its mpv handle.
    delete preview; preview = nullptr;
    delete player; player = nullptr;
}

void MainWindow::buildUi()
{
    auto *root = new QWidget(this); auto *layout = new QVBoxLayout(root); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(10);
    setCentralWidget(root);
    editor = new QWidget(root); auto *body = new QVBoxLayout(editor); body->setContentsMargins(0, 0, 0, 0); body->setSpacing(10);
    layout->addWidget(editor, 1);
    mode = new SettingsComboBox; mode->addItems({"Single Clip", "Multi Clip"});
    mode->setAccessibleName("Clipping mode");
    auto *modeHeader = new QWidget; auto *modeRow = new QHBoxLayout(modeHeader);
    modeRow->setContentsMargins(8, 0, 8, 0); modeRow->addWidget(new QLabel("Mode:")); modeRow->addWidget(mode);
    menuBar()->setCornerWidget(modeHeader, Qt::TopRightCorner);
    clipPanel = new QWidget; clipPanel->setObjectName("clipPanel"); auto *clipLayout = new QVBoxLayout(clipPanel);
    clipLayout->setContentsMargins(10, 8, 10, 8);
    clipList = new QListWidget;
    clipList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    clipList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    clipList->setAccessibleName("Clips");
    auto *clipActions = new QHBoxLayout;
    auto *queueTitle = new QLabel("Batch Queue"); queueTitle->setObjectName("sectionTitle");
    clipLayout->addWidget(queueTitle);
    clipActions->setSpacing(4);
    addClip = new QPushButton("Add Clip"); deleteClip = new QPushButton("Delete Clip");
    retryClips = new QPushButton("Retry Failed");
    for (auto *button : {addClip, deleteClip, retryClips}) clipActions->addWidget(button);
    clipLayout->addLayout(clipActions); clipLayout->addWidget(clipList); clipPanel->hide();
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        saveClip(); clipPanel->setVisible(index == 1);
        if (index == 1 && clips.isEmpty() && playbackReady) {
            clips.append(Clip{}); activeClip = 0; saveClip();
        }
        refreshClipList(); validate();
    });
    connect(addClip, &QPushButton::clicked, this, [this] {
        if (!playbackReady || stepping) return;
        saveClip(); Clip clip; clip.controls = clips[activeClip].controls;
        clip.output = clips[activeClip].output;
        clip.controls["manual"] = false;
        clips.append(clip); selectClip(clips.size() - 1);
        manualName = false; updateFilename(); saveClip(); refreshClipList();
    });
    connect(deleteClip, &QPushButton::clicked, this, [this] {
        if (clips.size() < 2 || stepping) return;
        clips.removeAt(activeClip); activeClip = -1; selectClip(0);
    });
    connect(clipList, &QListWidget::currentRowChanged, this, &MainWindow::selectClip);
    connect(clipList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (activeClip >= 0 && clips[activeClip].state == "Done")
            QDesktopServices::openUrl(QUrl::fromLocalFile(clips[activeClip].output.destination));
    });
    connect(retryClips, &QPushButton::clicked, this, [this] { beginBatch(true); });
    auto *top = new QGridLayout;
    top->setColumnStretch(0, 1); top->setRowStretch(0, 1);
    top->setVerticalSpacing(8); top->setHorizontalSpacing(14);
    previewContainer = new QWidget(editor); previewContainer->setObjectName("previewContainer");
    previewContainer->setAttribute(Qt::WA_StyledBackground); auto *previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    auto *welcome = new QLabel("Open a local video to begin", previewContainer);
    welcome->setAlignment(Qt::AlignCenter); previewLayout->addWidget(welcome);
    previewContainer->setMinimumSize(320, 200);
    auto *videoColumn = new QVBoxLayout; videoColumn->setSpacing(8);
    videoColumn->addWidget(previewContainer, 1); top->addLayout(videoColumn, 0, 0);
    settingsPanel = new QWidget(editor); settingsPanel->setObjectName("settingsPanel");
    settingsPanel->setAttribute(Qt::WA_StyledBackground);
    auto *form = new QFormLayout(settingsPanel);
    form->setContentsMargins(14, 12, 14, 12); form->setVerticalSpacing(8);
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    auto *settingsTitle = new QLabel("Export Settings"); settingsTitle->setObjectName("sectionTitle"); form->addRow(settingsTitle);
    auto combo = [form](const QString &label, const QStringList &items) {
        auto *widget = new SettingsComboBox; widget->setMaxVisibleItems(8); widget->addItems(items); widget->setAccessibleName(label); form->addRow(label, widget); return widget;
    };
    format = combo("Format", {"MP4", "MKV", "WebM", "GIF"});
    form->setRowVisible(format, false);
    auto *formats = new QHBoxLayout; formats->setSpacing(4);
    auto *formatGroup = new QButtonGroup(this);
    for (int i = 0; i < format->count(); ++i) {
        auto *button = new QPushButton(format->itemText(i)); button->setCheckable(true);
        button->setProperty("formatPill", true); formatGroup->addButton(button, i); formats->addWidget(button);
    }
    formatGroup->button(0)->setChecked(true); form->addRow("Format", formats);
    connect(formatGroup, &QButtonGroup::idClicked, format, &QComboBox::setCurrentIndex);
    connect(format, &QComboBox::currentIndexChanged, this, [formatGroup](int index) {
        if (auto *button = formatGroup->button(index)) button->setChecked(true);
    });
    connect(format->model(), &QAbstractItemModel::dataChanged, this, [this, formatGroup] {
        for (int i = 0; i < format->count(); ++i)
            formatGroup->button(i)->setEnabled(format->model()->flags(format->model()->index(i, 0)).testFlag(Qt::ItemIsEnabled));
    });
    resolution = combo("Resolution", {"Original", "1080p", "720p", "480p", "Custom"});
    customWidth = new QSpinBox; customHeight = new QSpinBox;
    for (auto *spin : {customWidth, customHeight}) { spin->setRange(2, 16384); spin->setSingleStep(2); }
    form->addRow("Width", customWidth); form->addRow("Height", customHeight);
    aspectLock = new QCheckBox("Lock aspect ratio"); aspectLock->setChecked(true); form->addRow(aspectLock);
    quality = combo("Quality", {"High", "Medium", "Low"});
    fps = combo("FPS", {"Original", "60", "30", "24"});
    gifWidth = combo("GIF width", {"Original", "1280", "960", "720", "640", "480", "Custom"});
    gifCustom = new QSpinBox; gifCustom->setRange(2, 16384); gifCustom->setValue(640); form->addRow("Custom GIF width", gifCustom);
    gifFps = combo("GIF FPS", {"Original", "30", "24", "20", "15", "12", "10"});
    gifQuality = combo("GIF quality", {"High", "Medium", "Low"});
    audio = combo("Audio", {}); audio->addItem("None", -1);
    subtitleMode = combo("Subtitles", {"None", "Hardsub", "Softsub"});
    subtitleMode->setToolTip("Hardsub renders subtitles permanently. Softsub keeps a selectable track.");
    subtitle = combo("Subtitle track", {});
    auto pairRows = [form](QWidget *left, const QString &leftLabel, QWidget *right, const QString &rightLabel) {
        for (auto *widget : {left, right}) {
            auto row = form->takeRow(widget);
            if (row.labelItem) { delete row.labelItem->widget(); delete row.labelItem; }
            delete row.fieldItem;
        }
        auto *pair = new QWidget; pair->setObjectName("pairedSettings");
        auto *grid = new QGridLayout(pair); grid->setContentsMargins(0, 0, 0, 0);
        grid->addWidget(new QLabel(leftLabel), 0, 0); grid->addWidget(new QLabel(rightLabel), 0, 1);
        grid->addWidget(left, 1, 0); grid->addWidget(right, 1, 1);
        grid->setColumnStretch(0, 1); grid->setColumnStretch(1, 1);
        form->addRow(pair);
        return pair;
    };
    auto *qualityPair = pairRows(quality, "Quality", fps, "FPS");
    auto qualityRow = form->takeRow(qualityPair); delete qualityRow.fieldItem;
    form->insertRow(4, qualityPair);
    pairRows(subtitleMode, "Subtitles", subtitle, "Subtitle track");
    subtitleHint = new QLabel; subtitleHint->setWordWrap(true); form->addRow(subtitleHint);
    auto *settingsScroll = new QScrollArea;
    settingsScroll->setWidget(settingsPanel); settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    auto *sidebar = new QWidget;
    sidebar->setMinimumWidth(300); sidebar->setMaximumWidth(340);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0); sidebarLayout->setSpacing(8);
    sidebarLayout->addWidget(clipPanel); sidebarLayout->addWidget(settingsScroll, 1);
    top->addWidget(sidebar, 0, 1); body->addLayout(top, 1);
    timeline = new RangeTimeline;
    connect(timeline, &RangeTimeline::clipSelected, this, &MainWindow::selectClip);
    play = new QPushButton("Play"); loop = new QCheckBox("Loop"); current = new QLabel(timecode(0));
    current->setObjectName("currentTime");
    auto *zoomControls = new QWidget; zoomControls->setObjectName("timelineZoom");
    auto *zoomRow = new QHBoxLayout(zoomControls); zoomRow->setContentsMargins(0, 0, 0, 0);
    auto *zoomOut = new QPushButton("-"); auto *zoomIn = new QPushButton("+");
    zoomOut->setAccessibleName("Zoom out timeline"); zoomIn->setAccessibleName("Zoom in timeline");
    zoomOut->setProperty("zoomButton", true); zoomIn->setProperty("zoomButton", true);
    zoomOut->setFixedWidth(32); zoomIn->setFixedWidth(32);
    auto *fitClip = new QPushButton("Zoom to Clip"); auto *fitVideo = new QPushButton("Fit Video");
    for (auto *button : {zoomOut, zoomIn, fitClip, fitVideo}) zoomRow->addWidget(button);
    zoomRow->addStretch();
    connect(zoomOut, &QPushButton::clicked, this, [this] { timeline->zoom(1 / 1.5); });
    connect(zoomIn, &QPushButton::clicked, this, [this] { timeline->zoom(1.5); });
    connect(fitClip, &QPushButton::clicked, timeline, &RangeTimeline::zoomToClip);
    connect(fitVideo, &QPushButton::clicked, timeline, &RangeTimeline::fitVideo);
    auto *playback = new QHBoxLayout;
    playback->setSpacing(6);
    play->setMinimumHeight(30); loop->setMinimumHeight(30);
    playback->addWidget(play); playback->addWidget(loop); playback->addStretch();
    current->setAlignment(Qt::AlignCenter);
    auto *rangeLabel = new QLabel("Selected range"); rangeLabel->setObjectName("rangeDuration");
    auto *bounds = new QHBoxLayout;
    bounds->setSpacing(6);
    for (int boundary = 1; boundary <= 2; ++boundary) {
        auto *step = new QPushButton(boundary == 1 ? "<" : ">");
        step->setFixedWidth(32);
        step->setToolTip(boundary == 1 ? "Step selected marker or playback back one frame" : "Step selected marker or playback forward one frame");
        step->setAccessibleName(step->toolTip());
        auto *field = new QLineEdit(timecode(0)); field->setMaximumWidth(150);
        field->setAccessibleName(boundary == 1 ? "Start timestamp" : "End timestamp");
        if (boundary == 1) startText = field; else endText = field;
        field->setAlignment(Qt::AlignCenter);
        if (boundary == 1) bounds->addWidget(step);
        else bounds->addWidget(new QLabel("-"));
        bounds->addWidget(field);
        if (boundary == 2) bounds->addWidget(step);
        stepButtons << step;
        connect(step, &QPushButton::clicked, this, [this, boundary] { frameStep(timeline->selectedBoundary(), boundary == 1); });
        connect(field, &QLineEdit::editingFinished, this, [this, field, boundary] {
            auto time = parseTimecode(field->text());
            if (!time || (boundary == 1 ? *time >= end : *time <= start)) {
                field->setProperty("invalid", true); field->setStyleSheet("border: 1px solid #f87171; background-color: #241116");
                validate(); setStatus("Enter a valid timestamp with Start before End."); return;
            }
            field->setProperty("invalid", false); field->setStyleSheet({}); setBoundary(boundary, *time);
        });
    }
    auto *transport = new QGridLayout;
    transport->setContentsMargins(0, 0, 0, 0); transport->setHorizontalSpacing(6);
    transport->addLayout(playback, 0, 0);
    transport->addLayout(bounds, 0, 1);
    transport->setColumnStretch(0, 1); transport->setColumnStretch(2, 1);
    transport->setColumnMinimumWidth(2, play->sizeHint().width() + loop->sizeHint().width() + 6);
    videoColumn->setSpacing(4);
    videoColumn->addLayout(transport);
    videoColumn->addWidget(timeline);
    auto *timelineFooter = new QGridLayout;
    timelineFooter->setContentsMargins(0, 0, 0, 0); timelineFooter->setHorizontalSpacing(6);
    timelineFooter->addWidget(zoomControls, 0, 0);
    timelineFooter->addWidget(current, 0, 1, Qt::AlignCenter);
    timelineFooter->addWidget(rangeLabel, 0, 2, Qt::AlignRight);
    timelineFooter->setColumnStretch(0, 1); timelineFooter->setColumnStretch(2, 1);
    videoColumn->addLayout(timelineFooter);
    auto *dock = new QWidget; dock->setObjectName("dock"); dock->setAttribute(Qt::WA_StyledBackground);
    auto *dockLayout = new QVBoxLayout(dock); dockLayout->setContentsMargins(12, 10, 12, 10);
    layout->addWidget(dock);
    destinationPanel = new QWidget;
    auto *destination = new QFormLayout(destinationPanel); destination->setContentsMargins(0, 0, 0, 0);
    filename = new QLineEdit; directory = new QLineEdit;
    filename->setObjectName("filename"); directory->setObjectName("directory");
    destination->addRow("File name", filename);
    auto *folderRow = new QHBoxLayout; folderRow->addWidget(directory);
    auto *browse = new QPushButton("Browse..."); folderRow->addWidget(browse);
    auto *saveDefault = new QPushButton("Set default"); folderRow->addWidget(saveDefault);
    destination->addRow("Save to", folderRow); dockLayout->addWidget(destinationPanel);
    auto *actions = new QHBoxLayout;
    status = new QLabel("Open a video to begin."); status->setWordWrap(false);
    status->setObjectName("status"); status->setMinimumWidth(180); status->setMaximumWidth(340);
    actions->setSpacing(destination->horizontalSpacing());
    statusDot = new QLabel; statusDot->setObjectName("statusDot"); statusDot->setFixedSize(8, 8);
    actions->addWidget(statusDot); actions->addWidget(status);
    progress = new QProgressBar; progress->setRange(0, 100); progress->setValue(0); progress->hide();
    QSizePolicy progressPolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    progressPolicy.setRetainSizeWhenHidden(true);
    progress->setSizePolicy(progressPolicy);
    actions->addWidget(progress, 1);
    exportButton = new QPushButton("Export Clip"); exportButton->setObjectName("exportButton");
    cancelButton = new QPushButton("Cancel"); cancelButton->setObjectName("cancelButton"); cancelButton->setEnabled(false);
    cancelAll = new QPushButton("Cancel All"); cancelAll->setObjectName("cancelAll"); cancelAll->hide();
    connect(cancelAll, &QPushButton::clicked, this, [this] { cancelBatch = true; job.cancel(); });
    for (auto *button : {cancelAll, cancelButton, exportButton}) button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    actions->addWidget(cancelAll); actions->addWidget(cancelButton); actions->addWidget(exportButton); dockLayout->addLayout(actions);
    auto *results = new QHBoxLayout;
    openFile = new QPushButton("Open File"); openFolder = new QPushButton("Open Folder"); copyPath = new QPushButton("Copy Path");
    for (auto *button : {openFile, openFolder, copyPath}) { button->hide(); results->addWidget(button); }
    results->addStretch(); dockLayout->addLayout(results);
    auto *openAction = menuBar()->addAction("Open Video...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] {
        auto source = QFileDialog::getOpenFileName(this, "Open local video");
        if (!source.isEmpty()) { LaunchPayload p; p.source = source; open(p); }
    });
    auto *subtitleAction = menuBar()->addAction("Add Subtitle...");
    connect(subtitleAction, &QAction::triggered, this, [this] {
        if (!player || !playbackReady || probing || batching || job.running()) return;
        auto path = QFileDialog::getOpenFileName(this, "Add external text subtitles", {}, "Subtitles (*.ass *.ssa *.srt *.vtt)");
        if (path.isEmpty()) return;
        pending.subtitle = {{"externalFilename", path}}; tracksInitialized = false;
        player->command({"sub-add", path, "select"});
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getExistingDirectory(this, "Save clips to", directory->text());
        if (!path.isEmpty()) { directory->setText(path); QSettings().setValue("lastDirectory", path); }
    });
    connect(saveDefault, &QPushButton::clicked, this, [this] { QSettings().setValue("defaultDirectory", directory->text()); setStatus("Default output folder saved."); });
    connect(filename, &QLineEdit::textEdited, this, [this] { manualName = true; validate(); });
    connect(filename, &QLineEdit::editingFinished, this, [this] { updateFilename(); validate(); });
    connect(directory, &QLineEdit::textChanged, this, &MainWindow::validate);
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::beginExport);
    connect(cancelButton, &QPushButton::clicked, &job, &ExportJob::cancel);
    connect(openFile, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(completedPath)); });
    connect(openFolder, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(completedPath).absolutePath())); });
    connect(copyPath, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(completedPath); });
    connect(play, &QPushButton::clicked, this, [this] {
        if (!player || probing || media.duration <= 0) return;
        stoppedAtEnd = false;
        if (!playing && (position < start || position >= end - 0.005)) player->seek(start);
        wantPlaying = !playing;
        player->pause(playing);
    });
    connect(timeline, &RangeTimeline::edited, this, [this](int boundary, double value, bool final) {
        if (!player || probing) return;
        if (boundary == 3) {
            stoppedAtEnd = false;
            if (wantPlaying) value = std::clamp(value, start, end);
            player->seek(value, final); position = value; updateRange();
        }
        else setBoundary(boundary, value, final);
    });
    for (auto *box : {format, resolution, quality, fps, gifWidth, gifFps, gifQuality, subtitleMode})
        connect(box, &QComboBox::currentIndexChanged, this, &MainWindow::updateSettings);
    connect(audio, &QComboBox::currentIndexChanged, this, &MainWindow::updateTracks);
    connect(subtitle, &QComboBox::currentIndexChanged, this, &MainWindow::updateSettings);
    connect(customWidth, &QSpinBox::valueChanged, this, [this](int value) {
        if (!syncing && aspectLock->isChecked() && media.width > 0) {
            QSignalBlocker b(customHeight); customHeight->setValue(qRound(value * double(media.height) / media.width));
        }
        validate();
    });
    connect(customHeight, &QSpinBox::valueChanged, this, [this](int value) {
        if (!syncing && aspectLock->isChecked() && media.height > 0) {
            QSignalBlocker b(customWidth); customWidth->setValue(qRound(value * double(media.width) / media.height));
        }
        validate();
    });
    connect(gifCustom, &QSpinBox::valueChanged, this, &MainWindow::validate);
}

void MainWindow::setStatus(const QString &message, const char *color)
{
    if (!color) color = message == "Ready" ? "#4ade80" :
        (job.running() || batching || probing || stepping || message.startsWith("Exporting")) ? "#a855f7" : "#7e5f8f";
    statusDot->setStyleSheet(QString("background-color: %1; border-radius: 4px;").arg(color));
    statusDot->setToolTip(message); statusDot->setAccessibleName(message);
    status->setToolTip(message);
    status->setText(status->fontMetrics().elidedText(message, Qt::ElideRight, status->width()));
}

void MainWindow::showError(const QString &message, const QString &details)
{
    setStatus(message, "#f87171");
    QMessageBox box(QMessageBox::Warning, "mpv Clipper", message, QMessageBox::Ok, this);
    if (!details.isEmpty()) box.setDetailedText(details);
    box.exec();
}
void MainWindow::open(const LaunchPayload &payload)
{
    showNormal(); raise(); activateWindow();
    if (payload.source.isEmpty()) return;
    if (batching || job.running() || probing || stepping) { setStatus("Finish the current operation before opening another video."); return; }
    if (!QFileInfo(payload.source).isReadable() || !QFileInfo(payload.source).isFile()) { showError("The source video is unavailable."); return; }
    pending = payload; pending.source = QFileInfo(payload.source).absoluteFilePath();
    probing = true; playbackReady = false; tracksInitialized = false; manualName = false; wantPlaying = false; stoppedAtEnd = false;
    loadTimeout.start(45000);
    for (auto *field : {startText, endText}) { field->clearFocus(); field->setProperty("invalid", false); field->setStyleSheet({}); }
    media = {}; start = end = position = 0; updateRange();
    setStatus("Opening video..."); validate();
    if (!player) {
        try { player = new MpvPlayer(this); }
        catch (const std::exception &e) { loadTimeout.stop(); probing = false; showError(e.what()); return; }
        auto *layout = previewContainer->layout();
        while (auto *item = layout->takeAt(0)) { delete item->widget(); delete item; }
        preview = new MpvRenderWidget(player, previewContainer); layout->addWidget(preview);
        connect(preview, &MpvRenderWidget::ready, this, [this] {
            renderReady = true;
            if (media.duration > 0) player->load(media.source);
        });
        connect(preview, &MpvRenderWidget::failed, this, [this](QString error) { loadTimeout.stop(); probing = false; validate(); showError(error); });
        connect(player, &MpvPlayer::failed, this, [this](QString error) { loadTimeout.stop(); probing = false; validate(); setStatus(error, "#f87171"); });
        connect(player, &MpvPlayer::loaded, this, &MainWindow::loaded);
        connect(player, &MpvPlayer::tracksChanged, this, [this] { if (!probing && media.duration > 0) refreshTracks(); });
        connect(player, &MpvPlayer::pauseChanged, this, [this](bool paused) { playing = !paused; play->setText(paused ? "Play" : "Pause"); });
        connect(player, &MpvPlayer::timeChanged, this, [this](double time) {
            if (probing || media.duration <= 0 || timeline->dragging()) return;
            position = std::clamp(time, 0.0, media.duration);
            if (stoppedAtEnd && !wantPlaying && !stepping) position = end;
            if (position < end - 0.005) wrapping = false;
            if (wantPlaying && !wrapping && position >= end - 0.005) {
                wrapping = true;
                if (loop->isChecked()) { player->seek(start); player->pause(false); }
                else { wantPlaying = false; stoppedAtEnd = true; position = end; player->pause(true); player->seek(end); }
            }
            if (!timeline->dragging()) updateRange();
        });
        connect(player, &MpvPlayer::endReached, this, [this] {
            if (!wantPlaying || stepping) return;
            if (loop->isChecked()) { player->seek(start); player->pause(false); }
            else { wantPlaying = false; stoppedAtEnd = true; player->pause(true); position = end; updateRange(); }
        });
        connect(player, &MpvPlayer::frameStepped, this, [this](double time) {
            if (stepping == 3) { position = std::clamp(time, 0.0, media.duration); updateRange(); }
            else setBoundary(stepping, time);
        });
        connect(player, &MpvPlayer::stepFinished, this, [this] { stepping = 0; validate(); });
    } else player->pause(true);
    probe.start(pending.source);
}
void MainWindow::loaded()
{
    loadTimeout.stop(); playbackReady = true;
    if (!pending.subtitle["externalFilename"].toString().isEmpty()) {
        QString path = pending.subtitle["externalFilename"].toString();
        if (QFileInfo(path).isFile()) player->command({"sub-add", QFileInfo(path).absoluteFilePath(), "select"});
    }
    probing = false; player->pause(true); player->seek(position);
    refreshTracks(); updateRange(); updateFilename();
    clips.clear(); activeClip = -1;
    if (mode->currentIndex() == 1) { clips.append(Clip{}); activeClip = 0; saveClip(); }
    refreshClipList(); validate();
    setWindowTitle(QFileInfo(media.source).fileName() + " - mpv Clipper");
}
void MainWindow::refreshTracks()
{
    if (!player) return;
    bool pendingExternal = !pending.subtitle["externalFilename"].toString().isEmpty();
    bool initial = !tracksInitialized;
    int oldAudio = audio->currentData().toInt(), oldSub = subtitle->currentData().toInt();
    const auto list = player->tracks();
    for (const auto &entry : list) {
        auto o = entry.toObject(); QString type = o["type"].toString();
        if (type != "audio" && type != "sub") continue;
        auto &tracks = type == "audio" ? media.audio : media.subtitles;
        QString external = o["external-filename"].toString();
        if (!external.isEmpty() && type == "sub") {
            bool exists = false; for (auto &t : tracks) if (t.external == external) { t.mpvId = o["id"].toInt(); exists = true; }
            if (!exists) {
                Track t; t.type = "subtitle"; t.external = external; t.mpvId = o["id"].toInt();
                t.codec = o["codec"].toString(); t.language = o["lang"].toString(); t.title = o["title"].toString();
                if (t.codec.isEmpty()) t.codec = QFileInfo(external).suffix().toLower();
                t.ordinal = tracks.size(); tracks.append(t);
            }
        } else for (auto &t : tracks) if (t.index == o["ff-index"].toInt(-999)) t.mpvId = o["id"].toInt();
    }
    syncing = true;
    audio->clear(); audio->addItem("None", -1); subtitle->clear();
    for (int i = 0; i < media.audio.size(); ++i) audio->addItem(media.audio[i].label(), i);
    for (int i = 0; i < media.subtitles.size(); ++i) subtitle->addItem(media.subtitles[i].label(), i);
    auto match = [](const QList<Track> &tracks, const QJsonObject &hint) {
        for (int i = 0; i < tracks.size(); ++i) if (!tracks[i].external.isEmpty() && tracks[i].external == hint["externalFilename"].toString()) return i;
        for (int i = 0; i < tracks.size(); ++i) if (hint.contains("sourceIndex") && tracks[i].index == hint["sourceIndex"].toInt(-1)) return i;
        for (int i = 0; i < tracks.size(); ++i) if (!hint.isEmpty() && tracks[i].language == hint["lang"].toString() && tracks[i].title == hint["title"].toString() && tracks[i].codec == hint["codec"].toString()) return i;
        for (int i = 0; i < tracks.size(); ++i) if (tracks[i].preferred) return i;
        return tracks.isEmpty() ? -1 : 0;
    };
    int ai = initial ? (pending.fromMpv && pending.audio.isEmpty() ? -1 : match(media.audio, pending.audio)) : oldAudio;
    int si = initial ? match(media.subtitles, pending.subtitle) : oldSub;
    audio->setCurrentIndex(audio->findData(ai)); subtitle->setCurrentIndex(subtitle->findData(si));
    if (initial) subtitleMode->setCurrentText(!media.subtitles.isEmpty() && (!pending.fromMpv || !pending.subtitle.isEmpty()) ? "Hardsub" : "None");
    bool externalMatched = false;
    for (const auto &t : media.subtitles) if (t.external == pending.subtitle["externalFilename"].toString() && t.mpvId >= 0) externalMatched = true;
    tracksInitialized = !pendingExternal || externalMatched; syncing = false; updateSettings();
}
void MainWindow::updateTracks()
{
    if (syncing) return;
    if (player && !probing) {
        int a = audio->currentData().toInt(), s = subtitle->currentIndex();
        player->set("aid", format->currentText() == "GIF" || a < 0 || a >= media.audio.size() || media.audio[a].mpvId < 0 ? "no" : QString::number(media.audio[a].mpvId));
        player->set("sub-visibility", subtitleMode->currentText() == "None" ? "no" : "yes");
        if (s >= 0 && s < media.subtitles.size() && media.subtitles[s].mpvId >= 0) player->set("sid", QString::number(media.subtitles[s].mpvId));
    }
    validate();
}
void MainWindow::updateSettings()
{
    if (syncing) return;
    syncing = true;
    bool gif = format->currentText() == "GIF";
    auto *form = qobject_cast<QFormLayout *>(settingsPanel->layout());
    auto visible = [form](QWidget *widget, bool show) {
        if (widget->parentWidget()->objectName() == "pairedSettings") widget = widget->parentWidget();
        form->setRowVisible(widget, show);
    };
    for (auto *widget : {resolution, quality, fps, audio}) visible(widget, !gif);
    for (auto *widget : {gifWidth, gifFps, gifQuality}) visible(widget, gif);
    visible(gifCustom, gif && gifWidth->currentText() == "Custom");
    for (auto *widget : {customWidth, customHeight}) visible(widget, !gif && resolution->currentText() == "Custom");
    visible(aspectLock, !gif && resolution->currentText() == "Custom");
    QString mode = subtitleMode->currentText(); subtitleMode->clear(); subtitleMode->addItems({"None", "Hardsub"});
    int si = subtitle->currentIndex();
    bool text = si >= 0 && si < media.subtitles.size() && textSubtitle(media.subtitles[si].codec);
    QString codec = format->currentText() == "MP4" ? "mov_text" : format->currentText() == "WebM" ? "webvtt" : "ass";
    if (!gif && text && caps.encoders.contains(codec) && caps.encoders.contains("ass")) subtitleMode->addItem("Softsub");
    subtitleMode->setCurrentText(subtitleMode->findText(mode) >= 0 ? mode : "None");
    auto *modes = qobject_cast<QStandardItemModel *>(subtitleMode->model());
    modes->item(1)->setEnabled(text && (!caps.available && caps.error.isEmpty() ? true : caps.filters.contains("subtitles")));
    if (!modes->item(1)->isEnabled() && subtitleMode->currentText() == "Hardsub") subtitleMode->setCurrentText("None");
    subtitle->setEnabled(subtitleMode->currentText() != "None");
    subtitleHint->setText(subtitleMode->currentText() == "Softsub" && format->currentText() != "MKV" ?
        "Softsub conversion loses ASS styling. Use Hardsub to preserve its appearance." :
        (!text && !media.subtitles.isEmpty() ? "Only text subtitles are supported for export." : ""));
    syncing = false; updateFilename(); updateTracks();
}
void MainWindow::updateRange()
{
    timeline->setRange(media.duration, start, end, position);
    current->setText(timecode(position));
    if (auto *label = findChild<QLabel *>("rangeDuration")) label->setText("Range: " + timecode(end - start));
    if (!startText->hasFocus()) startText->setText(timecode(start));
    if (!endText->hasFocus()) endText->setText(timecode(end));
}
void MainWindow::setBoundary(int boundary, double time, bool exact)
{
    if (!player || media.duration <= 0 || boundary < 1 || boundary > 2) return;
    wantPlaying = false; player->pause(true);
    stoppedAtEnd = boundary == 2;
    double gap = std::min(media.duration, media.fps > 0 ? 1 / media.fps : 0.001);
    if (boundary == 1) start = std::clamp(time, 0.0, std::max(0.0, end - gap));
    else end = std::clamp(time, std::min(media.duration, start + gap), media.duration);
    position = boundary == 1 ? start : end;
    player->seek(position, exact); startText->setText(timecode(start)); endText->setText(timecode(end));
    updateRange();
    if (exact) { updateFilename(); validate(); saveClip(); refreshClipList(); }
}
void MainWindow::frameStep(int boundary, bool backward)
{
    if (!player || probing || stepping || media.duration <= 0) return;
    double boundaryTime = boundary == 1 ? start : boundary == 2 ? end : position;
    if ((backward && boundaryTime <= 0) || (!backward && boundaryTime >= media.duration)) return;
    wantPlaying = false;
    stoppedAtEnd = false;
    stepping = boundary ? boundary : 3; validate(); player->step(boundaryTime, backward);
}
void MainWindow::updateFilename()
{
    if (media.source.isEmpty()) return;
    QString stem;
    if (manualName) {
        stem = filename->text();
        QString suffix = QFileInfo(stem).suffix().toLower();
        if (QStringList{"mp4", "mkv", "webm", "gif"}.contains(suffix)) stem.chop(suffix.size() + 1);
    }
    else stem = QFileInfo(media.source).completeBaseName() + "_" + timecode(start).replace(':', '-') + "-" + timecode(end).replace(':', '-');
    if (!manualName && mode->currentIndex() == 1 && activeClip >= 0) stem += QString("_clip_%1").arg(activeClip + 1, 3, 10, QChar('0'));
    filename->setText(safeFilename(stem) + "." + format->currentText().toLower());
}
ExportSettings MainWindow::settings() const
{
    ExportSettings s; s.format = format->currentText().toLower(); s.start = start; s.end = end;
    s.audio = audio->currentData().toInt(); s.subtitle = subtitle->currentIndex(); s.subtitleMode = subtitleMode->currentText();
    s.quality = quality->currentIndex(); s.fps = fps->currentText().toInt();
    s.height = resolution->currentText().chopped(1).toInt();
    if (resolution->currentText() == "Custom") { s.customWidth = customWidth->value(); s.customHeight = customHeight->value(); }
    if (s.format == "gif") { s.fps = gifFps->currentText().toInt(); s.quality = gifQuality->currentIndex(); }
    s.gifWidth = gifWidth->currentText() == "Custom" ? gifCustom->value() : gifWidth->currentText().toInt();
    s.destination = QDir(directory->text()).absoluteFilePath(filename->text()); return s;
}
void MainWindow::validate()
{
    if (!status) return;
    mode->setEnabled(!probing && !stepping && !batching && !job.running());
    addClip->setEnabled(playbackReady && !stepping);
    deleteClip->setEnabled(clips.size() > 1 && !stepping);
    clipList->setEnabled(!stepping);
    retryClips->setEnabled(!batching && playbackReady && !stepping);
    exportButton->setText(mode->currentIndex() == 1 ? "Export All" : "Export Clip");
    bool ready = media.duration > 0 && playbackReady && !probing;
    QString error = !ready ? "Open a video to begin." : exportValidation(media, settings(), caps);
    if (filename->text().contains('/') || filename->text().contains('\\')) error = "The filename must not contain folders.";
    if (startText->property("invalid").toBool() || endText->property("invalid").toBool()) error = "Correct the invalid timestamp before exporting.";
    exportButton->setEnabled(ready && error.isEmpty() && !job.running() && !stepping);
    play->setEnabled(ready && !stepping); timeline->setEnabled(ready && !stepping);
    findChild<QWidget *>("timelineZoom")->setEnabled(ready && !stepping);
    startText->setEnabled(ready && !stepping); endText->setEnabled(ready && !stepping);
    for (auto *button : stepButtons) button->setEnabled(ready && !stepping);
    if (!job.running()) setStatus(probing ? "Opening video..." : stepping ? "Finding the adjacent frame..." : error.isEmpty() ? "Ready" : error, ready && !error.isEmpty() ? "#f87171" : nullptr);
}
void MainWindow::beginExport()
{
    if (mode->currentIndex() == 1) { beginBatch(); return; }
    updateFilename(); auto s = settings();
    QString error = exportValidation(media, s, caps);
    if (!error.isEmpty()) { showError(error); return; }
    bool replace = QFileInfo::exists(s.destination);
    if (replace) {
        QMessageBox box(QMessageBox::Question, "File already exists", "Replace the existing file?", QMessageBox::NoButton, this);
        auto *replaceButton = box.addButton("Replace", QMessageBox::DestructiveRole);
        auto *renameButton = box.addButton("Choose New Name", QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel); box.exec();
        if (box.clickedButton() == renameButton) { filename->setFocus(); filename->selectAll(); return; }
        if (box.clickedButton() != replaceButton) return;
    }
    wantPlaying = false; player->pause(true); completedPath = s.destination;
    QSettings().setValue("lastDirectory", directory->text());
    editor->setEnabled(false); destinationPanel->setEnabled(false); mode->setEnabled(false); exportButton->setEnabled(false); cancelButton->setEnabled(true);
    for (auto *button : {openFile, openFolder, copyPath}) button->hide();
    progress->show(); progress->setValue(0); setStatus("Exporting " + filename->text() + "...");
    exportElapsed.start(); exportStatus.start();
    job.start(media, s, replace);
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (batching || job.running()) {
        if (QMessageBox::question(this, "Export in progress", "Cancel the export and close?") != QMessageBox::Yes) { event->ignore(); return; }
        closing = true; cancelBatch = true; job.cancel(); event->ignore(); return;
    }
    QSettings c; c.setValue("geometry", saveGeometry()); c.setValue("loop", loop->isChecked());
    c.setValue("format", format->currentText()); c.setValue("quality", quality->currentIndex());
    c.setValue("resolution", resolution->currentIndex()); c.setValue("fps", fps->currentIndex());
    c.setValue("gifWidth", gifWidth->currentIndex()); c.setValue("gifFps", gifFps->currentText()); c.setValue("gifQuality", gifQuality->currentIndex());
    event->accept();
}

void MainWindow::saveClip()
{
    if (activeClip < 0 || activeClip >= clips.size() || batching) return;
    auto &clip = clips[activeClip];
    QVariantMap values;
    int index = 0;
    for (auto *box : {format, resolution, quality, fps, audio, subtitle, subtitleMode, gifWidth, gifFps, gifQuality})
        values[QString::number(index++)] = box->currentIndex();
    for (auto *spin : {customWidth, customHeight, gifCustom}) values[QString::number(index++)] = spin->value();
    values["aspect"] = aspectLock->isChecked(); values["loop"] = loop->isChecked();
    values["manual"] = manualName; values["filename"] = filename->text(); values["directory"] = directory->text();
    values["startText"] = startText->text(); values["endText"] = endText->text();
    values["invalidStart"] = startText->property("invalid"); values["invalidEnd"] = endText->property("invalid");
    if (clip.controls != values) { clip.state = "Pending"; clip.error.clear(); }
    clip.controls = values; clip.output = settings();
}
void MainWindow::selectClip(int index)
{
    if (index < 0 || index >= clips.size() || index == activeClip || stepping || batching) return;
    saveClip(); activeClip = index;
    const auto clip = clips[index]; const auto &v = clip.controls;
    syncing = true; int key = 0;
    for (auto *box : {format, resolution, quality, fps, audio, subtitle, subtitleMode, gifWidth, gifFps, gifQuality})
        box->setCurrentIndex(v.value(QString::number(key++)).toInt());
    for (auto *spin : {customWidth, customHeight, gifCustom}) spin->setValue(v.value(QString::number(key++)).toInt());
    aspectLock->setChecked(v.value("aspect").toBool()); loop->setChecked(v.value("loop").toBool());
    manualName = v.value("manual").toBool(); filename->setText(v.value("filename").toString());
    directory->setText(v.value("directory").toString());
    start = clip.output.start; end = clip.output.end; position = start;
    wantPlaying = false; stoppedAtEnd = false; player->pause(true); player->seek(start);
    syncing = false; updateSettings();
    subtitleMode->setCurrentText(clip.output.subtitleMode); updateTracks(); updateRange();
    startText->setText(v.value("startText").toString()); endText->setText(v.value("endText").toString());
    startText->setProperty("invalid", v.value("invalidStart")); endText->setProperty("invalid", v.value("invalidEnd"));
    for (auto *field : {startText, endText}) field->setStyleSheet(field->property("invalid").toBool() ? "border: 1px solid #f87171; background-color: #241116" : "");
    validate(); refreshClipList();
}
void MainWindow::refreshClipList()
{
    QSignalBlocker block(clipList);
    while (clipList->count() > clips.size()) delete clipList->takeItem(clipList->count() - 1);
    QList<QPair<double, double>> ranges;
    for (int i = 0; i < clips.size(); ++i) {
        const auto &clip = clips[i];
        const QString text = QString("Clip %1   %4\n%2 - %3\n%5").arg(i + 1)
            .arg(timecode(clip.output.start), timecode(clip.output.end), clip.output.format.toUpper(), clip.state);
        if (i == clipList->count()) clipList->addItem(text);
        else if (clipList->item(i)->text() != text) clipList->item(i)->setText(text);
        clipList->item(i)->setToolTip(clip.output.destination + "\n" + clip.error);
        ranges.append({clip.output.start, clip.output.end});
    }
    clipList->setCurrentRow(activeClip);
    clipList->setFixedHeight(std::max(1, std::min(3, clipList->count())) *
        std::max(60, clipList->sizeHintForRow(0)) + 2 * clipList->frameWidth());
    timeline->setClips(mode->currentIndex() == 1 ? ranges : QList<QPair<double, double>>{}, activeClip);
}
void MainWindow::beginBatch(bool retry)
{
    if (batching || job.running() || !playbackReady || stepping) return;
    saveClip(); queue.clear(); QSet<QString> destinations;
    for (int i = 0; i < clips.size(); ++i) {
        auto &clip = clips[i];
        if (retry && clip.state != "Failed") continue;
        QString error = exportValidation(media, clip.output, caps);
        if (clip.controls.value("invalidStart").toBool() || clip.controls.value("invalidEnd").toBool()) error = "Correct the invalid timestamp.";
        QString name = clip.controls.value("filename").toString();
        if (name.contains('/') || name.contains('\\')) error = "The filename must not contain folders.";
        QFileInfo target(clip.output.destination);
        QString folder = QFileInfo(target.absolutePath()).canonicalFilePath();
        QString path = (folder.isEmpty() ? target.absolutePath() : folder) + '/' + target.fileName();
        if (destinations.contains(path)) error = "Two clips have the same output path. Give each a different filename.";
        if (!error.isEmpty()) { showError(QString("Clip %1: %2").arg(i + 1).arg(error)); queue.clear(); return; }
        destinations.insert(path); clip.replace = target.exists(); queue.append(i);
    }
    if (queue.isEmpty()) { setStatus("No failed clips to retry."); return; }
    QStringList existing;
    for (int i : queue) if (clips[i].replace) existing.append(clips[i].output.destination);
    if (!existing.isEmpty() && QMessageBox::question(this, "Replace existing files?", existing.join('\n'), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) { queue.clear(); return; }
    for (int i : queue) clips[i].state = "Pending";
    batching = true; cancelBatch = false; wantPlaying = false; player->pause(true);
    editor->setEnabled(false); destinationPanel->setEnabled(false); mode->setEnabled(false); exportButton->setEnabled(false); cancelButton->setEnabled(true);
    cancelButton->setText("Cancel Current"); cancelAll->show();
    for (auto *button : {openFile, openFolder, copyPath}) button->hide();
    nextExport();
}
void MainWindow::nextExport()
{
    if (cancelBatch) { for (int i : queue) clips[i].state = "Cancelled"; queue.clear(); }
    if (queue.isEmpty()) {
        batching = false; exportingClip = -1; editor->setEnabled(true); destinationPanel->setEnabled(true); mode->setEnabled(true);
        cancelButton->setText("Cancel"); cancelButton->setEnabled(false); cancelAll->hide();
        refreshClipList(); validate();
        int done = 0, failed = 0, cancelled = 0;
        for (const auto &clip : clips) { done += clip.state == "Done"; failed += clip.state == "Failed"; cancelled += clip.state == "Cancelled"; }
        setStatus(QString("%1 done, %2 failed, %3 cancelled").arg(done).arg(failed).arg(cancelled), failed ? "#f87171" : cancelled ? "#facc15" : "#4ade80");
        if (closing) close(); return;
    }
    exportingClip = queue.takeFirst(); auto &clip = clips[exportingClip];
    clip.state = "Exporting"; completedPath = clip.output.destination;
    progress->show(); progress->setValue(0); refreshClipList(); exportElapsed.start(); exportStatus.start();
    job.start(media, clip.output, clip.replace);
}
