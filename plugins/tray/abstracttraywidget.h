// SPDX-FileCopyrightText: 2011 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QWidget>
#include <QTimer>
#include <QPair>

class QDBusMessage;
class AbstractTrayWidget: public QWidget
{
    Q_OBJECT
public:
    enum TrayType {
        ApplicationTray,
        SystemTray,
    };

    explicit AbstractTrayWidget(QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags());
    virtual ~AbstractTrayWidget();

    virtual QString itemKeyForConfig() = 0;
    QString accessibleName() {return itemKeyForConfig().replace("sni:", "");}
    virtual void updateIcon() = 0;
    virtual void sendClick(uint8_t mouseButton, int x, int y) = 0;
    virtual inline TrayType trayType() const { return TrayType::ApplicationTray; } // default is ApplicationTray
    virtual bool isValid() {return true;}
    uint getOwnerPID();

Q_SIGNALS:
    void iconChanged();
    void clicked();
    void needAttention();
    void requestWindowAutoHide(const bool autoHide);
    void requestRefershWindowVisible();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

    void handleMouseRelease();
    const QRect perfectIconRect() const;
    void resizeEvent(QResizeEvent *event) override;
    void setOwnerPID(uint PID);

private:
    QTimer *m_handleMouseReleaseTimer;

    QPair<QPoint, Qt::MouseButton> m_lastMouseReleaseData;

    uint m_ownerPID;
};

