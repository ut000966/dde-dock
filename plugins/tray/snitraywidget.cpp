// SPDX-FileCopyrightText: 2011 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "snitraywidget.h"
#include "util/themeappicon.h"
#include "../../widgets/tipswidget.h"

#include <dbusmenu-qt5/dbusmenuimporter.h>

#include <DGuiApplicationHelper>

#include <QPainter>
#include <QApplication>
#include <QDBusPendingCall>
#include <QtConcurrent>
#include <QFuture>

#include <xcb/xproto.h>

DGUI_USE_NAMESPACE

#define IconSize 20

const QStringList ItemCategoryList {"ApplicationStatus", "Communications", "SystemServices", "Hardware"};
const QStringList ItemStatusList {"Passive", "Active", "NeedsAttention"};
QPointer<DockPopupWindow> SNITrayWidget::PopupWindow = nullptr;
Dock::Position SNITrayWidget::DockPosition = Dock::Position::Top;
using namespace Dock;
SNITrayWidget::SNITrayWidget(const QString &sniServicePath, QWidget *parent)
    : AbstractTrayWidget(parent)
    , m_dbusMenuImporter(nullptr)
    , m_menu(nullptr)
    , m_updateIconTimer(new QTimer(this))
    , m_updateOverlayIconTimer(new QTimer(this))
    , m_updateAttentionIconTimer(new QTimer(this))
    , m_sniServicePath(sniServicePath)
    , m_popupTipsDelayTimer(new QTimer(this))
    , m_handleMouseReleaseTimer(new QTimer(this))
    , m_tipsLabel(new TipsWidget(this))
{
    m_popupTipsDelayTimer->setInterval(500);
    m_popupTipsDelayTimer->setSingleShot(true);
    m_handleMouseReleaseTimer->setSingleShot(true);
    m_handleMouseReleaseTimer->setInterval(100);

    connect(m_handleMouseReleaseTimer, &QTimer::timeout, this, &SNITrayWidget::handleMouseRelease);
    connect(m_popupTipsDelayTimer, &QTimer::timeout, this, &SNITrayWidget::showHoverTips);

    if (PopupWindow.isNull()) {
        DockPopupWindow *arrowRectangle = new DockPopupWindow(nullptr);
        arrowRectangle->setShadowBlurRadius(20);
        arrowRectangle->setRadius(6);
        arrowRectangle->setShadowYOffset(2);
        arrowRectangle->setShadowXOffset(0);
        arrowRectangle->setArrowWidth(18);
        arrowRectangle->setArrowHeight(10);
        arrowRectangle->setObjectName("snitraypopup");
        PopupWindow = arrowRectangle;
        connect(qApp, &QApplication::aboutToQuit, PopupWindow, &DockPopupWindow::deleteLater);
    }

    if (m_sniServicePath.startsWith("/") || !m_sniServicePath.contains("/")) {
        qDebug() << "SNI service path invalid";
        return;
    }

    QPair<QString, QString> pair = serviceAndPath(m_sniServicePath);
    m_dbusService = pair.first;
    m_dbusPath = pair.second;

    QDBusConnection conn = QDBusConnection::sessionBus();
    setOwnerPID(conn.interface()->servicePid(m_dbusService));

    m_sniInter = new StatusNotifierItem(m_dbusService, m_dbusPath, QDBusConnection::sessionBus(), this);
    m_sniInter->setSync(false);
    if (!m_sniInter->isValid()) {
        qDebug() << "SNI dbus interface is invalid!" << m_dbusService << m_dbusPath << m_sniInter->lastError();
        return;
    }

    m_updateIconTimer->setInterval(100);
    m_updateIconTimer->setSingleShot(true);
    m_updateOverlayIconTimer->setInterval(500);
    m_updateOverlayIconTimer->setSingleShot(true);
    m_updateAttentionIconTimer->setInterval(1000);
    m_updateAttentionIconTimer->setSingleShot(true);

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &SNITrayWidget::refreshIcon);
    connect(m_updateIconTimer, &QTimer::timeout, this, &SNITrayWidget::refreshIcon);
    connect(m_updateOverlayIconTimer, &QTimer::timeout, this, &SNITrayWidget::refreshOverlayIcon);
    connect(m_updateAttentionIconTimer, &QTimer::timeout, this, &SNITrayWidget::refreshAttentionIcon);

    // SNI property change
    // thses signals of properties may not be emit automatically!!
    // since the SniInter in on async mode we can not call property's getter function to obtain property directly
    // the way to refresh properties(emit the following signals) is call property's getter function and wait these signals
    connect(m_sniInter, &StatusNotifierItem::AttentionIconNameChanged, this, &SNITrayWidget::onSNIAttentionIconNameChanged);
    connect(m_sniInter, &StatusNotifierItem::AttentionIconPixmapChanged, this, &SNITrayWidget::onSNIAttentionIconPixmapChanged);
    connect(m_sniInter, &StatusNotifierItem::AttentionMovieNameChanged, this, &SNITrayWidget::onSNIAttentionMovieNameChanged);
    connect(m_sniInter, &StatusNotifierItem::CategoryChanged, this, &SNITrayWidget::onSNICategoryChanged);
    connect(m_sniInter, &StatusNotifierItem::IconNameChanged, this, &SNITrayWidget::onSNIIconNameChanged);
    connect(m_sniInter, &StatusNotifierItem::IconPixmapChanged, this, &SNITrayWidget::onSNIIconPixmapChanged);
    connect(m_sniInter, &StatusNotifierItem::IconThemePathChanged, this, &SNITrayWidget::onSNIIconThemePathChanged);
    connect(m_sniInter, &StatusNotifierItem::IdChanged, this, &SNITrayWidget::onSNIIdChanged);
    connect(m_sniInter, &StatusNotifierItem::MenuChanged, this, &SNITrayWidget::onSNIMenuChanged);
    connect(m_sniInter, &StatusNotifierItem::OverlayIconNameChanged, this, &SNITrayWidget::onSNIOverlayIconNameChanged);
    connect(m_sniInter, &StatusNotifierItem::OverlayIconPixmapChanged, this, &SNITrayWidget::onSNIOverlayIconPixmapChanged);
    connect(m_sniInter, &StatusNotifierItem::StatusChanged, this, &SNITrayWidget::onSNIStatusChanged);

    connect(this, &SNITrayWidget::requestShowMenu, this, &SNITrayWidget::showContextMenu);

    // the following signals can be emit automatically
    // need refresh cached properties in these slots
    connect(m_sniInter, &StatusNotifierItem::NewIcon, [ = ] {
        m_sniIconName = m_sniInter->iconName();
        m_sniIconPixmap = m_sniInter->iconPixmap();
        m_sniIconThemePath = m_sniInter->iconThemePath();

        m_updateIconTimer->start();
    });
    connect(m_sniInter, &StatusNotifierItem::NewOverlayIcon, [ = ] {
        m_sniOverlayIconName = m_sniInter->overlayIconName();
        m_sniOverlayIconPixmap = m_sniInter->overlayIconPixmap();
        m_sniIconThemePath = m_sniInter->iconThemePath();

        m_updateOverlayIconTimer->start();
    });
    connect(m_sniInter, &StatusNotifierItem::NewAttentionIcon, [ = ] {
        m_sniAttentionIconName = m_sniInter->attentionIconName();
        m_sniAttentionIconPixmap = m_sniInter->attentionIconPixmap();
        m_sniIconThemePath = m_sniInter->iconThemePath();

        m_updateAttentionIconTimer->start();
    });
    connect(m_sniInter, &StatusNotifierItem::NewStatus, [ = ] {
        onSNIStatusChanged(m_sniInter->status());
    });

    initSNIPropertys();
}

