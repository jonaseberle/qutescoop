/**************************************************************************
 *  This file is part of QuteScoop. See README for license
 **************************************************************************/

#include "Controller.h"

#include "Client.h"
#include "ControllerDetails.h"
#include "NavData.h"

#include <QJsonObject>

Controller::Controller(const QJsonObject& json, const WhazzupData* whazzup):
    Client(json, whazzup),
    sector(0)
{
    frequency = json["frequency"].toString();
    facilityType = json["facility"].toInt();
    if(label.right(4) == "_FSS") facilityType = 7; // workaround as VATSIM reports 1 for _FSS

    visualRange = json["visual_range"].toInt();

    atisMessage = "";
    if (json.contains("text_atis") && json["text_atis"].isArray()) {
        QJsonArray atis = json["text_atis"].toArray();
        for (int i = 0; i < atis.size(); ++i) {
            atisMessage += atis[i].toString() + "\n";
        }
    }

    atisCode = "";
    if(json.contains("atis_code")) {
        atisCode = json["atis_code"].toString();
    }

    // do some magic for Controller Info like "online until"...
    QRegExp rxOnlineUntil = QRegExp(
          "(open|close|online|offline|till|until)(\\W*\\w*\\W*){0,4}\\b(\\d{1,2}):?(\\d{2})\\W?(z|utc)?",
          Qt::CaseInsensitive
    );
    if (rxOnlineUntil.indexIn(atisMessage) > 0) {
        //fixme
        QTime found = QTime::fromString(rxOnlineUntil.cap(3)+rxOnlineUntil.cap(4), "HHmm");
        if(found.isValid()) {
            if (qAbs(found.secsTo(whazzup->whazzupTime.time())) > 60*60 * 12) {
                // e.g. now its 2200z, and he says "online until 0030z", allow for up to 12 hours
                assumeOnlineUntil = QDateTime(whazzup->whazzupTime.date().addDays(1), found, Qt::UTC);
            } else {
                assumeOnlineUntil = QDateTime(whazzup->whazzupTime.date(), found, Qt::UTC);
            }
        }
    }

    QString icao = this->controllerSectorName();
    // Look for a sector name matching any part of the login down to 4 characters
    if (!icao.isEmpty()) {
        do {
            this->sector = NavData::instance()->sectors.value(icao, 0);
            if (sector != 0) {
                // We determine lat/lon from the sector
                QPair<double, double> center = this->sector->getCenter();
                this->lat = center.first;
                this->lon = center.second;

                break;
            }
            icao.chop(1);
        } while (icao.length() >= 2);

        if (sector == 0) {
            qDebug() << "Unknown sector/FIR" << icao << "Please provide sector information if you can";
        }
    }
}

QString Controller::facilityString() const {
    switch (facilityType) {
        case 0: return "OBS";
        case 1: return "Staff";
        case 2: return "DEL";
        case 3: return "GND";
        case 4: return "TWR";
        case 5: return "APP";
        case 6: return "CTR";
        case 7: return "FSS";
    }
    return QString();
}

QStringList Controller::atcLabelTokens() const {
  if(!isATC())
      return QStringList();

  return label.split('_', Qt::SkipEmptyParts);
}

QString Controller::controllerSectorName() const{
    auto _atcLabelTokens = atcLabelTokens();

    if(
       !_atcLabelTokens.empty()
       && (_atcLabelTokens.last().startsWith("CTR") || _atcLabelTokens.last().startsWith("FSS"))
    ) {
        _atcLabelTokens.removeLast();
        return _atcLabelTokens.join("_");
    }
    return QString();
}

bool Controller::isCtrFss() const
{
  return label.endsWith("_CTR") || label.endsWith("FSS");
}

bool Controller::isAppDep() const
{
  return label.endsWith("_APP") || label.endsWith("DEP");
}

bool Controller::isTwr() const
{
  return label.endsWith("_TWR");
}

bool Controller::isGnd() const
{
  return label.endsWith("_GND");
}

bool Controller::isDel() const
{
  return label.endsWith("_DEL");
}

bool Controller::isAtis() const
{
  return label.endsWith("_ATIS");
}

Airport *Controller::airport() const {
    auto _atcLabelTokens = atcLabelTokens();

    if (!_atcLabelTokens.empty()) {
        QString tryAirport = specialAirportWorkarounds(_atcLabelTokens.first());
        if (NavData::instance()->airports.contains(tryAirport))
            return NavData::instance()->airports[tryAirport];
    }

    return 0;
}

void Controller::showDetailsDialog() {
    ControllerDetails *infoDialog = ControllerDetails::instance();
    infoDialog->refresh(this);
    infoDialog->show();
    infoDialog->raise();
    infoDialog->activateWindow();
    infoDialog->setFocus();
}

QString Controller::rank() const {
    switch(rating) {
        case 0: return QString();
        case 1: return QString("OBS");
        case 2: return QString("S1");
        case 3: return QString("S2");
        case 4: return QString("S3");
        case 5: return QString("C1");
        case 6: return QString("C2");
        case 7: return QString("C3");
        case 8: return QString("I1");
        case 9: return QString("I2");
        case 10: return QString("I3");
        case 11: return QString("SUP");
        case 12: return QString("ADM");
        default: return QString("unknown:%1").arg(rating);
    }
}

QString Controller::toolTip() const { // LOVV_CTR [Vienna] (134.350, Alias | Name, C1)
    QString result = label;
    if (sector != 0)
        result += " [" + sector->name + "]";
    result += " (";
    if(!isObserver() && !frequency.isEmpty())
        result += frequency + ", ";
    result += realName();
    if(!rank().isEmpty())
        result += ", " + rank();
    result += ")";
    return result;
}

QString Controller::toolTipShort() const // LOVV_CTR [Vienna]
{
  QString result = label;
  if (sector != 0)
      result += " [" + sector->name + "]";
  return result;
}

QString Controller::mapLabel() const { // LOVV
    if(sector != 0) {
        return controllerSectorName();
    }

    return label;
}

bool Controller::matches(const QRegExp& regex) const {
    if (frequency.contains(regex)) return true;
    if (atisMessage.contains(regex)) return true;
    if(realName().contains(regex)) return true;
    if (sector != 0)
        if (sector->name.contains(regex)) return true;
    return MapObject::matches(regex);
}

bool Controller::isObserver() const {
  return facilityType == 0;
}

bool Controller::isATC() const {
  // 199.998 gets transmitted on VATSIM for a controller without prim freq
  return facilityType > 0 && frequency != "199.998";
}

QString Controller::specialAirportWorkarounds(const QString& rawAirport) const {
  // map special callsigns to airports. Still not perfect, because only 1 airport gets matched this way...
  if(rawAirport == "NY")
      return "KLGA"; // map NY -> KLGA
  else if(rawAirport == "MSK")
      return "UUWW"; // map MSK -> UUWW

  // VATSIMmers don't think ICAO codes are cool
  if(rawAirport.length() == 3)
      return "K" + rawAirport;

  return rawAirport;
}
