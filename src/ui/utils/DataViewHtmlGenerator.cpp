#include "include/ui/utils/DataViewHtmlGenerator.h"

#include "include/global/CountryHelper.hpp"
#include "include/global/Configs.hpp"
#include "include/ui/setting/ThemeManager.hpp"

void DataViewHtmlGenerator::setDownloadReport(const DownloadProgressReport &report, bool show) {
    QMutexLocker lk(&mu_);
    download_.visible = show;
    download_.report = report;
}

void DataViewHtmlGenerator::seedSpeedTest(int totalProfiles) {
    QMutexLocker lk(&mu_);
    testProgress.store(0);
    Configs::dataManager->settingsRepo->speed_test_mode == Configs::TestConfig::COUNTRY ? speedtest_.kind = SpeedtestPanelState::Kind::Country : speedtest_.kind = SpeedtestPanelState::Kind::Speed;
    speedtest_.totalProfiles = totalProfiles;
    speedtest_.visible = true;
}

void DataViewHtmlGenerator::setSpeedtestProgress(const QString &profileName, const libcore::SpeedTestResult &result) {
    QMutexLocker lk(&mu_);
    speedtest_.profileName = profileName;
    speedtest_.dlSpeed = QString::fromStdString(result.dl_speed.value());
    speedtest_.ulSpeed = QString::fromStdString(result.ul_speed.value());
    speedtest_.serverCountryFlag = CountryCodeToFlag(CountryNameToCode(QString::fromStdString(result.server_country.value())));
    speedtest_.serverCountry = QString::fromStdString(result.server_country.value());
    speedtest_.serverName = QString::fromStdString(result.server_name.value());
}

void DataViewHtmlGenerator::seedLatencyTest(LatencyTestPanelState::Kind kind, int totalProfiles) {
    QMutexLocker lk(&mu_);
    testProgress.store(0);
    latencyTest_.visible = true;
    latencyTest_.kind = kind;
    latencyTest_.totalProfiles = totalProfiles;
}

void DataViewHtmlGenerator::setAutoSelectorStatus(const QString &summary, const QString &detail) {
    QMutexLocker lk(&mu_);
    autoSelector_.summary = summary;
    autoSelector_.detail = detail;
    autoSelector_.visible = !summary.isEmpty();
}

void DataViewHtmlGenerator::setVpnEndpointStatus(const QString &summary, const QString &detail, bool problem) {
    QMutexLocker lk(&mu_);
    vpnEndpoint_.summary = summary;
    vpnEndpoint_.detail = detail;
    vpnEndpoint_.problem = problem;
    vpnEndpoint_.visible = !summary.isEmpty();
}

void DataViewHtmlGenerator::setSubscriptionStatus(const QString &groupName, const Configs::SubUserInfo &info, qint64 lastUpdate) {
    QMutexLocker lk(&mu_);
    subInfo_.groupName = groupName;
    subInfo_.info = info;
    subInfo_.lastUpdate = lastUpdate;
    subInfo_.visible = info.valid;
}


void DataViewHtmlGenerator::clearTestSections() {
    QMutexLocker lk(&mu_);
    latencyTest_ = {};
    speedtest_ = {};
    testProgress.store(0);
}

void DataViewHtmlGenerator::addTestProgress(int count) {
    testProgress.fetch_add(count);
}

QString DataViewHtmlGenerator::buildHtml() {
    QMutexLocker lk(&mu_);
    QString html;
    if (download_.visible) {
        html += downloadSectionHtml();
    }
    if (speedtest_.visible) {
        html += speedtestSectionHtml();
    }
    if (latencyTest_.visible) {
        html += latencyTestSectionHtml();
    }
    // Last and conditional: ambient status yields the view whenever a job wants to report progress.
    if (html.isEmpty() && vpnEndpoint_.visible) {
        html += vpnEndpointSectionHtml();
    }
    if (html.isEmpty() && autoSelector_.visible) {
        html += autoSelectorSectionHtml();
    }
    if (html.isEmpty() && subInfo_.visible) {
        html += subscriptionSectionHtml();
    }
    return html;
}

QString DataViewHtmlGenerator::vpnEndpointSectionHtml() {
    const auto colour = vpnEndpoint_.problem ? themeManager->tokens.danger : themeManager->tokens.info;
    QString res = QString("<p style='text-align:center;margin:0;color:%1;'>%2</p>")
                      .arg(colour.name(), vpnEndpoint_.summary.toHtmlEscaped());
    if (!vpnEndpoint_.detail.isEmpty()) {
        res += QString("<p style='text-align:center;margin:0;opacity:0.75;'>%1</p>")
                   .arg(vpnEndpoint_.detail.toHtmlEscaped());
    }
    return res;
}

