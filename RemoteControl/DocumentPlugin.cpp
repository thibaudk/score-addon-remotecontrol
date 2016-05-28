#include <RemoteControl/DocumentPlugin.hpp>

#include <core/document/Document.hpp>
#include <core/document/DocumentModel.hpp>
#include <iscore/document/DocumentInterface.hpp>

#include <RemoteControl/Scenario/Scenario.hpp>
#include <RemoteControl/Settings/Model.hpp>


#include <Device/Node/DeviceNode.hpp>
#include <Explorer/DocumentPlugin/DeviceDocumentPlugin.hpp>
#include <QBuffer>
#include <QJsonObject>
#include <QJsonDocument>
#include <iscore/tools/ModelPathSerialization.hpp>

namespace RemoteControl
{
DocumentPlugin::DocumentPlugin(
        const iscore::DocumentContext& doc,
        QObject* parent):
    iscore::DocumentPlugin{doc, "RemoteControl::DocumentPlugin", parent},
    receiver{doc, 10212}
{
    auto& set = m_context.app.settings<Settings::Model>();
    if(set.getEnabled())
    {
        create();
    }

    con(set, &Settings::Model::EnabledChanged,
        this, [=] (bool b) {
        if(b)
            create();
        else
            cleanup();
    }, Qt::QueuedConnection);
}

DocumentPlugin::~DocumentPlugin()
{
    cleanup();
}

void DocumentPlugin::create()
{
    if(m_root)
        cleanup();

    auto& doc = m_context.document.model().modelDelegate();
    auto scenar = safe_cast<Scenario::ScenarioDocumentModel*>(&doc);
    auto& cstr = scenar->baseScenario().constraint();
    m_root = new Constraint(
                getStrongId(cstr.components),
                cstr,
                *this,
                m_context,
                this);
    cstr.components.add(m_root);
}

void DocumentPlugin::cleanup()
{
    if(!m_root)
        return;

    // Delete
    auto& doc = m_context.document.model().modelDelegate();
    auto scenar = safe_cast<Scenario::ScenarioDocumentModel*>(&doc);
    auto& cstr = scenar->baseScenario().constraint();

    cstr.components.remove(m_root);
    m_root = nullptr;
}

Receiver::Receiver(
        const iscore::DocumentContext& doc,
        quint16 port):
    m_server{"i-score-ctrl",  QWebSocketServer::NonSecureMode},
    m_rootNode{doc.plugin<Explorer::DeviceDocumentPlugin>().rootNode()}
{
    if (m_server.listen(QHostAddress::Any, port))
    {
        connect(&m_server, &QWebSocketServer::newConnection,
                this, &Receiver::onNewConnection);
        connect(&m_server, &QWebSocketServer::closed,
                this, &Receiver::closed);
    }
}


Receiver::~Receiver()
{
    m_server.close();
    qDeleteAll(m_clients);
}


void Receiver::registerTimeNode(Path<Scenario::TimeNodeModel> tn)
{
    if(find(m_activeTimeNodes, tn) != m_activeTimeNodes.end())
        return;

    m_activeTimeNodes.push_back(tn);

    QJsonObject mess;
    mess["Message"] = "TimeNodeAdded";
    mess["Path"] = toJsonObject(tn);
    QJsonDocument doc{mess};
    auto json = doc.toJson();

    for(auto client : m_clients)
    {
        client->sendTextMessage(json);
    }
}


void Receiver::unregisterTimeNode(Path<Scenario::TimeNodeModel> tn)
{
    if(find(m_activeTimeNodes, tn) == m_activeTimeNodes.end())
        return;

    m_activeTimeNodes.remove(tn);

    QJsonObject mess;
    mess["Message"] = "TimeNodeRemoved";
    mess["Path"] = toJsonObject(tn);
    QJsonDocument doc{mess};
    auto json = doc.toJson();

    for(auto client : m_clients)
    {
        client->sendTextMessage(json);
    }
}


void Receiver::onNewConnection()
{
    auto client = m_server.nextPendingConnection();

    connect(client, &QWebSocket::textMessageReceived,
            this, &Receiver::processTextMessage);
    connect(client, &QWebSocket::binaryMessageReceived,
            this, &Receiver::processBinaryMessage);
    connect(client, &QWebSocket::disconnected,
            this, &Receiver::socketDisconnected);


    QJsonObject mess;
    mess["Message"] = "DeviceTree";
    mess["Path"] = toJsonObject(m_rootNode);
    QJsonDocument doc{mess};
    client->sendTextMessage(doc.toJson());
    m_clients.push_back(client);
}


void Receiver::processTextMessage(const QString& message)
{
    processBinaryMessage(message.toLatin1());
}


void Receiver::processBinaryMessage(QByteArray message)
{
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(std::move(message), &error);
    if(error.error)
        return;

    auto obj = doc.object();
    auto it = obj.find("Message");
    if(it == obj.end())
        return;

    auto mess = it->toString();
    auto answer_it = m_answers.find(mess);
    if(answer_it == m_answers.end())
        return;

    answer_it->second(obj);
}


void Receiver::socketDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());

    if (pClient)
    {
        m_clients.removeAll(pClient);
        pClient->deleteLater();
    }
}

}
