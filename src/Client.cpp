/**************************************************************************
 *  This file is part of QuteScoop. See README for license
 **************************************************************************/

#include "Client.h"

#include "Whazzup.h"
#include "Settings.h"


Client::Client(const QJsonObject& json, const WhazzupData*) {
    label = json["callsign"].toString();
    userId = QString::number(json["cid"].toInt());
    realName = json["name"].toString();
    lat = json["latitude"].toDouble();
    lon = json["longitude"].toDouble();
    server = json["server"].toString();
    rating = json["rating"].toInt();
    timeConnected = QDateTime::fromString(json["logon_time"].toString(), Qt::ISODate);

    if(realName.contains(QRegExp("\\b[A-Z]{4}$"))) {
        homeBase = realName.right(4);
        realName = realName.left(realName.length() - 4).trimmed();
    }
}

QString Client::onlineTime() const {
    if (!timeConnected.isValid())
        return QString("not connected");
    return QDateTime::fromTime_t( // this will get wrapped by 24h but that should not be a problem...
            Whazzup::instance()->whazzupData().whazzupTime.toTime_t()
            - timeConnected.toTime_t()
            ).toUTC().toString("HH:mm");
}

QString Client::displayName(bool withLink) const {
    QString result = realName;
    if(!rank().isEmpty())
        result += " (" + rank() + ")";

    if(withLink && !userId.isEmpty()) {
        QString link = Whazzup::instance()->userUrl(userId);
        if(link.isEmpty())
            return result;
        result = QString("<a href='%1'>%2</a>").arg(link, result);
    }

    return result;
}

QString Client::clientInformation() const {
    return QString();
}

QString Client::detailInformation() const {
    if(!homeBase.isEmpty()) {
        return "(" + homeBase + ")";
    }
    return QString();
}

bool Client::matches(const QRegExp& regex) const {
    if(realName.contains(regex)) return true;
    if(userId.contains(regex)) return true;
    return MapObject::matches(regex);
}

QString Client::toolTip() const {
    QString result = label + " (" + realName;
    if(!rank().isEmpty()) {
        result += ", " + rank();
    }
    result += ")";
    return result;
}

bool Client::isFriend() const {
    return Settings::friends().contains(userId);
}