QString SNITrayWidget::itemKeyForConfig()
{
    return QString("sni:%1").arg(m_sniId.isEmpty() ? m_sniServicePath : m_sniId);
}

void SNITrayWidget::updateIcon()
{
    m_updateIconTimer->start();
}

void SNITrayWidget::sendClick(uint8_t mouseButton, int x, int y)
{
    switch (mouseButton) {
    case XCB_BUTTON_INDEX_1:
    {
        QFuture<void> future = QtConcurrent::run([ = ] {
            StatusNotifierItem inter(m_dbusService, m_dbusPath, QDBusConnection::sessionBus());
            QDBusPendingReply<> reply = inter.Activate(x, y);
            // try to invoke context menu while calling activate get a error.
            // primarily work for apps using libappindicator.
            reply.waitForFinished();
            if (reply.isError()) {
                Q_EMIT requestShowMenu(x, y);
            }
        });
    }
        break;
    case XCB_BUTTON_INDEX_2:
        m_sniInter->SecondaryActivate(x, y);
        break;
    case XCB_BUTTON_INDEX_3:
        showContextMenu(x, y);
        break;
    default:
        qDebug() << "unknown mouse button key";
        break;
    }
}

bool SNITrayWidget::isValid()
{
    return m_sniInter->isValid();
}

