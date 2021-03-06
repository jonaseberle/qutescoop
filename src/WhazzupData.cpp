/**************************************************************************
 *  This file is part of QuteScoop. See README for license
 **************************************************************************/

#include "WhazzupData.h"

#include "Sector.h"
#include "Pilot.h"
#include "Controller.h"
#include "BookedController.h"
#include "NavData.h"
#include "Settings.h"
#include "helpers.h"

#include <QJsonDocument>

WhazzupData::WhazzupData():
        servers(QList<QStringList>()), voiceServers(QList<QStringList>()),
        updateEarliest(QDateTime()), whazzupTime(QDateTime()), bookingsTime(QDateTime()),
        _whazzupVersion(0), _dataType(UNIFIED)  {
}

WhazzupData::WhazzupData(QByteArray* bytes, WhazzupType type):
        servers(QList<QStringList>()), voiceServers(QList<QStringList>()),
        updateEarliest(QDateTime()), whazzupTime(QDateTime()),
        bookingsTime(QDateTime()), _whazzupVersion(0) {
    qDebug() << "WhazzupData::WhazzupData(buffer)" << type << "[NONE, WHAZZUP, ATCBOOKINGS, UNIFIED]";
    qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
    _dataType = type;
    QStringList friends = Settings::friends();
    int reloadInMin = Settings::downloadInterval();
    QJsonDocument data = QJsonDocument::fromJson(*bytes);
    if(!data.isNull() && type == WHAZZUP) {
        QJsonObject json = data.object();
        if(json.contains("general") && json["general"].isObject()) {
            QJsonObject generalObject = json["general"].toObject();
            if(generalObject.contains("update_timestamp") && generalObject["update_timestamp"].isString()) {
                whazzupTime = QDateTime::fromString(generalObject["update_timestamp"].toString(), Qt::ISODate);
            }
        }
        if(!whazzupTime.isValid()) {
            // Assume it's the current time
            whazzupTime = QDateTime::currentDateTime();
        }
        if(json.contains("servers") && json["servers"].isArray()) {
            QJsonArray serversArray = json["servers"].toArray();
            for(int i = 0; i < serversArray.size(); ++i)  {
                QJsonObject serverObject = serversArray[i].toObject();
                if(serverObject.contains("ident") && serverObject["ident"].isString()
                && serverObject.contains("hostname_or_ip") && serverObject["hostname_or_ip"].isString()
                && serverObject.contains("location") && serverObject["location"].isString()
                && serverObject.contains("name") && serverObject["name"].isString()
                && serverObject.contains("clients_connection_allowed") && serverObject["clients_connection_allowed"].isDouble()) {
                    QStringList server;
                    server.append(serverObject["ident"].toString());
                    server.append(serverObject["hostname_or_ip"].toString());
                    server.append(serverObject["location"].toString());
                    server.append(serverObject["name"].toString());
                    server.append(serverObject["clients_connection_allowed"].toString());
                    servers += server;
                }
            }
        }
        if(json.contains("pilots") && json["pilots"].isArray()) {
            QJsonArray pilotsArray = json["pilots"].toArray();
            for(int i = 0; i < pilotsArray.size(); ++i) {
                QJsonObject pilotObject = pilotsArray[i].toObject();
                Pilot *p = new Pilot(pilotObject, this);
                pilots[p->label] = p;
                if(friends.contains(p->userId))
                    friendsLatLon.append(QPair<double, double>(p->lat, p->lon));
            }
        }
        
        if(json.contains("controllers") && json["controllers"].isArray()) {
            QJsonArray controllersArray = json["controllers"].toArray();
            for(int i = 0; i < controllersArray.size(); ++i) {
                QJsonObject controllerObject = controllersArray[i].toObject();
                Controller *c = new Controller(controllerObject, this);
                controllers[c->label] = c;
                if(friends.contains(c->userId))
                    friendsLatLon.append(QPair<double, double>(c->lat, c->lon));
            }
        }

        if(json.contains("prefiles") && json["prefiles"].isArray()) {
            QJsonArray prefilesArray = json["prefiles"].toArray();
            for(int i = 0; i < prefilesArray.size(); ++i) {
                QJsonObject prefileObject = prefilesArray[i].toObject();
                Pilot *p = new Pilot(prefileObject, this);
                bookedPilots[p->label] = p;
            }
        }
    } else if(type == ATCBOOKINGS) {
        // ATC Bookings are still in the old format, however we only need the CLIENTS and GENERAL sections
        enum ParserState {STATE_NONE, STATE_GENERAL, STATE_CLIENTS};
        ParserState state = STATE_NONE;
        foreach(QString line, bytes->split('\n')) {
            line = line.trimmed();
            if(line.isEmpty())
                continue;
            
            if(line.startsWith(';'))
                continue;
            
            if(line.startsWith("!")) {
                if(line.startsWith("!CLIENTS"))
                    state = STATE_CLIENTS;
                else if(line.startsWith("!GENERAL"))
                    state = STATE_GENERAL;
                else
                    state = STATE_NONE;
                
                continue;
            }
            
            switch(state) {
                case STATE_CLIENTS: {
                    QStringList list = line.split(':');

                    if(list.size() < 38)
                        continue;

                    if(list[3] != "ATC")
                        continue;
                    // Create a JSON Object containing the required fields
                    QJsonObject controllerObject;
                    controllerObject["callsign"] = list[0];
                    controllerObject["cid"] = list[1].toInt();
                    controllerObject["name"] = list[2];

                    // Bookings only:
                    controllerObject["bookingType"] = list[4].toInt();
                    controllerObject["timeTo"] = list[14];
                    controllerObject["date"] = list[16];
                    controllerObject["link"] = list[35];
                    controllerObject["timeFrom"] = list[37];

                    BookedController *bc = new BookedController(controllerObject, this);
                    bookedControllers.append(bc);
                }
                    break;
                case STATE_GENERAL: {
                    QStringList list = line.split("=");
                    if(list.size() != 2)
                        continue;
                    if(line.startsWith("UPDATE")) {
                        bookingsTime = QDateTime::fromString(list[1].trimmed(), "yyyyMMddHHmmss");
                        bookingsTime.setTimeSpec(Qt::UTC);
                    }
                }
                    break;
                case STATE_NONE:
                    break;
            }
        }
    } else {
        // Try again in 15 seconds
        updateEarliest = QDateTime::currentDateTime().addSecs(15);
    }
    // set the earliest time the server will have new data
    if (whazzupTime.isValid() && reloadInMin > 0)
        updateEarliest = whazzupTime.addSecs(reloadInMin * 60).toUTC();
    qApp->restoreOverrideCursor();
    qDebug() << "WhazzupData::WhazzupData(buffer) -- finished";
}

