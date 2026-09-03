#pragma once

#include <QFrame>
#include <memory>

namespace Configs {
    class Group;
}

class QLabel;
class QProgressBar;
class QToolButton;

class SubscriptionInfoCard : public QFrame {
    Q_OBJECT
public:
    explicit SubscriptionInfoCard(QWidget *parent = nullptr);
    ~SubscriptionInfoCard() override = default;

    void setGroup(const std::shared_ptr<Configs::Group> &group);
    void applyTheme();
    [[nodiscard]] bool hasSubscription() const;

private:
    void setupUi();
    void updateData();

    std::shared_ptr<Configs::Group> m_group;

    // Line 1: Title (left) + Action Buttons (right)
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_btnPortal = nullptr;
    QToolButton *m_btnSupport = nullptr;
    QToolButton *m_btnUpdate = nullptr;

    // Line 2: Full-width Progress Bar
    QProgressBar *m_progressBar = nullptr;

    // Line 3: Expiry (left) + Announce (right)
    QLabel *m_expiryIcon = nullptr;
    QLabel *m_expiryLabel = nullptr;
    QWidget *m_announceContainer = nullptr;
    QLabel *m_announceIcon = nullptr;
    QLabel *m_announceLabel = nullptr;

    int m_announceOffset = 0;
};