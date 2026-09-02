#include "include/database/entities/Profile.h"
#include "include/global/HTTPRequestHelper.hpp"

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/clash.hpp"
#include "include/configs/sub/vpnFileImport.hpp"

#include <QInputDialog>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QHash>

#include "include/configs/common/utils.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"

namespace Subscription {

    GroupUpdater *groupUpdater = new GroupUpdater;

    int JsonEndIdx(const QString &str, int begin) {
        int sz = str.length();
        int counter = 1;
        for (int i=begin+1;i<sz;i++) {
            if (str[i] == '{') counter++;
            if (str[i] == '}') counter--;
            if (counter==0) return i;
        }
        return -1;
    }

    QList<QString> Disect(const QString &str) {
        QList<QString> res = QList<QString>();
        int idx=0;
        int sz = str.size();
        while(idx < sz) {
            if (str[idx] == '\n') {
                idx++;
                continue;
            }
            if (str[idx] == '{') {
                int endIdx = JsonEndIdx(str, idx);
                if (endIdx == -1) return res;
                res.append(str.mid(idx, endIdx-idx + 1));
                idx = endIdx+1;
                continue;
            }
            int nlineIdx = str.indexOf('\n', idx);
            if (nlineIdx == -1) nlineIdx = sz;
            res.append(str.mid(idx, nlineIdx-idx));
            idx = nlineIdx+1;
        }
        return res;
    }

    SingBoxSubType getSingBoxSubType(const QJsonDocument &doc) {
        if (doc.isObject()) {
            auto obj = doc.object();
            bool hasInbound = obj.contains("inbounds");
            bool hasOutbound = obj.contains("outbounds") || obj.contains("endpoints");
            if (hasOutbound) return SingBoxSubType::outboundInJson;
            if (obj.contains("type")) return SingBoxSubType::outboundObject;
            return SingBoxSubType::invalid;
        }
        if (doc.isArray() && !doc.array().empty()) {
            auto arr = doc.array();
            auto firstRaw = arr.first();
            if (firstRaw.isObject()) {
                auto obj = firstRaw.toObject();
                if (obj.contains("type")) return SingBoxSubType::outboundJsonArray;
            }
            return SingBoxSubType::invalid;
        }
        return SingBoxSubType::invalid;
    }

    // Xray tags outbounds with "protocol" where sing-box uses "type".
    XraySubType getXraySubType(const QJsonDocument &doc) {
        if (doc.isObject()) {
            auto obj = doc.object();
            if (obj.contains("outbounds")) {
                for (const auto &item : obj["outbounds"].toArray()) {
                    if (item.isObject() && item.toObject().contains("protocol")) {
                        return XraySubType::outboundInJson;
                    }
                }
            }
            if (obj.contains("protocol")) return XraySubType::outboundObject;
            return XraySubType::invalid;
        }
        if (doc.isArray() && !doc.array().empty()) {
            auto first = doc.array().first();
            if (first.isObject()) {
                auto obj = first.toObject();
                if (obj.contains("protocol")) return XraySubType::outboundJsonArray;
                if (obj.contains("outbounds")) {
                    for (const auto &item : obj["outbounds"].toArray()) {
                        if (item.isObject() && item.toObject().contains("protocol")) {
                            return XraySubType::configJsonArray;
                        }
                    }
                }
            }
        }
        return XraySubType::invalid;
    }

    // Real Xray VLESS nests the server under settings.vnext[0]; ParseFromJson wants it flat.
    QJsonObject normalizeXrayVlessForParse(const QJsonObject &out) {
        if (out["protocol"].toString() != "vless") return {};
        auto settings = out["settings"].toObject();
        if (settings.contains("address") && !settings.contains("vnext")) return out;
        auto vnext = settings["vnext"].toArray();
        if (vnext.isEmpty()) return {};
        auto first = vnext.first().toObject();
        if (first.isEmpty()) return {};
        auto users = first["users"].toArray();
        if (users.isEmpty()) return {};
        auto user = users.first().toObject();
        QJsonObject simpleSettings;
        simpleSettings["address"] = first["address"];
        simpleSettings["port"] = first["port"];
        simpleSettings["id"] = user["id"];
        simpleSettings["encryption"] = user.contains("encryption") ? user["encryption"] : QJsonValue("none");
        simpleSettings["flow"] = user["flow"];
        QJsonObject normalized = out;
        normalized["settings"] = simpleSettings;
        return normalized;
    }

    std::shared_ptr<Configs::Profile> makeProfileForXrayOutbound(const QJsonObject &out) {
        if (out.isEmpty()) return nullptr;
        auto protocol = out["protocol"].toString();
        if (protocol == "freedom" || protocol == "blackhole" || protocol == "dns" || protocol == "loopback") {
            return nullptr;
        }
        std::shared_ptr<Configs::Profile> ent;
        if (protocol == "vless") {
            if (auto normalized = normalizeXrayVlessForParse(out); !normalized.isEmpty()) {
                ent = Configs::ProfilesRepo::NewProfile("xrayvless");
                if (ent->XrayVLESS()->ParseFromJson(normalized)) return ent;
            }
        }
        ent = Configs::ProfilesRepo::NewProfile("custom");
        ent->Custom()->type = Configs::Custom::CustomXrayOutbound;
        ent->Custom()->config = QJsonObject2QString(out, false);
        if (auto tag = out["tag"].toString(); !tag.isEmpty()) ent->Custom()->name = tag;
        return ent;
    }

