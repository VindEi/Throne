#include "include/ui/widget/SubscriptionInfoCard.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>

#include "include/database/entities/Group.h"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/global/Utils.hpp"

namespace {
    QPixmap renderVectorPixmap(const QString &kind, const QColor &color, int size = 14) {
        QPixmap pix(size, size);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        if (kind == "globe") {
            p.drawEllipse(QRectF(1.0, 1.0, 12.0, 12.0));
            p.drawLine(QPointF(1.2, 7.0), QPointF(12.8, 7.0));
            p.drawEllipse(QRectF(3.8, 1.0, 6.4, 12.0));
        } else if (kind == "chat") {
            QPainterPath path;
            path.addRoundedRect(QRectF(1.0, 1.5, 12.0, 8.5), 2.0, 2.0);
            path.moveTo(3.5, 10.0);
            path.lineTo(2.0, 12.5);
            path.lineTo(6.5, 10.0);
            p.drawPath(path);
            p.drawLine(QPointF(4.0, 4.5), QPointF(10.0, 4.5));
            p.drawLine(QPointF(4.0, 7.0), QPointF(8.5, 7.0));
        } else if (kind == "calendar") {
            p.drawRoundedRect(QRectF(1.0, 2.5, 12.0, 10.0), 2.0, 2.0);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRoundedRect(QRectF(1.0, 2.5, 12.0, 3.0), 1.5, 1.5);
            p.setPen(pen);
            p.drawLine(QPointF(3.8, 0.8), QPointF(3.8, 2.5));
            p.drawLine(QPointF(10.2, 0.8), QPointF(10.2, 2.5));
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRect(QRectF(3.5, 7.2, 1.8, 1.4));
            p.drawRect(QRectF(6.2, 7.2, 1.8, 1.4));
            p.drawRect(QRectF(8.9, 7.2, 1.8, 1.4));
            p.drawRect(QRectF(3.5, 9.6, 1.8, 1.4));
            p.drawRect(QRectF(6.2, 9.6, 1.8, 1.4));
        } else if (kind == "warn") {
            QPolygonF tri;
            tri << QPointF(7.0, 1.2) << QPointF(13.2, 12.5) << QPointF(0.8, 12.5);
            p.drawPolygon(tri);
            p.drawLine(QPointF(7.0, 5.0), QPointF(7.0, 8.5));
            p.drawPoint(QPointF(7.0, 10.5));
        } else if (kind == "refresh") {
            p.drawArc(QRectF(1.8, 1.8, 10.4, 10.4), 45 * 16, 270 * 16);
            p.drawLine(QPointF(9.5, 1.0), QPointF(12.5, 3.8));
            p.drawLine(QPointF(9.5, 6.5), QPointF(12.5, 3.8));
        } else if (kind == "notice") {
            QPolygonF horn;
            horn << QPointF(1.5, 5.0) << QPointF(6.5, 2.5) << QPointF(6.5, 10.5) << QPointF(1.5, 8.0);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawPolygon(horn);
            p.drawRoundedRect(QRectF(6.5, 4.0, 3.0, 5.0), 1.0, 1.0);
            p.setPen(pen);
            p.drawLine(QPointF(3.0, 8.0), QPointF(2.5, 11.5));
            p.drawArc(QRectF(9.5, 3.2, 3.0, 6.6), -50 * 16, 100 * 16);
        }
        p.end();
        return pix;
    }
}

SubscriptionInfoCard::SubscriptionInfoCard(QWidget *parent)
    : QFrame(parent) {
    setupUi();
    applyTheme();

    connect(themeManager, &ThemeManager::themeChanged, this, [this](const QString &) {
        applyTheme();
        updateData();
    });
}