QString DataViewHtmlGenerator::autoSelectorSectionHtml() {
    QString res = QString("<p style='text-align:center;margin:0;'>%1</p>").arg(autoSelector_.summary.toHtmlEscaped());
    if (!autoSelector_.detail.isEmpty()) {
        res += QString("<p style='text-align:center;margin:0;opacity:0.75;'>%1</p>")
                   .arg(autoSelector_.detail.toHtmlEscaped());
    }
    return res;
}

QString DataViewHtmlGenerator::getProgressBar(long long current, long long total) {
    qint64 count = 0;
    if (total > 0) {
        count = 10 * current / total;
    }
    QString progressText;
    for (int i = 0; i < 10; i++) {
        if (count--; count >= 0) {
            progressText += "#";
        } else {
            progressText += "-";
        }
    }
    return progressText;
}

QString DataViewHtmlGenerator::downloadSectionHtml() {
    auto progressText = getProgressBar(download_.report.downloadedSize, download_.report.totalSize);
    const QString stat =
        ReadableSize(download_.report.downloadedSize) + "/" + ReadableSize(download_.report.totalSize);
    return QString("<p style='text-align:center;margin:0;'>Downloading %1: %2 %3</p>")
        .arg(download_.report.fileName, stat, progressText);
}

QString DataViewHtmlGenerator::speedtestSectionHtml() {
    if (speedtest_.kind == SpeedtestPanelState::Kind::Speed) {
        auto firstLine = QStringLiteral("Running Speedtest: %1").arg(speedtest_.profileName);
        if (speedtest_.totalProfiles > 1) {
            firstLine += QString(" (%1 / %2)").arg(Int2String(testProgress.load()), Int2String(speedtest_.totalProfiles));
        }
        if (speedtest_.serverName.isEmpty()) return QString("<p style='text-align:center;margin:0;'>%1</p>").arg(firstLine);
        return QString(
           "<p style='text-align:center;margin:0;'>%1</p>"
           "<div style='text-align: center;'>"
           "<span style='color: %7;'>Dl↓ %2</span>  "
           "<span style='color: %8;'>Ul↑ %3</span>"
           "</div>"
           "<p style='text-align:center;margin:0;'>Server: %4%5, %6</p>")
            .arg(firstLine, speedtest_.dlSpeed, speedtest_.ulSpeed, speedtest_.serverCountryFlag, speedtest_.serverCountry,
                speedtest_.serverName, themeManager->tokens.info.name(), themeManager->tokens.success.name());
    } else {
        QString res;
        auto content = QString("Running Country Test");
        if (speedtest_.totalProfiles > 1) {
            auto progress = getProgressBar(testProgress.load(), speedtest_.totalProfiles);
            progress += QString(" ") + Int2String(100 * testProgress.load() / speedtest_.totalProfiles) + "%";
            res = QString("<p style='text-align:center;margin:0;'>%1</p>").arg(progress);
            content += QString(" (%1 / %2)").arg(Int2String(testProgress.load()), Int2String(speedtest_.totalProfiles));
        }
        res += QString("<p style='text-align:center;margin:0;'>%1</p>").arg(content);
        return res;
    }
}

QString DataViewHtmlGenerator::latencyTestSectionHtml() {
    QString res;
    auto content =
        latencyTest_.kind == LatencyTestPanelState::Kind::Url ? QString("Running URL test") : QString("Running IP test");
    if (latencyTest_.totalProfiles > 1) {
        auto progress = getProgressBar(testProgress.load(), latencyTest_.totalProfiles);
        progress += QString(" ") + Int2String(100 * testProgress.load() / latencyTest_.totalProfiles) + "%";
        res = QString("<p style='text-align:center;margin:0;'>%1</p>").arg(progress);
        content += QString(" (%1 / %2)").arg(Int2String(testProgress.load()), Int2String(latencyTest_.totalProfiles));
    }
    res += QString("<p style='text-align:center;margin:0;'>%1</p>").arg(content);
    return res;
}