//faking WhazzupData based on valid data and a predictTime
WhazzupData::WhazzupData(const QDateTime predictTime, const WhazzupData &data):
        servers(QList<QStringList>()), voiceServers(QList<QStringList>()),
        updateEarliest(QDateTime()), whazzupTime(QDateTime()),
        bookingsTime(QDateTime()), predictionBasedOnTime(QDateTime()),
        predictionBasedOnBookingsTime(QDateTime()), _whazzupVersion(0) {
    qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
    qDebug() << "WhazzupData::WhazzupData(predictTime)" << predictTime;

    _whazzupVersion = data._whazzupVersion;
    whazzupTime = predictTime;
    predictionBasedOnTime = QDateTime(data.whazzupTime);
    predictionBasedOnBookingsTime = QDateTime(data.bookingsTime);

    _dataType = data._dataType;
    // so now lets fake some controllers
    foreach(const BookedController* bc, data.bookedControllers) {
        //if (bc == 0) continue;
        if (bc->starts() <= predictTime && bc->ends() >= predictTime) { // only ones booked for the selected time
            QJsonObject controllerObject;

            controllerObject["callsign"] = bc->label;
            controllerObject["name"] = bc->realName;
            controllerObject["facility"] = bc->facilityType;

            //atisMessage = getField(stringList, 35);
            QJsonArray atisLines;
            atisLines.append(QString("BOOKED from %1, online until %2").arg(bc->starts().toString("HHmm'z'")
                                                                .arg(bc->ends().toString("HHmm'z'"))));
            atisLines.append(bc->bookingInfoStr);
            controllerObject["text_atis"] = atisLines;

            //timeConnected = QDateTime::fromString(getField(stringList, 37), "yyyyMMddHHmmss");
            controllerObject["logon_time"] = bc->timeConnected.toString(Qt::ISODate);

            //server = getField(stringList, 14);
            controllerObject["server"] = "BOOKED SESSION";

            //visualRange = getField(stringList, 19).toInt();
            controllerObject["visual_range"] = bc->visualRange;

            // not applicable:
            //frequency = getField(stringList, 4);
            //visualRange = getField(stringList, 19).toInt();
            //timeLastAtisReceived = QDateTime::fromString(getField(stringList, 36), "yyyyMMddHHmmss");
            //protrevision = getField(stringList, 15).toInt();
            //rating = getField(stringList, 16).toInt();

            controllers[bc->label] = new Controller(controllerObject, this);
        }
    }

    // let controllers be in until he states in his Controller Info also if only found in Whazzup, not booked
    foreach(const Controller *c, data.controllers) {
        QDateTime showUntil = predictionBasedOnTime.addSecs(Settings::downloadInterval() * 4 * 60); // standard for online controllers: 4 min
        if(c->assumeOnlineUntil.isValid())
            if(predictionBasedOnTime.secsTo(c->assumeOnlineUntil) >= 0) // use only if we catched him before his stated leave-time.
                showUntil = c->assumeOnlineUntil;

        if (predictTime <= showUntil && predictTime >= predictionBasedOnTime)
            controllers[c->label] = new Controller(*c);
    }

    QDateTime startTime, endTime = QDateTime();
    double startLat, startLon, endLat, endLon = 0.;
    int altitude = 0;

    foreach(const Pilot *p, data.allPilots()) {
        //if (p == 0) continue;
        if (!p->eta().isValid())
            continue; // no ETA, no prediction...
        if (!p->etd().isValid() && predictTime < predictionBasedOnTime)
            continue; // no ETD, difficult prediction. Before the whazzupTime, no prediction...
        if(p->destAirport() == 0)
            continue; // sorry, no magic available yet. Just let him fly the last heading until etaPlan()? Does not make sense
        if(p->etd() > predictTime || p->eta() < predictTime) {
            if (p->flightStatus() == Pilot::PREFILED && p->etd() > predictTime) { // we want prefiled before their
                //departure as in non-Warped view
                Pilot* np = new Pilot(*p);
                np->whazzupTime = QDateTime(predictTime);
                bookedPilots[np->label] = np; // just copy him over
                continue;
            }
            continue; // not on the map on the selected time
        }
        if (p->flightStatus() == Pilot::PREFILED) {
            // if we dont know where a prefiled comes from, no magic available
            if(p->depAirport() == 0 || p->destAirport() == 0)
                continue;

            startTime = p->etd();
            startLat = p->depAirport()->lat;
            startLon = p->depAirport()->lon;

            endTime = p->eta();
            endLat = p->destAirport()->lat;
            endLon = p->destAirport()->lon;
        } else {
            startTime = data.whazzupTime;
            startLat = p->lat;
            startLon = p->lon;

            endTime = p->eta();
            endLat = p->destAirport()->lat;
            endLon = p->destAirport()->lon;
        }
        // altitude
        if(p->planAlt.toInt() != 0)
            altitude = p->defuckPlanAlt(p->planAlt);

        // position
        double fraction = (double) startTime.secsTo(predictTime)
                          / startTime.secsTo(endTime);
        QPair<double, double> pos = NavData::greatCircleFraction(
                                        startLat, startLon,
                                        endLat, endLon, fraction);

        double dist = NavData::distance(startLat, startLon, endLat, endLon);
        double enrouteHrs = ((double) startTime.secsTo(endTime)) / 3600.0;
        if (qFuzzyIsNull(enrouteHrs)) enrouteHrs = 0.1;
        double groundspeed = dist / enrouteHrs;
        double trueHeading = NavData::courseTo(pos.first, pos.second, endLat, endLon);

        // create Pilot instance and assign values
        Pilot* np = new Pilot(*p);
        np->whazzupTime = QDateTime(predictTime);
        np->lat = pos.first;
        np->lon = pos.second;
        np->altitude = altitude;
        np->trueHeading = trueHeading;
        np->groundspeed = (int) groundspeed;

        pilots[np->label] = np;
    }
    qApp->restoreOverrideCursor();
    qDebug() << "WhazzupData::WhazzupData(predictTime) -- finished";
}