void SubscriptionInfoCard::setupUi() {
    setObjectName(QStringLiteral("SubscriptionInfoCard"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 1, 4, 1);
    rootLayout->setSpacing(2);

    // ==========================================
    // Line 1: Title (left) + Action Buttons (right)
    // ==========================================
    auto *row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(4);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(8);
    m_titleLabel->setFont(titleFont);
    row1->addWidget(m_titleLabel, 1);

    auto createButton = [this](const QString &tooltip) -> QToolButton* {
        auto *btn = new QToolButton(this);
        btn->setFixedSize(20, 17);
        btn->setIconSize(QSize(12, 12));
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_btnPortal = createButton(tr("Website / Portal"));
    connect(m_btnPortal, &QToolButton::clicked, this, [this] {
        if (m_group) {
            auto sub = m_group->GetSubUserInfo();
            if (!sub.web_url.isEmpty()) QDesktopServices::openUrl(QUrl(sub.web_url));
        }
    });
    row1->addWidget(m_btnPortal);

    m_btnSupport = createButton(tr("Technical Support"));
    connect(m_btnSupport, &QToolButton::clicked, this, [this] {
        if (m_group) {
            auto sub = m_group->GetSubUserInfo();
            if (!sub.support_url.isEmpty()) QDesktopServices::openUrl(QUrl(sub.support_url));
        }
    });
    row1->addWidget(m_btnSupport);

    m_btnUpdate = createButton(tr("Update Subscription"));
    connect(m_btnUpdate, &QToolButton::clicked, this, [this] {
        if (m_group && !m_group->url.isEmpty()) {
            Subscription::groupUpdater->AsyncUpdate(m_group->url, m_group->id, nullptr, true);
        }
    });
    row1->addWidget(m_btnUpdate);

    rootLayout->addLayout(row1);

    // ==========================================
    // Line 2: Full-width Progress Bar ALONE
    // (Nothing beside it = impossible to overlap!)
    // ==========================================
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(14);
    m_progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_progressBar->setTextVisible(true);
    m_progressBar->setAlignment(Qt::AlignCenter);
    rootLayout->addWidget(m_progressBar);

    // ==========================================
    // Line 3: Expiry (left) + Announce (right)
    // ==========================================
    auto *row3 = new QHBoxLayout();
    row3->setContentsMargins(0, 0, 0, 0);
    row3->setSpacing(4);

    m_expiryIcon = new QLabel(this);
    m_expiryIcon->setFixedSize(12, 12);
    m_expiryLabel = new QLabel(this);
    QFont expFont = m_expiryLabel->font();
    expFont.setPointSize(7);
    m_expiryLabel->setFont(expFont);
    row3->addWidget(m_expiryIcon);
    row3->addWidget(m_expiryLabel);

    row3->addStretch(1);

    m_announceContainer = new QWidget(this);
    auto *annLayout = new QHBoxLayout(m_announceContainer);
    annLayout->setContentsMargins(0, 0, 0, 0);
    annLayout->setSpacing(3);

    m_announceIcon = new QLabel(m_announceContainer);
    m_announceIcon->setFixedSize(12, 12);
    m_announceLabel = new QLabel(m_announceContainer);
    QFont annFont = m_announceLabel->font();
    annFont.setPointSize(7);
    annFont.setItalic(true);
    m_announceLabel->setFont(annFont);
    annLayout->addWidget(m_announceIcon);
    annLayout->addWidget(m_announceLabel);

    row3->addWidget(m_announceContainer);

    rootLayout->addLayout(row3);
}

bool SubscriptionInfoCard::hasSubscription() const {
    return m_group != nullptr && !m_group->url.isEmpty();
}

void SubscriptionInfoCard::setGroup(const std::shared_ptr<Configs::Group> &group) {
    m_group = group;
    updateData();
}

void SubscriptionInfoCard::updateData() {
    if (!hasSubscription()) {
        hide();
        return;
    }

    auto sub = m_group->GetSubUserInfo();
    const auto &tk = themeManager->tokens;
    const QColor winBg = qApp->palette().color(QPalette::Active, QPalette::Window);
    const bool isDark = (winBg.lightness() <= 128);
    const QString textColor = isDark ? QStringLiteral("#DFE1E2") : QStringLiteral("#1F2328");
    const QString trackColor = isDark ? QStringLiteral("#1E2630") : QStringLiteral("#E9ECEF");
    const QString borderColor = isDark ? QStringLiteral("#455364") : QStringLiteral("#CBD5E1");

    // 1. Line 1: Title
    QString title = !sub.title.isEmpty() ? sub.title : m_group->name;
    m_titleLabel->setText(title);

    // 2. Line 2: Progress Bar with dynamic thresholds
    QString usedStr = ReadableSize(sub.used());
    QString totalStr = (sub.total > 0) ? ReadableSize(sub.total) : QString::fromUtf8("∞");
    double pct = sub.percentUsed();

    QString barColor;
    if (sub.isExpired() || pct >= 90.0) barColor = tk.danger.name();
    else if (pct >= 75.0) barColor = tk.tag.name();
    else if (pct >= 50.0) barColor = tk.accent.name();
    else barColor = tk.success.name();

    if (sub.total > 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>(pct));
        m_progressBar->setFormat(QStringLiteral("%1 / %2 (%3%)").arg(usedStr, totalStr, QString::number(static_cast<int>(pct))));
    } else {
        // Unlimited: clean background pill with centered usage
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_progressBar->setFormat(QStringLiteral("%1 / ∞").arg(usedStr));
    }

    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar {"
        "  border: 1px solid %1;"
        "  border-radius: 3px;"
        "  text-align: center;"
        "  font-weight: bold;"
        "  font-size: 7pt;"
        "  color: %2;"
        "  background-color: %3;"
        "}"
        "QProgressBar::chunk {"
        "  background-color: %4;"
        "  border-radius: 2px;"
        "}"
    ).arg(borderColor, (pct >= 50.0 && sub.total > 0) ? QStringLiteral("#FFFFFF") : textColor, trackColor, barColor));

    // 3. Line 3: Expiry (left)
    if (sub.expire > 0) {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        qint64 diffDays = (sub.expire - now) / 86400;
        if (sub.isExpired()) {
            m_expiryIcon->setPixmap(renderVectorPixmap("warn", tk.danger, 12));
            m_expiryLabel->setText(tr("Expired"));
            m_expiryLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(tk.danger.name()));
        } else if (diffDays <= 3) {
            m_expiryIcon->setPixmap(renderVectorPixmap("warn", tk.danger, 12));
            m_expiryLabel->setText(QStringLiteral("%1d left").arg(std::max<qint64>(1, diffDays)));
            m_expiryLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(tk.danger.name()));
        } else {
            m_expiryIcon->setPixmap(renderVectorPixmap("calendar", tk.muted, 12));
            m_expiryLabel->setText(QStringLiteral("%1d left").arg(diffDays));
            m_expiryLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk.muted.name()));
        }
    } else {
        m_expiryIcon->setPixmap(renderVectorPixmap("calendar", tk.muted, 12));
        m_expiryLabel->setText(QStringLiteral("∞ left"));
        m_expiryLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk.muted.name()));
    }

    // 4. Action buttons (strictly verified against empty URLs)
    const bool hasWeb = !sub.web_url.isEmpty() && (sub.web_url.startsWith("http://", Qt::CaseInsensitive) || sub.web_url.startsWith("https://", Qt::CaseInsensitive));
    const bool hasSupport = !sub.support_url.isEmpty() && (sub.support_url.startsWith("http://", Qt::CaseInsensitive) || sub.support_url.startsWith("https://", Qt::CaseInsensitive));
    m_btnPortal->setVisible(hasWeb);
    m_btnSupport->setVisible(hasSupport);

    // 5. Line 3: Announce (right) - with vector notice icon, ZERO emojis
    const QString cleanAnnounce = sub.announce.trimmed();
    if (!cleanAnnounce.isEmpty() && cleanAnnounce.compare("base64:", Qt::CaseInsensitive) != 0) {
        QString text = cleanAnnounce;
        constexpr int maxLen = 30;
        if (text.length() > maxLen) {
            QString loopText = text + "          •          " + text;
            m_announceOffset = (m_announceOffset + 1) % (text.length() + 21);
            text = loopText.mid(m_announceOffset, maxLen);
        }
        m_announceIcon->setPixmap(renderVectorPixmap("notice", tk.muted, 12));
        m_announceLabel->setText(text);
        m_announceLabel->setStyleSheet(QStringLiteral("color: %1;").arg(tk.muted.name()));
        m_announceContainer->show();
    } else {
        m_announceContainer->hide();
    }

    show();
}

void SubscriptionInfoCard::applyTheme() {
    const auto &tk = themeManager->tokens;
    const QColor winBg = qApp->palette().color(QPalette::Active, QPalette::Window);
    const bool isDark = (winBg.lightness() <= 128);

    const QString chipBg = isDark ? QStringLiteral("#1E293B") : QStringLiteral("#EEF2F6");
    const QString chipBorder = isDark ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
    const QString textColor = isDark ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E293B");

    setStyleSheet(QStringLiteral(
        "QFrame#SubscriptionInfoCard { background: transparent; border: none; }"
        "QToolButton {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 3px;"
        "  padding: 1px;"
        "  color: %3;"
        "}"
        "QToolButton:hover {"
        "  background-color: %4;"
        "  border-color: %5;"
        "}"
    ).arg(chipBg, chipBorder, textColor, tk.hoverFill.name(), tk.accent.name()));

    m_btnPortal->setIcon(QIcon(renderVectorPixmap("globe", tk.accent, 12)));
    m_btnSupport->setIcon(QIcon(renderVectorPixmap("chat", tk.accent, 12)));
    m_btnUpdate->setIcon(QIcon(renderVectorPixmap("refresh", tk.onSurface, 12)));
}