QString DataViewHtmlGenerator::subscriptionSectionHtml() {
    if (!subInfo_.visible || !subInfo_.info.valid) return {};

    const auto &info = subInfo_.info;
    const auto &tk = themeManager->tokens;

    QString usedStr = ReadableSize(info.used());
    QString totalStr = (info.total > 0) ? ReadableSize(info.total) : QString::fromUtf8("∞");
    double pct = info.percentUsed();

    // 1. Color matching threshold
    QString barColor = (pct > 90.0 || info.isExpired()) ? tk.danger.name() : (pct > 75.0 ? tk.tag.name() : tk.info.name());

    // 2. Real Working Progress Bar Pill with text on it
    QString progressBar;
    if (info.total > 0) {
        int fillPct = std::clamp(static_cast<int>(pct), 5, 95);
        int emptyPct = 100 - fillPct;
        progressBar = QString(
            "<table width='130' border='0' cellpadding='1' cellspacing='0' style='border:1px solid %1; background-color:%2;'>"
            "<tr>"
            "<td width='%3%' bgcolor='%4' align='center' style='font-size:7.5pt; font-weight:bold; color:#FFFFFF; white-space:nowrap;'>"
            "&nbsp;%5&nbsp;"
            "</td>"
            "<td width='%6%' bgcolor='%2' align='center' style='font-size:7.5pt; font-weight:bold; color:%7; white-space:nowrap;'>"
            "&nbsp;/ %8 (%9%)&nbsp;"
            "</td>"
            "</tr>"
            "</table>"
        ).arg(tk.borderSubtle.name(), tk.surface.name(), QString::number(fillPct), barColor, usedStr, QString::number(emptyPct), tk.onSurface.name(), totalStr, QString::number(static_cast<int>(pct)));
    } else {
        // Unlimited Quota Bar
        progressBar = QString(
            "<table width='110' border='0' cellpadding='1' cellspacing='0' style='border:1px solid %1; background-color:%2;'>"
            "<tr>"
            "<td width='100%' bgcolor='%3' align='center' style='font-size:7.5pt; font-weight:bold; color:#FFFFFF; white-space:nowrap;'>"
            "&nbsp;%4 / ∞&nbsp;"
            "</td>"
            "</tr>"
            "</table>"
        ).arg(tk.borderSubtle.name(), tk.surface.name(), barColor, usedStr);
    }

    // 3. Expiration Text
    QString expireText;
    if (info.expire > 0) {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        qint64 diffDays = (info.expire - now) / 86400;
        if (info.isExpired()) {
            expireText = QString("<font color='%1'><b>⛔ %2</b></font>")
                             .arg(tk.danger.name(), QObject::tr("Expired"));
        } else if (diffDays <= 3) {
            expireText = QString("<font color='%1'><b>⚠️ %2d left</b></font>")
                             .arg(tk.danger.name(), QString::number(std::max<qint64>(1, diffDays)));
        } else {
            expireText = QString("<font color='%1'>📅 %2 (%3d)</font>")
                             .arg(tk.muted.name(), QDateTime::fromSecsSinceEpoch(info.expire).toString("dd.MM.yyyy"), QString::number(diffDays));
        }
    } else {
        expireText = QString("<font color='%1'>♾️ %2</font>").arg(tk.muted.name(), QObject::tr("No Expiry"));
    }

    // 4. Action links next to Expiry (Portal 🌐 & Support 💬)
    QString actionLinks;
    if (!info.web_url.isEmpty()) {
        actionLinks += QString("&nbsp;<a href='%1' style='text-decoration:none; font-size:8.5pt;' title='%2'>🌐</a>")
                           .arg(info.web_url.toHtmlEscaped(), QObject::tr("Website / Portal"));
    }
    if (!info.support_url.isEmpty()) {
        actionLinks += QString("&nbsp;<a href='%1' style='text-decoration:none; font-size:8.5pt;' title='%2'>💬</a>")
                           .arg(info.support_url.toHtmlEscaped(), QObject::tr("Technical Support"));
    }

    QString displayTitle = info.title.isEmpty() ? subInfo_.groupName : info.title;

    // 5. Rolling Announce Marquee (Smooth Ticker for Long Messages)
    QString announceRow;
    if (!info.announce.isEmpty()) {
        QString text = info.announce;
        constexpr int maxLen = 42;
        if (text.length() > maxLen) {
            QString loopText = text + "          •          " + text;
            subInfo_.announceOffset = (subInfo_.announceOffset + 1) % (text.length() + 21);
            text = loopText.mid(subInfo_.announceOffset, maxLen);
        }
        announceRow = QString("<tr><td colspan='3' align='center' style='font-size:7.5pt; color:%1; padding-top:1px;'>"
                              "📢 <i>%2</i>"
                              "</td></tr>")
                          .arg(tk.muted.name(), text.toHtmlEscaped());
    }

    // 6. Strict 2-Row Layout: Row 1 (Title | Bar | Expiry & Links), Row 2 (Announce)
    QString res = "<table width='100%' border='0' cellpadding='0' cellspacing='0'>";
    
    // Row 1
    res += QString("<tr>"
                   "<td align='left' width='30%' style='font-size:8.5pt; color:%1; white-space:nowrap;'>"
                   "<b>%2</b>"
                   "</td>"
                   "<td align='center' width='40%'>"
                   "%3"
                   "</td>"
                   "<td align='right' width='30%' style='font-size:8pt; white-space:nowrap;'>"
                   "%4%5"
                   "</td>"
                   "</tr>")
               .arg(tk.onSurface.name(), displayTitle.toHtmlEscaped(), progressBar, expireText, actionLinks);

    // Row 2 (Announce)
    if (!announceRow.isEmpty()) {
        res += announceRow;
    }

    res += "</table>";

    return res;
}