SNITrayWidget::ItemStatus SNITrayWidget::status()
{
    if (!ItemStatusList.contains(m_sniStatus)) {
        m_sniStatus = "Active";
        return ItemStatus::Active;
    }

    return static_cast<ItemStatus>(ItemStatusList.indexOf(m_sniStatus));
}

SNITrayWidget::ItemCategory SNITrayWidget::category()
{
    if (!ItemCategoryList.contains(m_sniCategory)) {
        return UnknownCategory;
    }

    return static_cast<ItemCategory>(ItemCategoryList.indexOf(m_sniCategory));
}

QString SNITrayWidget::toSNIKey(const QString &sniServicePath)
{
    return QString("sni:%1").arg(sniServicePath);
}

bool SNITrayWidget::isSNIKey(const QString &itemKey)
{
    return itemKey.startsWith("sni:");
}

QPair<QString, QString> SNITrayWidget::serviceAndPath(const QString &servicePath)
{
    QStringList list = servicePath.split("/");
    QPair<QString, QString> pair;
    pair.first = list.takeFirst();

    for (auto i : list) {
        pair.second.append("/");
        pair.second.append(i);
    }

    return pair;
}

uint SNITrayWidget::servicePID(const QString &servicePath)
{
    QString serviceName = serviceAndPath(servicePath).first;
    QDBusConnection conn = QDBusConnection::sessionBus();
    return conn.interface()->servicePid(serviceName);
}

void SNITrayWidget::initSNIPropertys()
{
    m_sniAttentionIconName = m_sniInter->attentionIconName();
    m_sniAttentionIconPixmap = m_sniInter->attentionIconPixmap();
    m_sniAttentionMovieName = m_sniInter->attentionMovieName();
    m_sniCategory = m_sniInter->category();
    m_sniIconName = m_sniInter->iconName();
    m_sniIconPixmap = m_sniInter->iconPixmap();
    m_sniIconThemePath = m_sniInter->iconThemePath();
    m_sniMenuPath = m_sniInter->menu();
    m_sniOverlayIconName = m_sniInter->overlayIconName();
    m_sniOverlayIconPixmap = m_sniInter->overlayIconPixmap();
    m_sniStatus = m_sniInter->status();

    // 使用同步的方式获取id，否则在插入的时候无法获取正确的位置
    m_sniInter->setSync(true);
    m_sniId = m_sniInter->id();
    m_sniInter->setSync(false);

    m_updateIconTimer->start();
//    m_updateOverlayIconTimer->start();
//    m_updateAttentionIconTimer->start();
}

void SNITrayWidget::initMenu()
{
    const QString &sniMenuPath = m_sniMenuPath.path();
    if (sniMenuPath.isEmpty()) {
        qDebug() << "Error: current sni menu path is empty of dbus service:" << m_dbusService << "id:" << m_sniId;
        return;
    }

    qDebug() << "using sni service path:" << m_dbusService << "menu path:" << sniMenuPath;

    m_dbusMenuImporter = new DBusMenuImporter(m_dbusService, sniMenuPath, ASYNCHRONOUS, this);

    qDebug() << "generate the sni menu object";

    m_menu = m_dbusMenuImporter->menu();
    if (m_menu && !m_menu->parentWidget())
        m_menu->setParent(topLevelWidget(), Qt::Popup);

    qDebug() << "the sni menu obect is:" << m_menu;
}

void SNITrayWidget::refreshIcon()
{
    QPixmap pix = newIconPixmap(Icon);
    if (pix.isNull()) {
        return;
    }

    m_pixmap = pix;
    update();
    Q_EMIT iconChanged();

    if (!isVisible()) {
        Q_EMIT needAttention();
    }
}

void SNITrayWidget::refreshOverlayIcon()
{
    QPixmap pix = newIconPixmap(OverlayIcon);
    if (pix.isNull()) {
        return;
    }

    m_overlayPixmap = pix;
    update();
    Q_EMIT iconChanged();

    if (!isVisible()) {
        Q_EMIT needAttention();
    }
}