    bool looksLikeOvpnConfig(const QString &str) {
        bool hasRemote = false;
        bool hasClientMarker = false;
        for (const auto &line : str.split('\n')) {
            const auto trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith(';')) continue;
            if (trimmed.startsWith("remote ") || trimmed == "<ca>" || trimmed == "<tls-auth>" ||
                trimmed == "<tls-crypt>" || trimmed == "<tls-crypt-v2>" || trimmed == "<secret>") {
                hasRemote = true;
            }
            if (trimmed == "client" || trimmed == "tls-client" || trimmed.startsWith("dev ") ||
                trimmed.startsWith("dev-type ") || trimmed.startsWith("proto ")) {
                hasClientMarker = true;
            }
            if (hasRemote && hasClientMarker) return true;
        }
        return false;
    }

    bool looksLikeOpenConnectProfile(const QString &str) {
        if (str.contains("<AnyConnectProfile") || str.contains("<ServerList")) return true;
        static const QRegularExpression cliRe(R"((?:^|\s)--protocol[= ](?:anyconnect|nc|gp|pulse|f5|fortinet)\b)");
        static const QRegularExpression fileRe(R"(^[ \t]*protocol[ \t]*=[ \t]*(?:anyconnect|nc|gp|pulse|f5|fortinet)[ \t]*$)",
                                               QRegularExpression::MultilineOption);
        if (cliRe.match(str).hasMatch() || fileRe.match(str).hasMatch()) return true;
        for (const auto &line : str.split('\n')) {
            const auto trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
            return trimmed.startsWith("openconnect ");
        }
        return false;
    }

    void RawUpdater::update(const QString &str, bool needParse, bool isBase64Decoded) {
        if (!isBase64Decoded) {
            if (auto str2 = DecodeB64IfValid(str); !str2.isEmpty()) {
                update(str2, true, true);
                return;
            }
        }

        std::shared_ptr<Configs::Profile> ent;

        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(str.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError) {
            // Xray first: its configs share the "outbounds" wrapper with sing-box.
            auto xrayType = getXraySubType(doc);
            if (xrayType == XraySubType::outboundObject) {
                if (auto e = makeProfileForXrayOutbound(doc.object()); e != nullptr) {
                    updated_order += e;
                }
                return;
            }
            if (xrayType == XraySubType::outboundInJson || xrayType == XraySubType::outboundJsonArray ||
                xrayType == XraySubType::configJsonArray) {
                updateXray(doc, xrayType);
                return;
            }

            auto subType = getSingBoxSubType(doc);
            if (subType == SingBoxSubType::fullConfig) {
                ent = Configs::ProfilesRepo::NewProfile("custom");
                ent->Custom()->type = Configs::Custom::CustomFullConfig;
                ent->Custom()->config = str;
                updated_order += ent;
            } else if (subType == SingBoxSubType::outboundObject) {
                ent = Configs::ProfilesRepo::NewProfile("custom");
                ent->Custom()->type = Configs::Custom::CustomOutbound;
                ent->Custom()->config = str;
                updated_order += ent;
            } else if (subType == SingBoxSubType::outboundInJson || subType == SingBoxSubType::outboundJsonArray) {
                updateSingBox(doc, subType);
                return;
            }

            if (str.contains("version") && str.contains("servers"))
            {
                updateSIP008(str);
                return;
            }

            return;
        }

        if (str.contains("proxies:")) {
            updateClash(str);
            return;
        }

        if (str.contains("[Interface]") && str.contains("[Peer]"))
        {
            updateWireguardFileConfig(str);
            return;
        }

        if (looksLikeOvpnConfig(str)) {
            updateOpenVPNFileConfig(str);
            return;
        }

        if (looksLikeOpenConnectProfile(str)) {
            updateOpenConnectProfile(str);
            return;
        }

        if (str.count("\n") > 0 && needParse) {
            auto list = Disect(str);
            for (const auto &str2: list) {
                update(str2.trimmed(), false);
            }
            return;
        }

        if (str.startsWith("//") || str.startsWith("#") || str.length() < 2) {
            return;
        }

        if (str.startsWith("json://")) {
            auto link = QUrl(str);
            if (!link.isValid()) return;
            auto dataBytes = DecodeB64IfValid(link.fragment().toUtf8(), QByteArray::Base64UrlEncoding);
            if (dataBytes.isEmpty()) return;
            auto data = QJsonDocument::fromJson(dataBytes).object();
            if (data.isEmpty()) return;
            if (data.contains("protocol")) {
                ent = Configs::ProfilesRepo::NewProfile("xray" + data["protocol"].toString());
            } else {
                ent = data["type"].toString() == "hysteria2" ? Configs::ProfilesRepo::NewProfile("hysteria") : Configs::ProfilesRepo::NewProfile(data["type"].toString());
            }
            if (ent->outbound->invalid) return;
            ent->outbound->ParseFromJson(data);
        }

        if (str.startsWith("throne://add/", Qt::CaseInsensitive)) {
            auto link = QUrl(str);
            if (!link.isValid()) return;
            auto dataBytes = DecodeB64IfValid(link.path().mid(1));
            if (dataBytes.isEmpty()) return;
            auto data = QJsonDocument::fromJson(dataBytes).object();
            if (data.isEmpty()) return;
            if (data.contains("protocol")) {
                ent = Configs::ProfilesRepo::NewProfile("xray" + data["protocol"].toString());
            } else {
                ent = data["type"].toString() == "hysteria2" ? Configs::ProfilesRepo::NewProfile("hysteria") : Configs::ProfilesRepo::NewProfile(data["type"].toString());
            }
            if (ent->outbound->invalid) return;
            ent->outbound->ParseFromJson(data);
        }

        if (str.startsWith('{')) {
            ent = Configs::ProfilesRepo::NewProfile("custom");
            auto custom = ent->Custom();
            auto obj = QString2QJsonObject(str);
            if (obj.contains("outbounds")) {
                custom->type = Configs::Custom::CustomFullConfig;
                custom->config = str;
            } else if (obj.contains("server")) {
                custom->type = Configs::Custom::CustomOutbound;
                custom->config = str;
            } else {
                return;
            }
        }

        if (str.startsWith("socks5://") || str.startsWith("socks4://") ||
            str.startsWith("socks4a://") || str.startsWith("socks://")) {
            ent = Configs::ProfilesRepo::NewProfile("socks");
            auto ok = ent->Socks()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("http://") || str.startsWith("https://")) {
            ent = Configs::ProfilesRepo::NewProfile("http");
            auto ok = ent->Http()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("ss://")) {
            ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
            auto ok = ent->ShadowSocks()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("vmess://")) {
            ent = Configs::ProfilesRepo::NewProfile("vmess");
            auto ok = ent->VMess()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("vless://")) {
            if (Configs::useXrayVless(str)) {
                ent = Configs::ProfilesRepo::NewProfile("xrayvless");
                auto ok = ent->XrayVLESS()->ParseFromLink(str);
                if (!ok) return;
            } else {
                ent = Configs::ProfilesRepo::NewProfile("vless");
                auto ok = ent->VLESS()->ParseFromLink(str);
                if (!ok) return;
            }
        }

        if (str.startsWith("trojan://")) {
            ent = Configs::ProfilesRepo::NewProfile("trojan");
            auto ok = ent->Trojan()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("anytls://")) {
            ent = Configs::ProfilesRepo::NewProfile("anytls");
            auto ok = ent->AnyTLS()->ParseFromLink(str);
            if (!ok) return;
        }

        // mierus:// is the "simple" link; base64 mieru:// is rejected inside ParseFromLink.
        if (str.startsWith("mierus://") || str.startsWith("mieru://")) {
            ent = Configs::ProfilesRepo::NewProfile("mieru");
            auto ok = ent->Mieru()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("snell://")) {
            ent = Configs::ProfilesRepo::NewProfile("snell");
            auto ok = ent->Snell()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("hysteria://") || str.startsWith("hysteria2://") || str.startsWith("hy2://")) {
            ent = Configs::ProfilesRepo::NewProfile("hysteria");
            auto ok = ent->Hysteria()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("tuic://")) {
            ent = Configs::ProfilesRepo::NewProfile("tuic");
            auto ok = ent->TUIC()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("juicity://")) {
            ent = Configs::ProfilesRepo::NewProfile("juicity");
            auto ok = ent->Juicity()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("tt://")) {
            ent = Configs::ProfilesRepo::NewProfile("trusttunnel");
            auto ok = ent->TrustTunnel()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("shadowtls://")) {
            ent = Configs::ProfilesRepo::NewProfile("shadowtls");
            auto ok = ent->ShadowTLS()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("wg://")) {
            ent = Configs::ProfilesRepo::NewProfile("wireguard");
            auto ok = ent->Wireguard()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("ssh://")) {
            ent = Configs::ProfilesRepo::NewProfile("ssh");
            auto ok = ent->SSH()->ParseFromLink(str);
            if (!ok) return;
        }

        if (str.startsWith("naive+https://") || str.startsWith("naive+quic://")) {
            ent = Configs::ProfilesRepo::NewProfile("naive");
            auto ok = ent->Naive()->ParseFromLink(str);
            if (!ok) return;
        }

        if (ent == nullptr) return;

        updated_order += ent;
    }

    void RawUpdater::updateSingBox(const QJsonDocument &doc, SingBoxSubType type)
    {
        QJsonArray outbounds, endpoints;
        if (type == SingBoxSubType::outboundInJson) {
            auto json = doc.object();
            outbounds = json["outbounds"].toArray();
            endpoints = json["endpoints"].toArray();
        } else if (type == SingBoxSubType::outboundJsonArray) {
            outbounds = doc.array();
        } else {
            return;
        }
        QJsonArray items;
        for (const auto& outbound : outbounds)
        {
            if (!outbound.isObject()) continue;
            items.append(outbound.toObject());
        }
        for (const auto& endpoint : endpoints)
        {
            if (!endpoint.isObject()) continue;
            items.append(endpoint.toObject());
        }

        for (const auto& o : items)
        {
            auto out = o.toObject();
            if (out.isEmpty())
            {
                MW_show_log("invalid outbound of type: " + o.type());
                continue;
            }

            std::shared_ptr<Configs::Profile> ent;

            if (out["type"] == "socks") {
                ent = Configs::ProfilesRepo::NewProfile("socks");
                auto ok = ent->Socks()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "http") {
                ent = Configs::ProfilesRepo::NewProfile("http");
                auto ok = ent->Http()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "shadowsocks") {
                ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
                auto ok = ent->ShadowSocks()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "vmess") {
                ent = Configs::ProfilesRepo::NewProfile("vmess");
                auto ok = ent->VMess()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "vless") {
                ent = Configs::ProfilesRepo::NewProfile("vless");
                auto ok = ent->VLESS()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "trojan") {
                ent = Configs::ProfilesRepo::NewProfile("trojan");
                auto ok = ent->Trojan()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "anytls") {
                ent = Configs::ProfilesRepo::NewProfile("anytls");
                auto ok = ent->AnyTLS()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "mieru") {
                ent = Configs::ProfilesRepo::NewProfile("mieru");
                auto ok = ent->Mieru()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "snell") {
                ent = Configs::ProfilesRepo::NewProfile("snell");
                auto ok = ent->Snell()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "hysteria" || out["type"] == "hysteria2") {
                ent = Configs::ProfilesRepo::NewProfile("hysteria");
                auto ok = ent->Hysteria()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "tuic") {
                ent = Configs::ProfilesRepo::NewProfile("tuic");
                auto ok = ent->TUIC()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "juicity") {
                ent = Configs::ProfilesRepo::NewProfile("juicity");
                auto ok = ent->Juicity()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "trusttunnel") {
                ent = Configs::ProfilesRepo::NewProfile("trusttunnel");
                auto ok = ent->TrustTunnel()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "shadowtls") {
                ent = Configs::ProfilesRepo::NewProfile("shadowtls");
                auto ok = ent->ShadowTLS()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "wireguard") {
                ent = Configs::ProfilesRepo::NewProfile("wireguard");
                auto ok = ent->Wireguard()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "ssh") {
                ent = Configs::ProfilesRepo::NewProfile("ssh");
                auto ok = ent->SSH()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (out["type"] == "naive") {
                ent = Configs::ProfilesRepo::NewProfile("naive");
                auto ok = ent->Naive()->ParseFromJson(out);
                if (!ok) continue;
            }

            if (ent == nullptr) continue;

            updated_order += ent;
        }
    }

    void RawUpdater::updateXray(const QJsonDocument &doc, XraySubType type)
    {
        // Each element is a self-contained config (balancers, dialerProxy chains) that must run verbatim.
        if (type == XraySubType::configJsonArray) {
            for (const auto &c : doc.array()) {
                if (!c.isObject()) continue;
                auto cfg = c.toObject();
                if (!cfg.contains("outbounds")) continue;
                // Throne injects its own bridge inbound; the bundled ones only risk port-bind conflicts.
                cfg.remove("inbounds");
                auto ent = Configs::ProfilesRepo::NewProfile("custom");
                ent->Custom()->type = Configs::Custom::CustomXrayFullConfig;
                ent->Custom()->config = QJsonObject2QString(cfg, false);
                if (auto remarks = cfg["remarks"].toString(); !remarks.isEmpty()) ent->Custom()->name = remarks;
                updated_order += ent;
            }
            return;
        }

        QJsonArray outbounds;
        if (type == XraySubType::outboundInJson) {
            outbounds = doc.object()["outbounds"].toArray();
        } else if (type == XraySubType::outboundJsonArray) {
            outbounds = doc.array();
        } else {
            return;
        }
        for (const auto &o : outbounds) {
            if (!o.isObject()) continue;
            if (auto e = makeProfileForXrayOutbound(o.toObject()); e != nullptr) {
                updated_order += e;
            }
        }
    }

    // A NUL in the first four bytes makes fkYAML decode as UTF-16/32 and read out of bounds (#1746).
    std::string sanitizeClashYaml(const QString &str) {
        QString normalized = str;
        normalized.remove(QChar(QChar::Null));
        if (normalized.startsWith(QChar(QChar::ByteOrderMark))) normalized.remove(0, 1);
        return normalized.toStdString();
    }

    void RawUpdater::updateClash(const QString& str)
    {
        try {
            fkyaml::node node = fkyaml::node::deserialize(sanitizeClashYaml(str));
            clash::Clash clash_config = node.get_value<clash::Clash>();
    
            for (const auto& out : clash_config.proxies)
            {
                std::shared_ptr<Configs::Profile> ent;
    
                if (out.type == "socks5") {
                    ent = Configs::ProfilesRepo::NewProfile("socks");
                    auto ok = ent->Socks()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "http") {
                    ent = Configs::ProfilesRepo::NewProfile("http");
                    auto ok = ent->Http()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "ss") {
                    ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
                    auto ok = ent->ShadowSocks()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "vmess") {
                    ent = Configs::ProfilesRepo::NewProfile("vmess");
                    auto ok = ent->VMess()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "vless") {
                    if (out.network == "xhttp" || (!out.encryption.empty() && out.encryption != "none")) {
                        ent = Configs::ProfilesRepo::NewProfile("xrayvless");
                        auto ok = ent->XrayVLESS()->ParseFromClash(out);
                        if (!ok) continue;
                    } else {
                        ent = Configs::ProfilesRepo::NewProfile("vless");
                        auto ok = ent->VLESS()->ParseFromClash(out);
                        if (!ok) continue;
                    }
                }
    
                if (out.type == "trojan") {
                    ent = Configs::ProfilesRepo::NewProfile("trojan");
                    auto ok = ent->Trojan()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "anytls") {
                    ent = Configs::ProfilesRepo::NewProfile("anytls");
                    auto ok = ent->AnyTLS()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "hysteria" || out.type == "hysteria2") {
                    ent = Configs::ProfilesRepo::NewProfile("hysteria");
                    auto ok = ent->Hysteria()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "tuic") {
                    ent = Configs::ProfilesRepo::NewProfile("tuic");
                    auto ok = ent->TUIC()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (out.type == "snell") {
                    ent = Configs::ProfilesRepo::NewProfile("snell");
                    auto ok = ent->Snell()->ParseFromClash(out);
                    if (!ok) continue;
                }

                if (out.type == "ssh") {
                    ent = Configs::ProfilesRepo::NewProfile("ssh");
                    auto ok = ent->SSH()->ParseFromClash(out);
                    if (!ok) continue;
                }
    
                if (ent == nullptr) continue;
    
                updated_order += ent;
            }
        // fkYAML can throw beyond fkyaml::exception on hostile input (bad_alloc, length_error).
        } catch (const std::exception &ex) {
            auto msg = QString::fromUtf8(ex.what());
            runOnUiThread([=] {
                MessageBoxWarning("YAML Exception", msg);
            });
        } catch (...) {
            runOnUiThread([] {
                MessageBoxWarning("YAML Exception", QObject::tr("Failed to parse the Clash configuration."));
            });
        }
    }

    void RawUpdater::updateWireguardFileConfig(const QString& str)
    {
        auto ent = Configs::ProfilesRepo::NewProfile("wireguard");
        auto ok = ent->Wireguard()->ParseFromLink(str);
        if (!ok) return;
        updated_order += ent;
    }

    void RawUpdater::updateOpenVPNFileConfig(const QString& str)
    {
        QStringList problems;
        auto ent = Configs::ProfilesRepo::NewProfile("openvpn");
        auto ok = Configs::ParseOvpnConfig(str, *ent->OpenVPN(), &problems);
        for (const auto &problem : problems) MW_show_log("OpenVPN: " + problem);
        if (!ok) {
            MW_show_log(QObject::tr("Failed to import the OpenVPN profile."));
            return;
        }
        updated_order += ent;
    }

    void RawUpdater::updateOpenConnectProfile(const QString& str)
    {
        QStringList problems;
        if (str.contains("<AnyConnectProfile") || str.contains("<ServerList")) {
            QList<std::shared_ptr<Configs::openconnect>> hosts;
            auto ok = Configs::ParseAnyConnectXml(str, hosts, &problems);
            for (const auto &problem : problems) MW_show_log("OpenConnect: " + problem);
            if (!ok) {
                MW_show_log(QObject::tr("Failed to import the OpenConnect profile."));
                return;
            }
            for (const auto &host : hosts) {
                auto ent = Configs::ProfilesRepo::NewProfile("openconnect");
                ent->outbound = host;
                updated_order += ent;
            }
            return;
        }

        auto ent = Configs::ProfilesRepo::NewProfile("openconnect");
        auto ok = Configs::ParseOpenConnectProfile(str, *ent->OpenConnect(), &problems);
        for (const auto &problem : problems) MW_show_log("OpenConnect: " + problem);
        if (!ok) {
            MW_show_log(QObject::tr("Failed to import the OpenConnect profile."));
            return;
        }
        updated_order += ent;
    }

    void RawUpdater::updateSIP008(const QString& str)
    {
        auto json = QString2QJsonObject(str);

        for (const auto& o : json["servers"].toArray())
        {
            auto out = o.toObject();
            if (out.isEmpty())
            {
                MW_show_log("invalid server object");
                continue;
            }

            auto ent = Configs::ProfilesRepo::NewProfile("shadowsocks");
            auto ok = ent->ShadowSocks()->ParseFromSIP008(out);
            if (!ok) continue;
            updated_order += ent;
        }
    }

    void GroupUpdater::AsyncUpdate(const QString &str, int _sub_gid, const std::function<void()> &finish, bool showDiff) {
        auto content = str.trimmed();
        bool asURL = false;
        bool createNewGroup = false;

        if (_sub_gid < 0 && (content.startsWith("http://") || content.startsWith("https://"))) {
            auto items = QStringList{
                QObject::tr("Add profiles to this group"),
                QObject::tr("Create new subscription group"),
                QObject::tr("Import HTTP proxy profile"),
            };
            bool ok;
            auto a = QInputDialog::getItem(nullptr,
                                           QObject::tr("url detected"),
                                           QObject::tr("%1\nHow to update?").arg(content),
                                           items, 0, false, &ok);
            if (!ok) return;
            switch (items.indexOf(a)) {
                case 1: createNewGroup = true;
                case 0: asURL = true; break;
            }
        }

        runOnNewThread([=,this] {
            auto gid = _sub_gid;
            if (createNewGroup) {
                auto group = Configs::GroupsRepo::NewGroup();
                group->name = QUrl(str).host();
                group->url = str;
                Configs::dataManager->groupsRepo->AddGroup(group);
                gid = group->id;
                MW_dialog_message(MwMessage::SubscriptionNewGroup, {});
            }
            Update(str, gid, asURL, showDiff);
            emit asyncUpdateCallback(gid);
            if (finish != nullptr) finish();
        });
    }

    void GroupUpdater::AsyncImportBatch(const QStringList &payloads, const std::function<void()> &finish) {
        if (payloads.isEmpty()) return;

        runOnNewThread([=,this] {
            Configs::dataManager->settingsRepo->imported_count = 0;
            auto rawUpdater = std::make_unique<RawUpdater>();

            MW_show_log(">>>>>>>> " + QObject::tr("Processing subscription data..."));
            for (const auto &payload: payloads) {
                rawUpdater->update(payload.trimmed());
            }
            Configs::dataManager->profilesRepo->AddProfileBatch(rawUpdater->updated_order, rawUpdater->gid_add_to);
            MW_show_log(">>>>>>>> " + QObject::tr("Process complete, applying..."));

            Configs::dataManager->settingsRepo->imported_count = rawUpdater->updated_order.count();
            MW_dialog_message(MwMessage::SubscriptionFinished, {});
            emit asyncUpdateCallback(rawUpdater->gid_add_to);
            if (finish != nullptr) finish();
        });
    }

    // BatchDeleteProfiles silently drops the running profile from the list it was handed (#1753).
    struct DeleteOutcome {
        bool ok = false;
        QList<int> deleted;
        QList<int> kept;
    };

    DeleteOutcome deleteProfiles(QList<int> ids) {
        DeleteOutcome outcome;
        const QSet<int> requested(ids.begin(), ids.end());
        outcome.ok = Configs::dataManager->profilesRepo->BatchDeleteProfiles(
            ids, Configs::dataManager->settingsRepo->allow_stopping_active_profile);
        const QSet<int> deleted(ids.begin(), ids.end());
        outcome.deleted = std::move(ids);
        for (int id : requested) {
            if (!deleted.contains(id)) outcome.kept << id;
        }
        return outcome;
    }

    int ParseUpdateInterval(const QString &headerStr) {
        if (headerStr.trimmed().isEmpty()) return 0;
        QString s = headerStr.trimmed().remove('"').remove('\'').toLower();
        bool ok = false;
        if (s.endsWith("h")) {
            int h = s.chopped(1).toInt(&ok);
            if (ok && h > 0) return h;
        } else if (s.endsWith("d")) {
            int d = s.chopped(1).toInt(&ok);
            if (ok && d > 0) return d * 24;
        } else if (s.endsWith("s")) {
            int sec = s.chopped(1).toInt(&ok);
            if (ok && sec > 0) return qMax(1, sec / 3600);
        }
        int num = s.toInt(&ok);
        if (ok && num > 0) {
            if (num > 1000) return qMax(1, num / 3600); 
            return num; 
        }
        
        return 0;
    }

    QString decode3XUIHeader(const QString &raw) {
        QString str = raw.trimmed();
        if (str.startsWith("base64:", Qt::CaseInsensitive)) {
            QByteArray decoded = QByteArray::fromBase64(str.mid(7).trimmed().toUtf8());
            if (!decoded.isEmpty()) return QString::fromUtf8(decoded).trimmed();
        }
        return str;
    }


void GroupUpdater::Update(const QString &_str, int _sub_gid, bool _not_sub_as_url, bool showDiff) {
        Configs::dataManager->settingsRepo->imported_count = 0;
        auto rawUpdater = std::make_unique<RawUpdater>();
        rawUpdater->gid_add_to = _sub_gid;

        QString sub_user_info;
        int sub_interval_header = 0;
        QString profile_title;
        QString web_page_url;
        QString support_url;
        QString announce_msg;
        bool asURL = _sub_gid >= 0 || _not_sub_as_url;
        auto content = _str.trimmed();
        auto group = Configs::dataManager->groupsRepo->GetGroup(_sub_gid);
        if (group != nullptr && group->archive) return;

        if (asURL) {
            auto groupName = group == nullptr ? content : group->name;
            MW_show_log(">>>>>>>> " + QObject::tr("Requesting subscription: %1").arg(groupName));

            auto resp = NetworkRequestHelper::HttpGet(content, Configs::dataManager->settingsRepo->sub_send_hwid);
            if (!resp.error.isEmpty()) {
                MW_show_log("<<<<<<<< " + QObject::tr("Requesting subscription %1 error: %2").arg(groupName, resp.error + "\n" + resp.data));
                return;
            }

            content = resp.data;

            // 1. Extract from HTTP Headers
            sub_user_info = NetworkRequestHelper::GetHeader(resp.header, "Subscription-UserInfo");
            if (sub_user_info.isEmpty()) {
                sub_user_info = NetworkRequestHelper::GetHeader(resp.header, "Subscription-Userinfo");
            }

            QString intervalHeader = NetworkRequestHelper::GetHeader(resp.header, "profile-update-interval");
            if (intervalHeader.isEmpty()) {
                intervalHeader = NetworkRequestHelper::GetHeader(resp.header, "x-profile-update-interval");
            }
            sub_interval_header = ParseUpdateInterval(intervalHeader);

            profile_title = decode3XUIHeader(NetworkRequestHelper::GetHeader(resp.header, "Profile-Title"));
            web_page_url = NetworkRequestHelper::GetHeader(resp.header, "Profile-Web-Page-Url");
            support_url = NetworkRequestHelper::GetHeader(resp.header, "Support-Url");
            if (support_url.isEmpty()) support_url = NetworkRequestHelper::GetHeader(resp.header, "support-url");
            announce_msg = decode3XUIHeader(NetworkRequestHelper::GetHeader(resp.header, "Announce"));

            // 2. Extract from in-body comments (#profile-title, #announce, etc.)
            for (const auto &line : content.split('\n')) {
                const auto trimmed = line.trimmed();
                if (!trimmed.startsWith('#') && !trimmed.startsWith("//")) break;
                auto clean = trimmed.mid(trimmed.startsWith("//") ? 2 : 1).trimmed();

                if (sub_user_info.isEmpty() && clean.startsWith("subscription-userinfo:", Qt::CaseInsensitive)) {
                    sub_user_info = clean.section(':', 1).trimmed();
                } else if (sub_interval_header <= 0 && clean.startsWith("profile-update-interval:", Qt::CaseInsensitive)) {
                    sub_interval_header = ParseUpdateInterval(clean.section(':', 1).trimmed());
                } else if (profile_title.isEmpty() && clean.startsWith("profile-title:", Qt::CaseInsensitive)) {
                    profile_title = decode3XUIHeader(clean.section(':', 1).trimmed());
                } else if (web_page_url.isEmpty() && clean.startsWith("profile-web-page-url:", Qt::CaseInsensitive)) {
                    web_page_url = clean.section(':', 1).trimmed();
                } else if (support_url.isEmpty() && clean.startsWith("support-url:", Qt::CaseInsensitive)) {
                    support_url = clean.section(':', 1).trimmed();
                } else if (announce_msg.isEmpty() && (clean.startsWith("announce:", Qt::CaseInsensitive) || clean.startsWith("notice:", Qt::CaseInsensitive))) {
                    announce_msg = decode3XUIHeader(clean.section(':', 1).trimmed());
                }
            }

            MW_show_log("<<<<<<<< " + QObject::tr("Subscription request fininshed: %1").arg(groupName));
            if (sub_interval_header > 0) {
                MW_show_log(QObject::tr("Subscription server update interval: %1 hour(s)").arg(sub_interval_header));
            }
        }


        QList<std::shared_ptr<Configs::Profile>> in;
        // Auto selectors are local state, not servers the remote sent: keep them out of the diff.
        QList<QPair<int, int>> sticky; // (position in the group, profile id)
        QSet<int> stickyIDs;
        // Ids a running auto selector can no longer trust: deleted, or same id with new settings.
        QList<int> disturbed;
        bool cleared = false;

        if (group != nullptr) {
            group->sub_last_update = QDateTime::currentSecsSinceEpoch();

            if (!profile_title.isEmpty() && (group->name.isEmpty() || group->name == QUrl(group->url).host())) {
                group->name = profile_title;
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }

            QString fullInfo = sub_user_info;
            if (!profile_title.isEmpty()) fullInfo += (fullInfo.isEmpty() ? "" : "; ") + QString("title=") + profile_title;
            if (!web_page_url.isEmpty()) fullInfo += (fullInfo.isEmpty() ? "" : "; ") + QString("web_url=") + web_page_url;
            if (!support_url.isEmpty()) fullInfo += (fullInfo.isEmpty() ? "" : "; ") + QString("support_url=") + support_url;
            if (!announce_msg.isEmpty()) fullInfo += (fullInfo.isEmpty() ? "" : "; ") + QString("announce=") + announce_msg;
            
            if (!fullInfo.isEmpty()) {
                group->info = fullInfo;
            }

            if (group->sub_update_interval == 0 && sub_interval_header > 0) {
                group->sub_update_interval = sub_interval_header;
            }
            Configs::dataManager->groupsRepo->Save(group);
            for (int i = 0; i < group->profiles.size(); i++) {
                auto ent = Configs::dataManager->profilesRepo->GetProfile(group->profiles[i]);
                if (ent == nullptr || ent->type != "autoselector") continue;
                sticky << qMakePair(i, group->profiles[i]);
                stickyIDs.insert(group->profiles[i]);
            }
            if (Configs::dataManager->settingsRepo->sub_clear) {
                MW_show_log(QObject::tr("Clearing servers..."));
                QList<int> clear_ids;
                for (int id : group->profiles) {
                    if (!stickyIDs.contains(id)) clear_ids << id;
                }
                const auto outcome = deleteProfiles(clear_ids);
                if (!outcome.ok) {
                    runOnUiThread([=] {
                        MessageBoxWarning("Internal Error", "DB Error when deleting profiles, Please try again.");
                    });
                    return;
                }
                disturbed = outcome.deleted;
                // A survivor still belongs to the subscription: fall through to the diff.
                cleared = outcome.kept.isEmpty();
            }
            if (!cleared) {
                for (const auto &ent : Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
                    if (ent != nullptr && !stickyIDs.contains(ent->id)) in << ent;
                }
            }
        }

        MW_show_log(">>>>>>>> " + QObject::tr("Processing subscription data..."));
        rawUpdater->update(content);
        content.clear();
        Configs::dataManager->profilesRepo->AddProfileBatch(rawUpdater->updated_order, rawUpdater->gid_add_to);
        MW_show_log(">>>>>>>> " + QObject::tr("Process complete, applying..."));

        if (group != nullptr) {
            QList<std::shared_ptr<Configs::Profile>> out_all;
            for (const auto &ent : Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
                if (ent != nullptr && !stickyIDs.contains(ent->id)) out_all << ent;
            }

            QString change_text;

            if (cleared) {
                if (out_all.size() >= 1000) {
                    change_text += "[+] " + Int2String(out_all.size()) + " profiles\n";
                } else {
                    for (const auto &ent: out_all) {
                        change_text += "[+] " + ent->outbound->DisplayTypeAndName() + "\n";
                    }
                }
            } else {
                QList<std::shared_ptr<Configs::Profile>> update_keep;
                QList<std::shared_ptr<Configs::Profile>> update_del;
                QList<std::shared_ptr<Configs::Profile>> only_out;
                QList<std::shared_ptr<Configs::Profile>> only_in;
                QList<std::shared_ptr<Configs::Profile>> out;
                Configs::ProfileFilter::OnlyInSrc_ByPointer(out_all, in, out);
                Configs::ProfileFilter::OnlyInSrc(in, out, only_in, false);
                Configs::ProfileFilter::OnlyInSrc(out, in, only_out, false);
                Configs::ProfileFilter::Common(in, out, update_keep, update_del, false);

                QList<std::shared_ptr<Configs::Profile>> changed_old;
                QList<std::shared_ptr<Configs::Profile>> changed_new;
                Configs::ProfileFilter::ChangedByIdentity(only_in, only_out, changed_old, changed_new);
                const auto make_notice = [](const auto &profiles, const QString &prefix, const QString &action) {
                    if (profiles.size() >= 1000) {
                        return QStringLiteral("%1 %2 %3\n")
                            .arg(prefix, action)
                            .arg(profiles.size());
                    }

                    QString result;
                    for (const auto &ent : profiles) {
                        result += prefix;
                        result += ' ';
                        result += ent->outbound->DisplayTypeAndName();
                        result += '\n';
                    }
                    return result;
                };

                const QString notice_added = make_notice(only_out, "[+]", "added");
                const QString notice_deleted = make_notice(only_in, "[-]", "deleted");
                const QString notice_updated = make_notice(changed_new, "[~]", "updated");

                QHash<Configs::Profile *, int> supersededBy;
                for (int i = 0; i < update_del.size() && i < update_keep.size(); ++i) {
                    supersededBy[update_del[i].get()] = update_keep[i]->id;
                }
                for (int i = 0; i < changed_new.size(); ++i) {
                    const auto &oldEnt = changed_old[i];
                    oldEnt->outbound = changed_new[i]->outbound;
                    oldEnt->name = oldEnt->outbound->name;
                    Configs::dataManager->profilesRepo->Save(oldEnt);
                    supersededBy[changed_new[i].get()] = oldEnt->id;
                    disturbed << oldEnt->id;
                }

                const auto previousOrder = group->profiles;
                group->profiles.clear();
                for (const auto &ent: rawUpdater->updated_order) {
                    auto it = supersededBy.find(ent.get());
                    if (it != supersededBy.end()) {
                        group->profiles.append(it.value());
                    } else {
                        group->profiles.append(ent->id);
                    }
                }
                for (const auto &[position, id] : sticky) {
                    group->profiles.insert(std::min<qsizetype>(position, group->profiles.size()), id);
                }
                Configs::dataManager->groupsRepo->Save(group);

                QList<int> del_ids;
                for (const auto &ent: out_all) {
                    if (!group->HasProfile(ent->id)) {
                        del_ids.append(ent->id);
                    }
                }
                const auto outcome = deleteProfiles(del_ids);
                if (!outcome.ok) {
                    runOnUiThread([=] {
                       MessageBoxWarning("Internal error", "DB Error when deleting profiles, data may be corrupted");
                    });
                }
                disturbed << outcome.deleted;

                // Nothing rebuilds group->profiles from the rows: a survivor left out here is orphaned.
                QString notice_kept;
                for (int id : outcome.kept) {
                    if (group->HasProfile(id)) continue;
                    const auto position = previousOrder.indexOf(id);
                    group->profiles.insert(position < 0 ? group->profiles.size()
                                                        : std::min<qsizetype>(position, group->profiles.size()), id);
                    if (auto ent = Configs::dataManager->profilesRepo->GetProfile(id); ent != nullptr) {
                        notice_kept += "[=] " + ent->outbound->DisplayTypeAndName() + "\n";
                    }
                }
                if (!outcome.kept.isEmpty()) Configs::dataManager->groupsRepo->Save(group);

                change_text = "\n" + QObject::tr("Added %1 profiles:\n%2\nUpdated %3 profiles:\n%4\nDeleted %5 Profiles:\n%6")
                                         .arg(only_out.length())
                                         .arg(notice_added)
                                         .arg(changed_old.length())
                                         .arg(notice_updated)
                                         .arg(only_in.length())
                                         .arg(notice_deleted);
                if (!notice_kept.isEmpty()) {
                    change_text += "\n" + QObject::tr("Still in use, so kept instead of deleted:\n%1").arg(notice_kept);
                }
                if (only_out.length() + only_in.length() + changed_old.length() == 0) change_text = QObject::tr("Nothing");
            }

            MW_show_log("<<<<<<<< " + QObject::tr("Change of %1:").arg(group->name) + "\n" + change_text);
            if (showDiff && Configs::dataManager->settingsRepo->sub_show_change_popup) {
                const auto diffTitle = QObject::tr("Change of %1").arg(group->name);
                auto diffBody = change_text.trimmed();
                if (diffBody.isEmpty()) diffBody = QObject::tr("Nothing");
                runOnUiThread([diffTitle, diffBody] { MessageBoxScrollable(diffTitle, diffBody); });
            }
            // Auto selectors resolve members from the group at build time, so a refresh can invalidate an untouched one.
            QStringList selectorArgs{Int2String(group->id)};
            for (int id : disturbed) selectorArgs << Int2String(id);
            MW_dialog_message(MwMessage::SubscriptionGroupChanged, selectorArgs);
            MW_dialog_message(MwMessage::SubscriptionFinished, {MwArg::Quiet});
        } else {
            Configs::dataManager->settingsRepo->imported_count = rawUpdater->updated_order.count();
            MW_dialog_message(MwMessage::SubscriptionFinished, {});
        }
    }
} // namespace Subscription

bool UI_update_all_groups_Updating = false;

#define should_skip_group(g) (g == nullptr || g->url.isEmpty() || g->archive || (onlyAllowed && g->skip_auto_update))

void serialUpdateSubscription(const QList<int> &groupsTabOrder, int _order, bool onlyAllowed) {
    if (_order >= groupsTabOrder.size()) {
        UI_update_all_groups_Updating = false;
        return;
    }

    auto group = Configs::dataManager->groupsRepo->GetGroup(groupsTabOrder[_order]);
    if (group == nullptr || should_skip_group(group)) {
        serialUpdateSubscription(groupsTabOrder, _order + 1, onlyAllowed);
        return;
    }

    int nextOrder = _order + 1;
    while (nextOrder < groupsTabOrder.size()) {
        auto nextGid = groupsTabOrder[nextOrder];
        auto nextGroup = Configs::dataManager->groupsRepo->GetGroup(nextGid);
        if (!should_skip_group(nextGroup)) {
            break;
        }
        nextOrder += 1;
    }

    UI_update_all_groups_Updating = true;
    Subscription::groupUpdater->AsyncUpdate(group->url, group->id, [=] {
        serialUpdateSubscription(groupsTabOrder, nextOrder, onlyAllowed);
    });
}

void UI_update_all_groups(bool onlyAllowed) {
    if (UI_update_all_groups_Updating) {
        MW_show_log("The last subscription update has not exited.");
        return;
    }

    auto groupsTabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    serialUpdateSubscription(groupsTabOrder, 0, onlyAllowed);
}

void UI_check_auto_update_groups() {
    if (UI_update_all_groups_Updating) return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto globalMinutes = Configs::dataManager->settingsRepo->sub_auto_update;
    const bool globalEnabled = globalMinutes >= 30;
    const auto tabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();

    QList<int> dueGroups;
    for (int gid : tabOrder) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group == nullptr || group->url.isEmpty() || group->archive || group->skip_auto_update) continue;

        qint64 intervalSecs = 0;
        if (group->sub_update_interval > 0) {
            intervalSecs = static_cast<qint64>(group->sub_update_interval) * 3600;
        } else if (globalEnabled) {
            intervalSecs = static_cast<qint64>(globalMinutes) * 60;
        }
        
        if (intervalSecs <= 0) continue;

        // If never updated or interval elapsed
        if (group->sub_last_update <= 0 || (now - group->sub_last_update) >= intervalSecs) {
            dueGroups << gid;
        }
    }

    if (!dueGroups.isEmpty()) {
        MW_show_log(QObject::tr("Auto-updating %1 due subscription(s)...").arg(dueGroups.size()));
        serialUpdateSubscription(dueGroups, 0, true);
    }
}