WhazzupData::WhazzupData(const WhazzupData &data) {
    assignFrom(data);
}

WhazzupData::~WhazzupData() {
    foreach(const QString s, pilots.keys())
        delete pilots[s];
    pilots.clear();

    foreach(const QString s, bookedPilots.keys())
        delete bookedPilots[s];
    bookedPilots.clear();

    foreach(const QString s, controllers.keys())
        delete controllers[s];
    controllers.clear();

    foreach(const BookedController *bc, bookedControllers)
        delete bc;
    bookedControllers.clear();
}

WhazzupData &WhazzupData::operator=(const WhazzupData &data) {
    assignFrom(data);
    return *this;
}

void WhazzupData::assignFrom(const WhazzupData &data) {
    qDebug() << "WhazzupData::assignFrom()";
    if(this == &data)
        return;

    if (data._dataType == WHAZZUP || data._dataType == UNIFIED) {
        if(_dataType == ATCBOOKINGS) _dataType = UNIFIED;
        servers = data.servers;
        voiceServers = data.voiceServers;
        whazzupTime = data.whazzupTime;
        predictionBasedOnTime = data.predictionBasedOnTime;
        updateEarliest = data.updateEarliest;

        pilots.clear();
        foreach(const QString s, data.pilots.keys())
            pilots[s] = new Pilot(*data.pilots[s]);

        bookedPilots.clear();
        foreach(const QString s, data.bookedPilots.keys())
            bookedPilots[s] = new Pilot(*data.bookedPilots[s]);

        controllers.clear();
        foreach(const QString s, data.controllers.keys())
            controllers[s] = new Controller(*data.controllers[s]);
    }
    if (data._dataType == ATCBOOKINGS || data._dataType == UNIFIED) {
        if(_dataType == WHAZZUP) _dataType = UNIFIED;

        bookedControllers.clear();
        foreach(const BookedController *bc, data.bookedControllers)
            bookedControllers.append(new BookedController(*bc));

        bookingsTime = QDateTime(data.bookingsTime);
        predictionBasedOnBookingsTime = QDateTime(data.predictionBasedOnBookingsTime);
    }
    qDebug() << "WhazzupData::assignFrom() -- finished";
}

