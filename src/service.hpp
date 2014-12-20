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

#ifndef SERVICE_H_
#define SERVICE_H_

#include <QObject>
#include <bb/system/phone/Phone>
#include <bb/system/phone/Call>
#include <bb/data/SqlDataAccess>
#include <bb/pim/phone/CallHistoryService>
#include <bb/pim/account/Account>
#include <bb/pim/phone/CallEntry>

namespace bb {
    class Application;
    namespace platform {
        class Notification;
    }
    namespace system {
        class InvokeManager;
        class InvokeRequest;
    }
}

class Service: public QObject
{
    Q_OBJECT
public:
    Service();
    virtual ~Service() {}
private slots:
    void handleInvoke(const bb::system::InvokeRequest &);
    void onCallUpdated(const bb::system::phone::Call &);
    void onTimeout();
    void onCallHistoryAdded(bb::pim::account::AccountKey, bb::pim::phone::CallEntryIdList);
    void onCallHistoryDeleted(bb::pim::account::AccountKey, bb::pim::phone::CallEntryIdList);

private:
    void triggerNotification();
    void resetCall();
    void addCallLog(bb::pim::account::AccountKey, bb::pim::phone::CallEntryIdList);
    bool displayCallAttr(const bb::system::phone::Call &);
    QVariantMap * getAttribution(QString);

    bb::platform::Notification * m_notify;
    bb::system::InvokeManager * m_invokeManager;
    bb::system::phone::Phone * phone;
    bb::data::SqlDataAccess * sda;
    bb::pim::phone::CallHistoryService callLogService;
    int callid;
    bool incoming;
    bool connecting;
    bool connected;
    bool disconnected;
};

#endif /* SERVICE_H_ */
