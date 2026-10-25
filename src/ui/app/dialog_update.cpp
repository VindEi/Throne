#include "include/ui/app/dialog_update.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "NkrVersion.h"
#include "include/ui/setting/ThemeManager.hpp"

DialogUpdate::DialogUpdate(const QString &version, bool preRelease, const QString &notes, const QUrl &releaseUrl,
                           bool canUpdate, const QString &notice, QWidget *parent) : QDialog(parent) {
    setWindowTitle(preRelease ? tr("Update (Pre-release)") : tr("Update"));
    setSizeGripEnabled(true);

    const auto fm = fontMetrics();
    if (const auto *screen = parent != nullptr ? parent->screen() : this->screen()) {
        const auto avail = screen->availableGeometry().size();
        setMinimumSize(std::min(fm.averageCharWidth() * 60, avail.width() * 8 / 10),
                       std::min(fm.lineSpacing() * 22, avail.height() * 8 / 10));
    } else {
        setMinimumSize(fm.averageCharWidth() * 60, fm.lineSpacing() * 22);
    }

    const auto &tk = themeManager()->tokens;

    auto *headerLayout = new QVBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, fm.lineSpacing() / 4);
    headerLayout->setSpacing(fm.lineSpacing() / 4);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);

    auto *titleLabel = new QLabel(tr("Update Available"), this);
    auto titleFont = titleLabel->font();
    titleFont.setBold(true);
    if (titleFont.pointSizeF() > 0) {
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.35);
    } else {
        titleFont.setPixelSize(titleFont.pixelSize() * 4 / 3);
    }
    titleLabel->setFont(titleFont);
    titleRow->addWidget(titleLabel);

    titleRow->addStretch(1);

    if (preRelease) {
        auto *badge = new QLabel(tr("Pre-release"), this);
        auto badgeFont = badge->font();
        if (badgeFont.pointSizeF() > 0) {
            badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.85);
        } else {
            badgeFont.setPixelSize(std::max(1, badgeFont.pixelSize() * 85 / 100));
        }
        badgeFont.setBold(true);
        badge->setFont(badgeFont);

        QColor tagBg = tk.tag;
        tagBg.setAlphaF(0.2f);
        badge->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background-color: %1;"
            "  color: %2;"
            "  padding: 1px 8px;"
            "  border: 1px solid %3;"
            "  border-radius: 8px;"
            "}"
        ).arg(tagBg.name(QColor::HexArgb), tk.accent.name(), tk.muted.name()));
        badge->setAlignment(Qt::AlignCenter);
        titleRow->addWidget(badge);
    }
    headerLayout->addLayout(titleRow);

    const QString currentVer = QStringLiteral(NKR_VERSION).trimmed().isEmpty()
        ? tr("dev")
        : QStringLiteral(NKR_VERSION);

    auto *versionLabel = new QLabel(this);
    versionLabel->setTextFormat(Qt::RichText);
    versionLabel->setText(QStringLiteral("<span style=\"color: %1;\">%2</span> &nbsp;→&nbsp; <b>%3</b>")
        .arg(tk.muted.name(), currentVer.toHtmlEscaped(), version.toHtmlEscaped()));
    auto vFont = versionLabel->font();
    if (vFont.pointSizeF() > 0) {
        vFont.setPointSizeF(vFont.pointSizeF() * 1.05);
    } else {
        vFont.setPixelSize(std::max(1, vFont.pixelSize() * 21 / 20));
    }
    versionLabel->setFont(vFont);
    headerLayout->addWidget(versionLabel);

    auto *notesHeader = new QLabel(tr("Release Notes:"), this);
    auto nhFont = notesHeader->font();
    nhFont.setBold(true);
    notesHeader->setFont(nhFont);

    auto *notesView = new QTextBrowser(this);
    notesView->setOpenLinks(false);
    notesView->setFocusPolicy(Qt::NoFocus);
    notesView->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "  background-color: palette(base);"
        "  padding: 8px 12px;"
        "}"
    ));
    notesView->document()->setDefaultStyleSheet(QStringLiteral(
        "ul { margin-top: 4px; margin-bottom: 8px; padding-left: 20px; }"
        "li { margin-bottom: 4px; }"
        "p { margin-top: 4px; margin-bottom: 6px; }"
        "h1, h2, h3, h4 { font-weight: bold; margin-top: 10px; margin-bottom: 4px; }"
        "code { background-color: rgba(128, 128, 128, 0.15); border-radius: 3px; font-family: monospace; padding: 2px 4px; }"
        "pre { background-color: rgba(128, 128, 128, 0.12); border-radius: 4px; padding: 8px; margin-top: 6px; margin-bottom: 6px; }"
    ));
    notesView->document()->setDocumentMargin(4);
    connect(notesView, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        if (url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))) {
            QDesktopServices::openUrl(url);
        }
    });

    if (notes.trimmed().isEmpty()) {
        notesView->setPlainText(tr("Release notes are unavailable."));
    } else {
        notesView->setMarkdown(notes);
    }

    auto *layout = new QVBoxLayout(this);
    const int margin = fm.lineSpacing();
    layout->setContentsMargins(margin, margin, margin, margin * 4 / 5);
    layout->setSpacing(fm.lineSpacing() / 2);
    layout->addLayout(headerLayout);
    layout->addWidget(notesHeader);
    layout->addWidget(notesView, 1);

    if (!notice.isEmpty()) {
        auto *noticeLabel = new QLabel(notice, this);
        noticeLabel->setTextFormat(Qt::PlainText);
        noticeLabel->setWordWrap(true);
        layout->addWidget(noticeLabel);
    }

    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->setContentsMargins(0, fm.lineSpacing() / 4, 0, 0);
    bottomLayout->setSpacing(fm.averageCharWidth());

    auto *openInBrowser = new QPushButton(tr("Open in browser"), this);
    openInBrowser->setAutoDefault(false);
    connect(openInBrowser, &QPushButton::clicked, this, [this, releaseUrl] {
        if (releaseUrl.isValid() && (releaseUrl.scheme() == QLatin1String("https") || releaseUrl.scheme() == QLatin1String("http"))) {
            QDesktopServices::openUrl(releaseUrl);
        }
        accept();
    });
    bottomLayout->addWidget(openInBrowser);

    bottomLayout->addStretch(1);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setAutoDefault(false);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottomLayout->addWidget(closeBtn);

    if (canUpdate) {
        auto *updateBtn = new QPushButton(tr("Update", "button"), this);
        updateBtn->setDefault(true);
        connect(updateBtn, &QPushButton::clicked, this, [this] {
            emit updateRequested();
            accept();
        });
        bottomLayout->addWidget(updateBtn);
    }

    layout->addLayout(bottomLayout);

    QSize size(fm.averageCharWidth() * 80, fm.lineSpacing() * 28);
    if (const auto *screen = parent != nullptr ? parent->screen() : this->screen()) {
        const auto avail = screen->availableGeometry().size();
        size = size.boundedTo(avail * 0.85);
        setMaximumSize(avail * 0.95);
    }
    resize(size);
}