void WhazzupData::updatePilotsFrom(const WhazzupData &data) {
    qDebug() << "WhazzupData::updatePilotsFrom()";
    foreach(const QString s, pilots.keys()) { // remove pilots that are no longer there
        if(!data.pilots.contains(s)) {
            foreach(const Pilot *p, pilots.values(s)) // there might be several...
                delete p;
            pilots.remove(s);
        }
    }
    foreach(const QString s, data.pilots.keys()) {
        if(!pilots.contains(s)) { // new pilots
            // create a new copy of new pilot
            Pilot *p = new Pilot(*data.pilots[s]);
            pilots[s] = p;
        } else { // existing pilots: data saved in the object needs to be transferred
            data.pilots[s]->showDepDestLine              = pilots[s]->showDepDestLine;
            data.pilots[s]->routeWaypointsCache          = pilots[s]->routeWaypointsCache;
            data.pilots[s]->routeWaypointsPlanDepCache   = pilots[s]->routeWaypointsPlanDepCache;
            data.pilots[s]->routeWaypointsPlanDestCache  = pilots[s]->routeWaypointsPlanDestCache;
            data.pilots[s]->routeWaypointsPlanRouteCache = pilots[s]->routeWaypointsPlanRouteCache;
            data.pilots[s]->checkStatus();

            if ((pilots[s]->lat != 0 || pilots[s]->lon != 0) &&
                (pilots[s]->lat != data.pilots[s]->lat ||
                 pilots[s]->lon != data.pilots[s]->lon))
                *pilots[s] = *data.pilots[s];
        }
    }

    foreach(const QString s, bookedPilots.keys()) { // remove pilots that are no longer there
        if(!data.bookedPilots.contains(s)) {
            foreach(const Pilot *p, bookedPilots.values(s)) // there might be several...
                delete p;
            bookedPilots.remove(s);
        }
    }
    foreach(const QString s, data.bookedPilots.keys()) {
        if(!bookedPilots.contains(s)) { // new pilots
            Pilot *p = new Pilot(*data.bookedPilots[s]);
            bookedPilots[s] = p;
        } else // existing pilots
            *bookedPilots[s] = *data.bookedPilots[s];
    }
    qDebug() << "WhazzupData::updatePilotsFrom() -- finished";
}

