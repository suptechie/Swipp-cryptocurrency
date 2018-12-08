// Copyright (c) 2017-2018 The Swipp developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "notificator.h"

#include <QMetaType>
#include <QVariant>
#include <QIcon>
#include <QApplication>
#include <QStyle>
#include <QByteArray>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QTemporaryFile>
#include <QImageWriter>

#ifdef USE_DBUS
#include <QtDBus>
#include <stdint.h>
#endif

#ifdef Q_OS_MAC
#include "macnotificationhandler.h"
#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef USE_DBUS
const int FREEDESKTOP_NOTIFICATION_ICON_SIZE = 128;
#endif

Notificator::Notificator(const QString &programName, QSystemTrayIcon *trayicon, QWidget *parent):
    QObject(parent), parent(parent), programName(programName), mode(None), trayIcon(trayicon)
#ifdef USE_DBUS
    ,interface(0)
#endif
{
    if (trayicon && trayicon->supportsMessages())
        mode = QSystemTray;

#ifdef USE_DBUS
    interface = new QDBusInterface("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                   "org.freedesktop.Notifications");
    if (interface->isValid())
        mode = Freedesktop;
#endif

#ifdef Q_OS_MAC
    // Check if Growl is installed (based on QT's tray icon implementation)
    CFURLRef cfurl;
    OSStatus status = LSGetApplicationForInfo(kLSUnknownType, kLSUnknownCreator, CFSTR("growlTicket"),
                                              kLSRolesAll, 0, &cfurl);

    if (status != kLSApplicationNotFoundErr)
    {
        CFBundleRef bundle = CFBundleCreate(0, cfurl);

        if (CFStringCompare(CFBundleGetIdentifier(bundle), CFSTR("com.Growl.GrowlHelperApp"),
            kCFCompareCaseInsensitive | kCFCompareBackwards) == kCFCompareEqualTo)
        {
            if (CFStringHasSuffix(CFURLGetString(cfurl), CFSTR("/Growl.app/")))
                mode = Growl13;
            else
                mode = Growl12;
        }

        CFRelease(cfurl);
        CFRelease(bundle);
    }
#endif
}

Notificator::~Notificator()
{
#ifdef USE_DBUS
    delete interface;
#endif
}


#ifdef USE_DBUS
// Loosely based on http://www.qtcentre.org/archive/index.php/t-25879.html
class FreedesktopImage
{
public:
    FreedesktopImage() {}
    FreedesktopImage(const QImage &img);

    static int metaType();
    static QVariant toVariant(const QImage &img);

private:
    int width, height, stride;
    bool hasAlpha;
    int channels;
    int bitsPerSample;
    QByteArray image;

    friend QDBusArgument &operator<<(QDBusArgument &a, const FreedesktopImage &i);
    friend const QDBusArgument &operator>>(const QDBusArgument &a, FreedesktopImage &i);
};

Q_DECLARE_METATYPE(FreedesktopImage);

// Image configuration settings
const int CHANNELS = 4;
const int BYTES_PER_PIXEL = 4;
const int BITS_PER_SAMPLE = 8;

FreedesktopImage::FreedesktopImage(const QImage &img) : width(img.width()), height(img.height()),
    stride(img.width() * BYTES_PER_PIXEL), hasAlpha(true), channels(CHANNELS), bitsPerSample(BITS_PER_SAMPLE)
{
    // Convert 00xAARRGGBB to RGBA bytewise (endian-independent) format
    QImage tmp = img.convertToFormat(QImage::Format_ARGB32);
    const uint32_t *data = reinterpret_cast<const uint32_t*>(tmp.bits());
    unsigned int num_pixels = width * height;

    image.resize(num_pixels * BYTES_PER_PIXEL);

    for (unsigned int ptr = 0; ptr < num_pixels; ++ptr)
    {
        image[ptr*BYTES_PER_PIXEL + 0] = data[ptr] >> 16; // R
        image[ptr*BYTES_PER_PIXEL + 1] = data[ptr] >> 8;  // G
        image[ptr*BYTES_PER_PIXEL + 2] = data[ptr];       // B
        image[ptr*BYTES_PER_PIXEL + 3] = data[ptr] >> 24; // A
    }
}

QDBusArgument &operator<<(QDBusArgument &a, const FreedesktopImage &i)
{
    a.beginStructure();
    a << i.width << i.height << i.stride << i.hasAlpha << i.bitsPerSample << i.channels << i.image;
    a.endStructure();

    return a;
}

const QDBusArgument &operator>>(const QDBusArgument &a, FreedesktopImage &i)
{
    a.beginStructure();
    a >> i.width >> i.height >> i.stride >> i.hasAlpha >> i.bitsPerSample >> i.channels >> i.image;
    a.endStructure();

    return a;
}

int FreedesktopImage::metaType()
{
    return qDBusRegisterMetaType<FreedesktopImage>();
}

QVariant FreedesktopImage::toVariant(const QImage &img)
{
    FreedesktopImage fimg(img);
    return QVariant(FreedesktopImage::metaType(), &fimg);
}

void Notificator::notifyDBus(Class cls, const QString &title, const QString &text, const QIcon &icon, int millisTimeout)
{
    Q_UNUSED(cls);
    QList<QVariant> args;

    args.append(programName);
    args.append(0U);
    args.append(QString());
    args.append(title);
    args.append(text);

    QStringList actions;
    args.append(actions);

    QVariantMap hints;
    QIcon tmpicon;

    if(icon.isNull())
    {
        QStyle::StandardPixmap sicon = QStyle::SP_MessageBoxQuestion;

        switch (cls)
        {
            case Information:
                sicon = QStyle::SP_MessageBoxInformation;
                break;
            case Warning:
                sicon = QStyle::SP_MessageBoxWarning;
                break;
            case Critical:
                sicon = QStyle::SP_MessageBoxCritical;
                break;
            default:
                break;
        }

        tmpicon = QApplication::style()->standardIcon(sicon);
    }
    else
        tmpicon = icon;

    hints["icon_data"] = FreedesktopImage::toVariant(tmpicon.pixmap(FREEDESKTOP_NOTIFICATION_ICON_SIZE).toImage());
    args.append(hints);
    args.append(millisTimeout); // Timeout (in msec)

    interface->callWithArgumentList(QDBus::NoBlock, "Notify", args);
}
#endif

void Notificator::notifySystray(Class cls, const QString &title, const QString &text, const QIcon &icon, int millisTimeout)
{
    Q_UNUSED(icon);
    QSystemTrayIcon::MessageIcon sicon = QSystemTrayIcon::NoIcon;

    switch (cls)
    {
        case Information:
            sicon = QSystemTrayIcon::Information;
            break;
        case Warning:
            sicon = QSystemTrayIcon::Warning;
            break;
        case Critical:
            sicon = QSystemTrayIcon::Critical;
            break;
    }

    trayIcon->showMessage(title, text, sicon, millisTimeout);
}

#ifdef Q_OS_MAC
void Notificator::notifyGrowl(Class cls, const QString &title, const QString &text, const QIcon &icon)
{
    const QString script("tell application \"%5\"\n"
                         "  set the allNotificationsList to {\"Notification\"}\n"
                         "  set the enabledNotificationsList to {\"Notification\"}\n"
                         "  register as application \"%1\" all notifications allNotificationsList default notifications enabledNotificationsList\n"
                         "  notify with name \"Notification\" title \"%2\" description \"%3\" application name \"%1\"%4\n"
                         "end tell");

    QString notificationApp(QApplication::applicationName());
    QPixmap notificationIconPixmap;

    if (notificationApp.isEmpty())
        notificationApp = "Application";

    if (icon.isNull()) {
        QStyle::StandardPixmap sicon = QStyle::SP_MessageBoxQuestion;

        switch (cls)
        {
            case Information:
                sicon = QStyle::SP_MessageBoxInformation;
                break;
            case Warning:
                sicon = QStyle::SP_MessageBoxWarning;
                break;
            case Critical:
                sicon = QStyle::SP_MessageBoxCritical;
                break;
        }

        notificationIconPixmap = QApplication::style()->standardPixmap(sicon);
    }
    else
    {
        QSize size = icon.actualSize(QSize(48, 48));
        notificationIconPixmap = icon.pixmap(size);
    }

    QString notificationIcon;
    QTemporaryFile notificationIconFile;

    if (!notificationIconPixmap.isNull() && notificationIconFile.open())
    {
        QImageWriter writer(&notificationIconFile, "PNG");

        if (writer.write(notificationIconPixmap.toImage()))
            notificationIcon = QString(" image from location \"file://%1\"").arg(notificationIconFile.fileName());
    }

    QString quotedTitle(title), quotedText(text);
    quotedTitle.replace("\\", "\\\\").replace("\"", "\\");
    quotedText.replace("\\", "\\\\").replace("\"", "\\");

    QString growlApp(this->mode == Notificator::Growl13 ? "Growl" : "GrowlHelperApp");
    MacNotificationHandler::instance()->sendAppleScript(script.arg(notificationApp, quotedTitle, quotedText, notificationIcon, growlApp));
}

void Notificator::notifyMacUserNotificationCenter(Class cls, const QString &title, const QString &text, const QIcon &icon) {
    MacNotificationHandler::instance()->showNotification(title, text);
}
#endif

void Notificator::notify(Class cls, const QString &title, const QString &text, const QIcon &icon, int millisTimeout)
{
    switch(mode)
    {
#ifdef USE_DBUS
        case Freedesktop:
            notifyDBus(cls, title, text, icon, millisTimeout);
            break;
#endif
        case QSystemTray:
            notifySystray(cls, title, text, icon, millisTimeout);
            break;
#ifdef Q_OS_MAC
        case Growl12:
        case Growl13:
            notifyGrowl(cls, title, text, icon);
            break;
#endif
        default:
            if(cls == Critical)
                QMessageBox::critical(parent, title, text, QMessageBox::Ok, QMessageBox::Ok);
            break;
    }
}
