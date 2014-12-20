/*
 * Copyright (c) 2013-2014 BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "service.hpp"

#include <bb/Application>
#include <bb/platform/Notification>
#include <bb/platform/NotificationDefaultApplicationSettings>
#include <bb/system/InvokeManager>
#include <bb/system/phone/CallState>
#include <bb/device/VibrationController>

#include <bb/pim/phone/CallHistoryFilter>
#include <bb/pim/phone/CallType>
#include <bb/pim/phone/CallHistoryParam>
#include <bb/pim/phone/CallHistoryError>
#include <bb/pim/phone/CallEntryResult>
#include <bb/pim/common/ContactEntry>
#include <bb/pim/contacts/ContactService>
#include <bb/pim/contacts/Contact>
#include <bb/pim/contacts/ContactConsts>
#include <bb/pim/phone/CallHistoryError>

#include <QTimer>
#include <QDateTime>

using namespace bb::platform;
using namespace bb::system;

Service::Service() :
        QObject(),
        m_notify(new Notification(this)),
        m_invokeManager(new InvokeManager(this))
{
    QCoreApplication::setOrganizationName("Yang Hongyang");
    QCoreApplication::setApplicationName("CallMan");

    m_invokeManager->connect(m_invokeManager, SIGNAL(invoked(const bb::system::InvokeRequest&)),
            this, SLOT(handleInvoke(const bb::system::InvokeRequest&)));

    NotificationDefaultApplicationSettings settings;
    settings.setPreview(NotificationPriorityPolicy::Allow);
    settings.setSound(bb::platform::NotificationPolicy::Deny);
    settings.setLed(bb::platform::NotificationPolicy::Deny);
    settings.apply();

    QString path = QDir::currentPath() + "/app/native/assets/mobile.db";
    sda = new bb::data::SqlDataAccess(path);
    sda->execute("set escape on;");

    //init calllog database
    QVariantList res = sda->execute("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='calllog';").value<QVariantList>();
    if (res.isEmpty())
        qDebug() << "empty";
    QVariantMap map = res[0].value<QVariantMap>();
    if (map["count(*)"].toLongLong()) {
        qDebug() << "exists";
        //sda->execute("DROP TABLE calllog;");
    } else {
        qDebug() << "not exists";
        sda->execute("CREATE TABLE `calllog` ("
                "`id` INTEGER PRIMARY KEY AUTOINCREMENT,"
                "`callEntryId` INT8 NOT NULL,"
                "`accountId` INT8 NOT NULL,"
                "`callType` INTEGER NOT NULL,"
                "`phoneNumber` CHAR(20) NOT NULL,"
                "`date` CHAR(12) NOT NULL,"
                "`time` CHAR(10) NOT NULL,"
                "`name` VARCHAR(20) NOT NULL,"
                "`city` VARCHAR(15) NOT NULL,"
                "`carrier` VARCHAR(10) NOT NULL);");
        qDebug() << "Create calllog table Haserror?: " << sda->hasError();
    }

    phone = new bb::system::phone::Phone();
    resetCall();

    bb::system::InvokeRequest request;
    request.setTarget("com.example.CallMan");
    request.setAction("bb.action.OPEN");
    m_notify->setInvokeRequest(request);

    QSettings cmsettings;
    if (cmsettings.contains("attribution/hub") && !cmsettings.value("attribution/hub").toBool())
        m_notify->setType(bb::platform::NotificationType::HubOff);

    //Start listening incoming/outcoming calls
    bb::system::InvokeRequest req;
    req.setAction("com.example.CallManService.START");
    handleInvoke(req);
}

void Service::handleInvoke(const bb::system::InvokeRequest & request)
{
    if (request.action().compare("com.example.CallManService.START") == 0) {
        QObject::connect(&callLogService,
                SIGNAL(callHistoryAdded(bb::pim::account::AccountKey, bb::pim::phone::CallEntryIdList)),
                this,
                SLOT(onCallHistoryAdded(bb::pim::account::AccountKey, bb::pim::phone::CallEntryIdList)),
                Qt::UniqueConnection);
        QObject::connect(&callLogService,
                SIGNAL(callHistoryDeleted(bb::pim::account::AccountKey, bb::pim::phone::CallEntryIdList)),
                this,
                SLOT(onCallHistoryDeleted(bb::pim::account::AccountKey, bb::pim::phone::CallEntryIdList)),
                Qt::UniqueConnection);
        bool success = QObject::connect(phone,
                SIGNAL(callUpdated(const bb::system::phone::Call &)),
                this,
                SLOT(onCallUpdated(const bb::system::phone::Call &)),
                Qt::UniqueConnection);
        if (success) {
            m_notify->setTitle(tr("CallMan"));
            m_notify->setBody(tr("Start Managing incoming/outgoing calls"));
            triggerNotification();
        } else {
            qDebug() << "Start Managing incoming/outgoing calls failed";
        }
    } else if (request.action().compare("com.example.CallManService.STOP") == 0) {
        bool success = QObject::disconnect(phone,
                SIGNAL(callUpdated(const bb::system::phone::Call &)),
                this,
                SLOT(onCallUpdated(const bb::system::phone::Call &)));
        if (success) {
            m_notify->setTitle(tr("CallMan"));
            m_notify->setBody(tr("Stop Managing incoming/outgoing calls"));
            triggerNotification();
        } else {
            qDebug() << "Stop Managing incoming/outgoing calls failed";
        }
    } else if (request.action().compare("com.example.CallManService.ENABLEHUB") == 0) {
        m_notify->setType(bb::platform::NotificationType::Default);
    } else if (request.action().compare("com.example.CallManService.DISABLEHUB") == 0) {
        m_notify->setType(bb::platform::NotificationType::HubOff);;
    } else {
        qDebug() << "Unsupported action";
    }
}

void Service::triggerNotification()
{
    onTimeout();
    //QTimer::singleShot(100, this, SLOT(onTimeout()));
}

void Service::onTimeout()
{
    Notification::deleteAllFromInbox();
    m_notify->notify();
    m_notify->clearEffects();
}

void Service::resetCall()
{
    callid = -1;
    incoming = false;
    connecting = false;
    connected = false;
    disconnected = false;
}

void Service::addCallLog(bb::pim::account::AccountKey accountId, bb::pim::phone::CallEntryIdList idList)
{
    bb::pim::phone::CallHistoryFilter filter = bb::pim::phone::CallHistoryFilter();
    bb::pim::phone::CallHistoryParam param = bb::pim::phone::CallHistoryParam();
    bb::pim::phone::CallHistoryError::Type err;

    filter.setIdFilter(idList);
    param.setContactSearchEnabled(true);
    QList<bb::pim::phone::CallEntryResult> res = callLogService.callHistory(
            accountId, filter, param, &err);

    QVariantMap map;
    bb::pim::contacts::Contact contact;
    bb::pim::contacts::ContactService contactSrv;
    QList<bb::pim::phone::CallEntryResult>::iterator i;
    for (i = res.begin(); i != res.end(); ++i) {
        bb::pim::phone::CallEntry call = (*i).call();
        QList<bb::pim::common::ContactEntry> contactList = (*i).contacts();
        QString name;
        if (contactList.size() == 0) {
            name = call.phoneNumber();
        } else {
            contact = contactSrv.contactDetails(contactList[0].accountId(), contactList[0].id());
            name = contact.displayName(bb::pim::contacts::NameOrder::LastFirst);
            if (name.isEmpty())
                name = call.phoneNumber();
        }
        QVariantMap * attrMap = getAttribution(call.phoneNumber());
        QDateTime date = call.startDate().toLocalTime();

        QString cmd = "INSERT INTO calllog (callEntryId, accountId, callType, phoneNumber, date, time, name, city, carrier) "
                "VALUES(" +
                QString::number(call.id()) + "," +
                QString::number(accountId) + "," +
                QString::number(call.callType()) + ",\"" +
                call.phoneNumber() + "\",\"" +
                date.date().toString("yyyy-MM-dd") + "\",\'" +
                date.time().toString("HH:mm") + "\',\"" +
                name + "\",\"" +
                (attrMap ? (*attrMap)["city"].toString() : "Unknown") + "\",\"" +
                (attrMap ? (*attrMap)["carrier"].toString() : "Unknown") + "\"" +
                ");";
        sda->execute(cmd);
        delete attrMap;
        qDebug() << "Haserror?: " << sda->hasError() << ", " << cmd;
    }
}

void Service::onCallHistoryAdded(bb::pim::account::AccountKey accountId, bb::pim::phone::CallEntryIdList idList)
{
    qDebug() << "call history added" << accountId << idList;
    addCallLog(accountId, idList);
    //tell ui part call log added
    QSettings settings;
    settings.setValue("tmp", true);
    settings.sync();
}

void Service::onCallHistoryDeleted(bb::pim::account::AccountKey accountId, bb::pim::phone::CallEntryIdList idList)
{
    qDebug() << "call history deleted" << accountId << idList;
}

QVariantMap * Service::getAttribution(QString pnum)
{
    QString key;
    QVariantMap map;

    if (pnum == "")
        return NULL;

    if (pnum.startsWith("+")) {
        //mobile number with country code
        QStringList list = pnum.split(" ");
        if (list.size() != 2)
            return NULL;
        list = list[1].split("-");
        if (list.size() != 3)
            return NULL;
        key = list[0] + list[1];
        qDebug() << "search key: " << key;
        QVariantList reslist = sda->execute("SELECT * FROM cellphone WHERE mobile = \""+key+"\";").value<QVariantList>();
        if (reslist.isEmpty())
            return NULL;
        map = reslist[0].value<QVariantMap>();
    } else if (pnum.startsWith("0")) {
        //telephone number with area code
        QStringList list = pnum.split(" ");
        if (list.size() != 3)
            return NULL;
        key = list[0] + list[1];
        qDebug() << "search key: " << key;
        QVariantList reslist = sda->execute("SELECT * FROM telephone WHERE region = \""+key+"\";").value<QVariantList>();
        if (reslist.isEmpty())
            return NULL;
        map = reslist[0].value<QVariantMap>();
    } else if (pnum.size() == 11 || pnum.size() == 13) {
        //mobile number without country code
        QStringList list = pnum.split("-");
        if (list.size() == 1) {
            key = pnum.left(7);
        } else if (list.size() == 3) {
            key = list[0] + list[1];
        } else {
            return NULL;
        }
        qDebug() << "search key: " << key;
        QVariantList reslist = sda->execute("SELECT * FROM cellphone WHERE mobile = \""+key+"\";").value<QVariantList>();
        if (reslist.isEmpty())
            return NULL;
        map = reslist[0].value<QVariantMap>();
    } else {
        //other number
        return NULL;
    }

    return new QVariantMap(map);
}

bool Service::displayCallAttr(const bb::system::phone::Call & call)
{
    QVariantMap *map = getAttribution(call.phoneNumber());

    if (!map)
        return false;

    //show phone number attribution
    m_notify->setTitle((*map)["city"].toString());
    m_notify->setBody((*map)["carrier"].toString() + "  " + call.phoneNumber());
    delete map;
    triggerNotification();
    return true;
}

void Service::onCallUpdated(const bb::system::phone::Call & call)
{
    if (!call.isValid())
        return;

    qDebug() << "call state: " << call.callState()
            << "number: " << call.phoneNumber()
            << "callid: " << call.callId()
            << "saved callid: " << callid
            << "call type: " << call.callType();

    QSettings cmsettings;

    if (call.callId() != callid) {
        resetCall();
        callid = call.callId();
    }

    if (call.callState() == bb::system::phone::CallState::Incoming && !incoming) {
        if (cmsettings.contains("attribution/onCall") && cmsettings.value("attribution/onCall").toBool())
            incoming = displayCallAttr(call);
    } else if (call.callState() == bb::system::phone::CallState::Connecting && !connecting) {
        if (cmsettings.contains("attribution/onCall") && cmsettings.value("attribution/onCall").toBool())
            connecting = displayCallAttr(call);
    } else if (call.callState() == bb::system::phone::CallState::Connected && !connected) {
        if (cmsettings.contains("vibration/onConnected") && cmsettings.value("vibration/onConnected").toFloat()) {
            bb::device::VibrationController ctl;
            if (ctl.isSupported())
                ctl.start(100, cmsettings.value("vibration/onConnected").toFloat()*1000);
        }
        if (cmsettings.contains("attribution/onConnected") && cmsettings.value("attribution/onConnected").toBool())
            connected = displayCallAttr(call);
    } else if (call.callState() == bb::system::phone::CallState::Disconnected && !disconnected) {
        if (cmsettings.contains("vibration/onDisconnected") && cmsettings.value("vibration/onDisconnected").toFloat()) {
            bb::device::VibrationController ctl;
            if (ctl.isSupported())
                ctl.start(100, cmsettings.value("vibration/onDisconnected").toFloat()*1000);
        }
        if (cmsettings.contains("attribution/onDisconnected") && cmsettings.value("attribution/onDisconnected").toBool())
            disconnected = displayCallAttr(call);
    } else {
        //do nothing
    }
}