void WhazzupData::updateControllersFrom(const WhazzupData &data) {
    qDebug() << "WhazzupData::updateControllersFrom()";
    foreach(const QString s, controllers.keys()) {
        if(!data.controllers.contains(s)) {
            // remove controllers that are no longer there
            delete controllers[s];
            controllers.remove(s);
        }
    }
    foreach(const QString s, data.controllers.keys()) {
        if(!controllers.contains(s)) {
            // create a new copy of new controllers
            Controller *c = new Controller(*data.controllers[s]);
            controllers[c->label] = c;
        } else // controller already exists, assign values from data
            *controllers[s] = *data.controllers[s];
    }
    qDebug() << "WhazzupData::updateControllersFrom() -- finished";
}

void WhazzupData::updateBookedControllersFrom(const WhazzupData &data) {
    qDebug() << "WhazzupData::updateBookedControllersFrom()";
    bookedControllers.clear();
    foreach(const BookedController *bc, data.bookedControllers)
        bookedControllers.append(new BookedController(*bc));
    qDebug() << "WhazzupData::updateBookedControllersFrom() -- finished";
}

void WhazzupData::updateFrom(const WhazzupData &data) {
    qDebug() << "WhazzupData::updateFrom()";
    if(this == &data)
        return;

    if(data.isNull())
        return;

    if (data._dataType == WHAZZUP || data._dataType == UNIFIED) {
        if(_dataType == ATCBOOKINGS) _dataType = UNIFIED;
        updatePilotsFrom(data);
        updateControllersFrom(data);

        servers = data.servers;
        voiceServers = data.voiceServers;
        _whazzupVersion = data._whazzupVersion;
        whazzupTime = data.whazzupTime;
        updateEarliest = data.updateEarliest;
        predictionBasedOnTime = data.predictionBasedOnTime;
        friendsLatLon = data.friendsLatLon;
    }
    if (data._dataType == ATCBOOKINGS || data._dataType == UNIFIED) {
        if(_dataType == WHAZZUP) _dataType = UNIFIED;
        updateBookedControllersFrom(data);
        bookingsTime = data.bookingsTime;
        predictionBasedOnBookingsTime = data.predictionBasedOnBookingsTime;
    }
    qDebug() << "WhazzupData::updateFrom() -- finished";
}

QSet<Controller*> WhazzupData::activeSectors() const {
    qDebug() << "WhazzupData::activeSectors()";
    QSet<Controller*> result;
    foreach(Controller *c, controllers.values())
        if (c->sector != 0)
            result.insert(c);

    qDebug() << "WhazzupData::activeSectors() -- finished";
    return result;
}

void WhazzupData::accept(MapObjectVisitor* visitor) const {
    foreach(Controller *c, controllers)
        visitor->visit(c);
    foreach(Pilot *p, pilots)
        visitor->visit(p);
    foreach(Pilot *b, bookedPilots)
        visitor->visit(b);
}
