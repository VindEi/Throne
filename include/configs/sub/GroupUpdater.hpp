#pragma once

#include <include/database/entities/Profile.h>

#include <QStringList>

namespace Subscription {
    enum class SingBoxSubType {
        fullConfig,
        outboundInJson,
        outboundJsonArray,
        outboundObject,
        invalid,
    };
    enum class XraySubType {
        outboundInJson,
        outboundJsonArray,
        outboundObject,
        configJsonArray,
        invalid,
    };
    class RawUpdater {
    public:
        void update(const QString &str, bool needParse = true, bool isBase64Decoded = false);

        void updateSingBox(const QJsonDocument &doc, SingBoxSubType type);

        void updateXray(const QJsonDocument &doc, XraySubType type);

        void updateClash(const QString &str);

        void updateWireguardFileConfig(const QString &str);

        void updateOpenVPNFileConfig(const QString &str);

        void updateOpenConnectProfile(const QString &str);

        void updateSIP008(const QString &str);

        int gid_add_to = -1;

        QList<std::shared_ptr<Configs::Profile>> updated_order;
    };

    class GroupUpdater : public QObject {
        Q_OBJECT

    public:
        // showDiff: a manual refresh pops up the diff; automatic paths leave it false and only log.
        void AsyncUpdate(const QString &str, int _sub_gid = -1, const std::function<void()> &finish = nullptr, bool showDiff = false);

        void AsyncImportBatch(const QStringList &payloads, const std::function<void()> &finish = nullptr);

        void Update(const QString &_str, int _sub_gid = -1, bool _not_sub_as_url = false, bool showDiff = false);

    signals:

        void asyncUpdateCallback(int gid);
    };

    extern GroupUpdater *groupUpdater;
    int ParseUpdateInterval(const QString &headerStr);
} // namespace Subscription

void UI_update_all_groups(bool onlyAllowed = false);
void UI_check_auto_update_groups();