void SNITrayWidget::refreshAttentionIcon()
{
    /* TODO: A new approach may be needed to deal with attentionIcon */
    QPixmap pix = newIconPixmap(AttentionIcon);
    if (pix.isNull()) {
        return;
    }

    m_pixmap = pix;
    update();
    Q_EMIT iconChanged();

    if (!isVisible()) {
        Q_EMIT needAttention();
    }
}

void SNITrayWidget::showContextMenu(int x, int y)
{
    // 这里的PopupWindow属性是置顶的,如果不隐藏,会导致菜单显示不出来
    hidePopup();

    // ContextMenu does not work
    if (m_sniMenuPath.path().startsWith("/NO_DBUSMENU")) {
        m_sniInter->ContextMenu(x, y);
    } else {
        if (!m_menu) {
            qDebug() << "context menu has not be ready, init menu";
            initMenu();
        }

        if (m_menu)
            m_menu->popup(QPoint(x, y));
    }
}

void SNITrayWidget::onSNIAttentionIconNameChanged(const QString &value)
{
    m_sniAttentionIconName = value;

    m_updateAttentionIconTimer->start();
}

void SNITrayWidget::onSNIAttentionIconPixmapChanged(DBusImageList value)
{
    m_sniAttentionIconPixmap = value;

    m_updateAttentionIconTimer->start();
}

void SNITrayWidget::onSNIAttentionMovieNameChanged(const QString &value)
{
    m_sniAttentionMovieName = value;

    m_updateAttentionIconTimer->start();
}

void SNITrayWidget::onSNICategoryChanged(const QString &value)
{
    m_sniCategory = value;
}

void SNITrayWidget::onSNIIconNameChanged(const QString &value)
{
    m_sniIconName = value;

    m_updateIconTimer->start();
}

void SNITrayWidget::onSNIIconPixmapChanged(DBusImageList value)
{
    m_sniIconPixmap = value;

    m_updateIconTimer->start();
}

void SNITrayWidget::onSNIIconThemePathChanged(const QString &value)
{
    m_sniIconThemePath = value;

    m_updateIconTimer->start();
}

void SNITrayWidget::onSNIIdChanged(const QString &value)
{
    m_sniId = value;
}

void SNITrayWidget::onSNIMenuChanged(const QDBusObjectPath &value)
{
    m_sniMenuPath = value;
}

void SNITrayWidget::onSNIOverlayIconNameChanged(const QString &value)
{
    m_sniOverlayIconName = value;

    m_updateOverlayIconTimer->start();
}

void SNITrayWidget::onSNIOverlayIconPixmapChanged(DBusImageList value)
{
    m_sniOverlayIconPixmap = value;

    m_updateOverlayIconTimer->start();
}

void SNITrayWidget::onSNIStatusChanged(const QString &status)
{
    if (!ItemStatusList.contains(status) || m_sniStatus == status) {
        return;
    }

    m_sniStatus = status;

    Q_EMIT statusChanged(static_cast<SNITrayWidget::ItemStatus>(ItemStatusList.indexOf(status)));
}

void SNITrayWidget::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);
    if (m_pixmap.isNull())
        return;

    QPainter painter;
    painter.begin(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF &rf = QRect(rect());
    const QRectF &rfp = QRect(m_pixmap.rect());
    const QPointF &p = rf.center() - rfp.center() / m_pixmap.devicePixelRatioF();
    painter.drawPixmap(p, m_pixmap);

    if (!m_overlayPixmap.isNull()) {
        painter.drawPixmap(p, m_overlayPixmap);
    }

    painter.end();
}

QPixmap SNITrayWidget::newIconPixmap(IconType iconType)
{
    QPixmap pixmap;
    if (iconType == UnknownIconType) {
        return pixmap;
    }

    QString iconName;
    DBusImageList dbusImageList;

    QString iconThemePath = m_sniIconThemePath;

    switch (iconType) {
    case Icon:
        iconName = m_sniIconName;
        dbusImageList = m_sniIconPixmap;
        break;
    case OverlayIcon:
        iconName = m_sniOverlayIconName;
        dbusImageList = m_sniOverlayIconPixmap;
        break;
    case AttentionIcon:
        iconName = m_sniAttentionIconName;
        dbusImageList = m_sniAttentionIconPixmap;
        break;
    case AttentionMovieIcon:
        iconName = m_sniAttentionMovieName;
        break;
    default:
        break;
    }

    const auto ratio = devicePixelRatioF();
    const int iconSizeScaled = IconSize * ratio;
    do {
        // load icon from sni dbus
        if (!dbusImageList.isEmpty() && !dbusImageList.first().pixels.isEmpty()) {
            for (DBusImage dbusImage : dbusImageList) {
                char *image_data = dbusImage.pixels.data();

                if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
                    for (int i = 0; i < dbusImage.pixels.size(); i += 4) {
                        *(qint32 *)(image_data + i) = qFromBigEndian(*(qint32 *)(image_data + i));
                    }
                }

                QImage image((const uchar *)dbusImage.pixels.constData(), dbusImage.width, dbusImage.height, QImage::Format_ARGB32);
                pixmap = QPixmap::fromImage(image.scaled(iconSizeScaled, iconSizeScaled, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                pixmap.setDevicePixelRatio(ratio);
                if (!pixmap.isNull()) {
                    break;
                }
            }
        }

        // load icon from specified file
        if (!iconThemePath.isEmpty() && !iconName.isEmpty()) {
            QDirIterator it(iconThemePath, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                if (it.fileName().startsWith(iconName, Qt::CaseInsensitive)) {
                    QImage image(it.filePath());
                    pixmap = QPixmap::fromImage(image.scaled(iconSizeScaled, iconSizeScaled, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    pixmap.setDevicePixelRatio(ratio);
                    if (!pixmap.isNull()) {
                        break;
                    }
                }
            }
            if (!pixmap.isNull()) {
                break;
            }
        }

        // load icon from theme
        // Note: this will ensure return a None-Null pixmap
        // so, it should be the last fallback
        if (!iconName.isEmpty()) {
            // ThemeAppIcon::getIcon 会处理高分屏缩放问题
             ThemeAppIcon::getIcon(pixmap, iconName, IconSize);
            if (!pixmap.isNull()) {
                break;
            }
        }

        if (pixmap.isNull()) {
            qDebug() << "get icon faild!" << iconType;
        }
    } while (false);

    return pixmap;
}

void SNITrayWidget::enterEvent(QEvent *event)
{
    // 触屏不显示hover效果
    if (!qApp->property(IS_TOUCH_STATE).toBool()) {
        m_popupTipsDelayTimer->start();
    }

    AbstractTrayWidget::enterEvent(event);
}

void SNITrayWidget::leaveEvent(QEvent *event)
{
    m_popupTipsDelayTimer->stop();
    if (m_popupShown && !PopupWindow->model())
        hidePopup();

    update();

    AbstractTrayWidget::leaveEvent(event);
}

void SNITrayWidget::mousePressEvent(QMouseEvent *event)
{
    // call QWidget::mousePressEvent means to show dock-context-menu
    // when right button of mouse is pressed immediately in fashion mode

    // here we hide the right button press event when it is click in the special area
    m_popupTipsDelayTimer->stop();
    if (event->button() == Qt::RightButton && perfectIconRect().contains(event->pos(), true)) {
        event->accept();
        setMouseData(event);
        return;
    }

    QWidget::mousePressEvent(event);
}

void SNITrayWidget::mouseReleaseEvent(QMouseEvent *e)
{
    //e->accept();

    // 由于 XWindowTrayWidget 中对 发送鼠标事件到X窗口的函数, 如 sendClick/sendHoverEvent 中
    // 使用了 setX11PassMouseEvent, 而每次调用 setX11PassMouseEvent 时都会导致产生 mousePress 和 mouseRelease 事件
    // 因此如果直接在这里处理事件会导致一些问题, 所以使用 Timer 来延迟处理 100 毫秒内的最后一个事件
    setMouseData(e);

    QWidget::mouseReleaseEvent(e);
}

void SNITrayWidget::handleMouseRelease()
{
    Q_ASSERT(sender() == m_handleMouseReleaseTimer);

    // do not dealwith all mouse event of SystemTray, class SystemTrayItem will handle it
    if (trayType() == SystemTray)
        return;

    const QPoint point(m_lastMouseReleaseData.first - rect().center());
    if (point.manhattanLength() > 24)
        return;

    QPoint globalPos = QCursor::pos();
    uint8_t buttonIndex = XCB_BUTTON_INDEX_1;

    switch (m_lastMouseReleaseData.second) {
    case Qt:: MiddleButton:
        buttonIndex = XCB_BUTTON_INDEX_2;
        break;
    case Qt::RightButton:
        buttonIndex = XCB_BUTTON_INDEX_3;
        break;
    default:
        break;
    }

    sendClick(buttonIndex, globalPos.x(), globalPos.y());

    // left mouse button clicked
    if (buttonIndex == XCB_BUTTON_INDEX_1) {
        Q_EMIT clicked();
    }
}

void SNITrayWidget::showHoverTips()
{
    if (PopupWindow->model())
        return;

    // if not in geometry area
    const QRect r(topleftPoint(), size());
    if (!r.contains(QCursor::pos()))
        return;

    QProcess p;
    p.start("qdbus", {m_dbusService});
    if (!p.waitForFinished(1000)) {
        qDebug() << "sni dbus service error : " << m_dbusService;
        return;
    }

    QDBusInterface infc(m_dbusService, m_dbusPath);
    QDBusMessage msg = infc.call("Get", "org.kde.StatusNotifierItem", "ToolTip");
    if (msg.type() == QDBusMessage::ReplyMessage) {
        QDBusArgument arg = msg.arguments().at(0).value<QDBusVariant>().variant().value<QDBusArgument>();
        DBusToolTip tooltip = qdbus_cast<DBusToolTip>(arg);

        if (tooltip.title.isEmpty())
            return;

        // 当提示信息中有换行符时，需要使用setTextList
        if (tooltip.title.contains('\n'))
            m_tipsLabel->setTextList(tooltip.title.split('\n'));
        else
            m_tipsLabel->setText(tooltip.title);

        m_tipsLabel->setAccessibleName(itemKeyForConfig().replace("sni:",""));

        showPopupWindow(m_tipsLabel);
    }

}

void SNITrayWidget::hideNonModel()
{
    // auto hide if popup is not model window
    if (m_popupShown && !PopupWindow->model())
        hidePopup();
}

void SNITrayWidget::popupWindowAccept()
{
    if (!PopupWindow->isVisible())
        return;

    hidePopup();
}

void SNITrayWidget::hidePopup()
{
    m_popupTipsDelayTimer->stop();
    m_popupShown = false;
    PopupWindow->hide();

    emit PopupWindow->accept();
    emit requestWindowAutoHide(true);
}
// 获取在最外层的窗口(MainWindow)中的位置
const QPoint SNITrayWidget::topleftPoint() const
{
    QPoint p;
    const QWidget *w = this;
    do {
        p += w->pos();
        w = qobject_cast<QWidget *>(w->parent());
    } while (w);

    return p;
}

const QPoint SNITrayWidget::popupMarkPoint() const
{
    QPoint p(topleftPoint());

    const QRect r = rect();
    const QRect wr = window()->rect();

    switch (DockPosition) {
    case Dock::Position::Top:
        p += QPoint(r.width() / 2, r.height() + (wr.height() - r.height()) / 2);
        break;
    case Dock::Position::Bottom:
      p += QPoint(r.width() / 2, 0 - (wr.height() - r.height()) / 2);
        break;
    case Dock::Position::Left:
        p += QPoint(r.width() + (wr.width() - r.width()) / 2, r.height() / 2);
        break;
    case Dock::Position::Right:
        p += QPoint(0 - (wr.width() - r.width()) / 2, r.height() / 2);
        break;
    }

    return p;
}

void SNITrayWidget::showPopupWindow(QWidget *const content, const bool model)
{
    m_popupShown = true;

    if (model)
        emit requestWindowAutoHide(false);

    DockPopupWindow *popup = PopupWindow.data();
    QWidget *lastContent = popup->getContent();
    if (lastContent)
        lastContent->setVisible(false);

    switch (DockPosition) {
    case Dock::Position::Top:   popup->setArrowDirection(DockPopupWindow::ArrowTop);     break;
    case Dock::Position::Bottom: popup->setArrowDirection(DockPopupWindow::ArrowBottom);  break;
    case Dock::Position::Left:  popup->setArrowDirection(DockPopupWindow::ArrowLeft);    break;
    case Dock::Position::Right: popup->setArrowDirection(DockPopupWindow::ArrowRight);   break;
    }
    popup->resize(content->sizeHint());
    popup->setContent(content);

    QPoint p = popupMarkPoint();
    if (!popup->isVisible())
        QMetaObject::invokeMethod(popup, "show", Qt::QueuedConnection, Q_ARG(QPoint, p), Q_ARG(bool, model));
    else
        popup->show(p, model);
}

void SNITrayWidget::setMouseData(QMouseEvent *e)
{
    m_lastMouseReleaseData.first = e->pos();
    m_lastMouseReleaseData.second = e->button();

    m_handleMouseReleaseTimer->start();
}
