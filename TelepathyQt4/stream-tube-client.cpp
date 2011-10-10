/**
 * This file is part of TelepathyQt4
 *
 * @copyright Copyright (C) 2011 Collabora Ltd. <http://www.collabora.co.uk/>
 * @copyright Copyright (C) 2011 Nokia Corporation
 * @license LGPL 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <TelepathyQt4/StreamTubeClient>

#include "TelepathyQt4/stream-tube-client-internal.h"
#include "TelepathyQt4/_gen/stream-tube-client.moc.hpp"
#include "TelepathyQt4/_gen/stream-tube-client-internal.moc.hpp"

#include "TelepathyQt4/debug-internal.h"
#include "TelepathyQt4/simple-stream-tube-handler.h"

#include <TelepathyQt4/AccountManager>
#include <TelepathyQt4/ClientRegistrar>
#include <TelepathyQt4/IncomingStreamTubeChannel>
#include <TelepathyQt4/PendingStreamTubeConnection>
#include <TelepathyQt4/StreamTubeChannel>

#include <QAbstractSocket>
#include <QHash>

namespace Tp
{

struct TELEPATHY_QT4_NO_EXPORT StreamTubeClient::Tube::Private : public QSharedData
{
    // empty placeholder for now
};

StreamTubeClient::Tube::Tube()
{
    // invalid instance
}

StreamTubeClient::Tube::Tube(
        const AccountPtr &account,
        const IncomingStreamTubeChannelPtr &channel)
    : QPair<AccountPtr, IncomingStreamTubeChannelPtr>(account, channel), mPriv(new Private)
{
}

StreamTubeClient::Tube::Tube(
        const Tube &a)
    : QPair<AccountPtr, IncomingStreamTubeChannelPtr>(a.account(), a.channel()), mPriv(a.mPriv)
{
}

StreamTubeClient::Tube::~Tube()
{
    // mPriv deleted automatically
}

StreamTubeClient::Tube &StreamTubeClient::Tube::operator=(
        const Tube &a)
{
    if (&a == this) {
        return *this;
    }

    first = a.account();
    second = a.channel();
    mPriv = a.mPriv;

    return *this;
}

struct TELEPATHY_QT4_NO_EXPORT StreamTubeClient::Private
{
    Private(const ClientRegistrarPtr &registrar,
            const QStringList &p2pServices,
            const QStringList &roomServices,
            const QString &maybeClientName,
            bool monitorConnections,
            bool bypassApproval)
        : registrar(registrar),
          handler(SimpleStreamTubeHandler::create(
                      p2pServices, roomServices, false, monitorConnections, bypassApproval)),
          clientName(maybeClientName),
          isRegistered(false),
          acceptsAsTcp(false), acceptsAsUnix(false),
          tcpGenerator(0), requireCredentials(false)
    {
        if (clientName.isEmpty()) {
            clientName = QString::fromLatin1("TpQt4STubeClient_%1_%2")
                .arg(registrar->dbusConnection().baseService()
                        .replace(QLatin1Char(':'), QLatin1Char('_'))
                        .replace(QLatin1Char('.'), QLatin1Char('_')))
                .arg((intptr_t) this, 0, 16);
        }
    }

    void ensureRegistered()
    {
        if (isRegistered) {
            return;
        }

        debug() << "Register StreamTubeClient with name " << clientName;

        if (registrar->registerClient(handler, clientName)) {
            isRegistered = true;
        } else {
            warning() << "StreamTubeClient" << clientName
                << "registration failed";
        }
    }

    ClientRegistrarPtr registrar;
    SharedPtr<SimpleStreamTubeHandler> handler;
    QString clientName;
    bool isRegistered;

    bool acceptsAsTcp, acceptsAsUnix;
    TcpSourceAddressGenerator *tcpGenerator;
    bool requireCredentials;

    QHash<StreamTubeChannelPtr, TubeWrapper *> tubes;
};

StreamTubeClient::TubeWrapper::TubeWrapper(
        const AccountPtr &acc,
        const IncomingStreamTubeChannelPtr &tube,
        const QHostAddress &sourceAddress,
        quint16 sourcePort,
        StreamTubeClient *parent)
    : QObject(parent), mAcc(acc), mTube(tube), mSourceAddress(sourceAddress), mSourcePort(sourcePort)
{
    if (sourcePort != 0) {
        if ((sourceAddress.protocol() == QAbstractSocket::IPv4Protocol &&
                    !tube->supportsIPv4SocketsWithSpecifiedAddress()) ||
                (sourceAddress.protocol() == QAbstractSocket::IPv6Protocol &&
                 !tube->supportsIPv6SocketsWithSpecifiedAddress())) {
            debug() << "StreamTubeClient falling back to Localhost AC for tube" <<
                tube->objectPath();
            mSourceAddress = sourceAddress.protocol() == QAbstractSocket::IPv4Protocol ?
                QHostAddress::Any : QHostAddress::AnyIPv6;
            mSourcePort = 0;
        }
    }

    connect(tube->acceptTubeAsTcpSocket(mSourceAddress, mSourcePort),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onTubeAccepted(Tp::PendingOperation*)));
    connect(tube.data(),
            SIGNAL(newConnection(uint)),
            SLOT(onNewConnection(uint)));
    connect(tube.data(),
            SIGNAL(connectionClosed(uint,QString,QString)),
            SLOT(onConnectionClosed(uint,QString,QString)));
}

StreamTubeClient::TubeWrapper::TubeWrapper(
        const AccountPtr &acc,
        const IncomingStreamTubeChannelPtr &tube,
        bool requireCredentials,
        StreamTubeClient *parent)
    : QObject(parent), mAcc(acc), mTube(tube), mSourcePort(0)
{
    if (requireCredentials && !tube->supportsUnixSocketsWithCredentials()) {
        debug() << "StreamTubeClient falling back to Localhost AC for tube" << tube->objectPath();
        requireCredentials = false;
    }

    connect(tube->acceptTubeAsUnixSocket(requireCredentials),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onTubeAccepted(Tp::PendingOperation*)));
    connect(tube.data(),
            SIGNAL(newConnection(uint)),
            SLOT(onNewConnection(uint)));
    connect(tube.data(),
            SIGNAL(connectionClosed(uint,QString,QString)),
            SLOT(onConnectionClosed(uint,QString,QString)));
}

void StreamTubeClient::TubeWrapper::onTubeAccepted(Tp::PendingOperation *op)
{
    emit acceptFinished(this, qobject_cast<Tp::PendingStreamTubeConnection *>(op));
}

void StreamTubeClient::TubeWrapper::onNewConnection(uint conn)
{
    emit newConnection(this, conn);
}

void StreamTubeClient::TubeWrapper::onConnectionClosed(uint conn, const QString &error,
        const QString &message)
{
    emit connectionClosed(this, conn, error, message);
}

StreamTubeClientPtr StreamTubeClient::create(
        const QStringList &p2pServices,
        const QStringList &roomServices,
        const QString &clientName,
        bool monitorConnections,
        bool bypassApproval,
        const AccountFactoryConstPtr &accountFactory,
        const ConnectionFactoryConstPtr &connectionFactory,
        const ChannelFactoryConstPtr &channelFactory,
        const ContactFactoryConstPtr &contactFactory)
{
    return create(
            QDBusConnection::sessionBus(),
            accountFactory,
            connectionFactory,
            channelFactory,
            contactFactory,
            p2pServices,
            roomServices,
            clientName,
            monitorConnections,
            bypassApproval);
}

StreamTubeClientPtr StreamTubeClient::create(
        const QDBusConnection &bus,
        const AccountFactoryConstPtr &accountFactory,
        const ConnectionFactoryConstPtr &connectionFactory,
        const ChannelFactoryConstPtr &channelFactory,
        const ContactFactoryConstPtr &contactFactory,
        const QStringList &p2pServices,
        const QStringList &roomServices,
        const QString &clientName,
        bool monitorConnections,
        bool bypassApproval)
{
    return create(
            ClientRegistrar::create(
                bus,
                accountFactory,
                connectionFactory,
                channelFactory,
                contactFactory),
            p2pServices,
            roomServices,
            clientName,
            monitorConnections,
            bypassApproval);
}

StreamTubeClientPtr StreamTubeClient::create(
        const AccountManagerPtr &accountManager,
        const QStringList &p2pServices,
        const QStringList &roomServices,
        const QString &clientName,
        bool monitorConnections,
        bool bypassApproval)
{
    return create(
            accountManager->dbusConnection(),
            accountManager->accountFactory(),
            accountManager->connectionFactory(),
            accountManager->channelFactory(),
            accountManager->contactFactory(),
            p2pServices,
            roomServices,
            clientName,
            monitorConnections,
            bypassApproval);
}

StreamTubeClientPtr StreamTubeClient::create(
        const ClientRegistrarPtr &registrar,
        const QStringList &p2pServices,
        const QStringList &roomServices,
        const QString &clientName,
        bool monitorConnections,
        bool bypassApproval)
{
    if (p2pServices.isEmpty() && roomServices.isEmpty()) {
        warning() << "Tried to create a StreamTubeClient with no services, returning NULL";
        return StreamTubeClientPtr();
    }

    return StreamTubeClientPtr(
            new StreamTubeClient(registrar, p2pServices, roomServices, clientName,
                monitorConnections, bypassApproval));
}

StreamTubeClient::StreamTubeClient(
        const ClientRegistrarPtr &registrar,
        const QStringList &p2pServices,
        const QStringList &roomServices,
        const QString &clientName,
        bool monitorConnections,
        bool bypassApproval)
    : mPriv(new Private(registrar, p2pServices, roomServices, clientName, monitorConnections, bypassApproval))
{
    connect(mPriv->handler.data(),
            SIGNAL(invokedForTube(
                    Tp::AccountPtr,
                    Tp::StreamTubeChannelPtr,
                    QDateTime,
                    Tp::ChannelRequestHints)),
            SLOT(onInvokedForTube(
                    Tp::AccountPtr,
                    Tp::StreamTubeChannelPtr,
                    QDateTime,
                    Tp::ChannelRequestHints)));
}

/**
 * Class destructor.
 */
StreamTubeClient::~StreamTubeClient()
{
    if (isRegistered()) {
        mPriv->registrar->unregisterClient(mPriv->handler);
    }

    delete mPriv;
}

ClientRegistrarPtr StreamTubeClient::registrar() const
{
    return mPriv->registrar;
}

QString StreamTubeClient::clientName() const
{
    return mPriv->clientName;
}

bool StreamTubeClient::isRegistered() const
{
    return mPriv->isRegistered;
}

bool StreamTubeClient::monitorsConnections() const
{
    return mPriv->handler->monitorsConnections();
}

bool StreamTubeClient::acceptsAsTcp() const
{
    return mPriv->acceptsAsTcp;
}

StreamTubeClient::TcpSourceAddressGenerator *StreamTubeClient::tcpGenerator() const
{
    if (!acceptsAsTcp()) {
        warning() << "StreamTubeClient::tcpGenerator() used, but not accepting as TCP, returning 0";
        return 0;
    }

    return mPriv->tcpGenerator;
}

bool StreamTubeClient::acceptsAsUnix() const
{
    return mPriv->acceptsAsUnix;
}

void StreamTubeClient::setToAcceptAsTcp(TcpSourceAddressGenerator *generator)
{
    mPriv->tcpGenerator = generator;
    mPriv->acceptsAsTcp = true;
    mPriv->acceptsAsUnix = false;

    mPriv->ensureRegistered();
}

void StreamTubeClient::setToAcceptAsUnix(bool requireCredentials)
{
    mPriv->tcpGenerator = 0;
    mPriv->acceptsAsTcp = false;
    mPriv->acceptsAsUnix = true;
    mPriv->requireCredentials = requireCredentials;

    mPriv->ensureRegistered();
}

QList<StreamTubeClient::Tube> StreamTubeClient::tubes() const
{
    QList<Tube> tubes;

    foreach (TubeWrapper *wrapper, mPriv->tubes.values()) {
        tubes.push_back(Tube(wrapper->mAcc, wrapper->mTube));
    }

    return tubes;
}

QHash<StreamTubeClient::Tube, QSet<uint> > StreamTubeClient::connections() const
{
    QHash<Tube, QSet<uint> > conns;
    if (!monitorsConnections()) {
        warning() << "StreamTubeClient::connections() used, but connection monitoring is disabled";
        return conns;
    }

    foreach (const Tube &tube, tubes()) {
        if (!tube.channel()->isValid()) {
            // The tube has been invalidated, so skip it to avoid warnings
            // We're going to get rid of the wrapper in the next mainloop iteration when we get the
            // invalidation signal
            continue;
        }

        QSet<uint> tubeConns = QSet<uint>::fromList(tube.channel()->connections());
        if (!tubeConns.empty()) {
            conns.insert(tube, tubeConns);
        }
    }

    return conns;
}

void StreamTubeClient::onInvokedForTube(
        const AccountPtr &acc,
        const StreamTubeChannelPtr &tube,
        const QDateTime &time,
        const ChannelRequestHints &hints)
{
    Q_ASSERT(isRegistered()); // our SSTH shouldn't be receiving any channels unless it's registered
    Q_ASSERT(!tube->isRequested());
    Q_ASSERT(tube->isValid()); // SSTH won't emit invalid tubes

    if (mPriv->tubes.contains(tube)) {
        debug() << "Ignoring StreamTubeClient reinvocation for tube" << tube->objectPath();
        return;
    }

    IncomingStreamTubeChannelPtr incoming = IncomingStreamTubeChannelPtr::qObjectCast(tube);

    if (!incoming) {
        warning() << "The ChannelFactory used by StreamTubeClient must construct" <<
            "IncomingStreamTubeChannel subclasses for Requested=false StreamTubes";
        tube->requestClose();
        return;
    }

    TubeWrapper *wrapper = 0;

    if (mPriv->acceptsAsTcp) {
        QPair<QHostAddress, quint16> srcAddr =
            qMakePair(QHostAddress(QHostAddress::Any), quint16(0));

        if (mPriv->tcpGenerator) {
            srcAddr = mPriv->tcpGenerator->nextSourceAddress(acc, incoming);
        }

        wrapper = new TubeWrapper(acc, incoming, srcAddr.first, srcAddr.second, this);
    } else {
        Q_ASSERT(mPriv->acceptsAsUnix); // we should only be registered when we're set to accept as either TCP or Unix
        wrapper = new TubeWrapper(acc, incoming, mPriv->requireCredentials, this);
    }

    connect(wrapper,
            SIGNAL(acceptFinished(TubeWrapper*,Tp::PendingStreamTubeConnection*)),
            SLOT(onAcceptFinished(TubeWrapper*,Tp::PendingStreamTubeConnection*)));
    connect(tube.data(),
            SIGNAL(invalidated(Tp::DBusProxy*,QString,QString)),
            SLOT(onTubeInvalidated(Tp::DBusProxy*,QString,QString)));

    if (monitorsConnections()) {
        connect(wrapper,
                SIGNAL(newConnection(TubeWrapper*,uint)),
                SLOT(onNewConnection(TubeWrapper*,uint)));
        connect(wrapper,
                SIGNAL(connectionClosed(TubeWrapper*,uint,QString,QString)),
                SLOT(onConnectionClosed(TubeWrapper*,uint,QString,QString)));
    }

    mPriv->tubes.insert(tube, wrapper);

    emit tubeOffered(acc, incoming);
}

void StreamTubeClient::onAcceptFinished(TubeWrapper *wrapper, PendingStreamTubeConnection *conn)
{
    Q_ASSERT(wrapper != NULL);
    Q_ASSERT(conn != NULL);

    if (!mPriv->tubes.contains(wrapper->mTube)) {
        debug() << "StreamTubeClient ignoring Accept result for invalidated tube"
            << wrapper->mTube->objectPath();
        return;
    }

    if (conn->isError()) {
        warning() << "StreamTubeClient couldn't accept tube" << wrapper->mTube->objectPath() << '-'
            << conn->errorName() << ':' << conn->errorMessage();

        if (wrapper->mTube->isValid()) {
            wrapper->mTube->requestClose();
        }

        wrapper->mTube->disconnect(this);
        emit tubeClosed(wrapper->mAcc, wrapper->mTube, conn->errorName(), conn->errorMessage());
        mPriv->tubes.remove(wrapper->mTube);
        wrapper->deleteLater();
        return;
    }

    debug() << "StreamTubeClient accepted tube" << wrapper->mTube->objectPath();

    if (conn->addressType() == SocketAddressTypeIPv4
            || conn->addressType() == SocketAddressTypeIPv6) {
        QPair<QHostAddress, quint16> addr = conn->ipAddress();
        emit tubeAcceptedAsTcp(addr.first, addr.second, wrapper->mSourceAddress,
                wrapper->mSourcePort, wrapper->mAcc, wrapper->mTube);
    } else {
        emit tubeAcceptedAsUnix(conn->localAddress(), conn->requiresCredentials(),
                conn->credentialByte(), wrapper->mAcc, wrapper->mTube);
    }
}

void StreamTubeClient::onTubeInvalidated(Tp::DBusProxy *proxy, const QString &error,
        const QString &message)
{
    StreamTubeChannelPtr tube(qobject_cast<StreamTubeChannel *>(proxy));
    Q_ASSERT(!tube.isNull());

    TubeWrapper *wrapper = mPriv->tubes.value(tube);
    if (!wrapper) {
        // Accept finish with error already removed it
        return;
    }

    debug() << "Client StreamTube" << tube->objectPath() << "invalidated - " << error << ':'
        << message;

    emit tubeClosed(wrapper->mAcc, wrapper->mTube, error, message);
    mPriv->tubes.remove(tube);
    delete wrapper;
}

void StreamTubeClient::onNewConnection(
        TubeWrapper *wrapper,
        uint conn)
{
    Q_ASSERT(monitorsConnections());
    emit newConnection(wrapper->mAcc, wrapper->mTube, conn);
}

void StreamTubeClient::onConnectionClosed(
        TubeWrapper *wrapper,
        uint conn,
        const QString &error,
        const QString &message)
{
    Q_ASSERT(monitorsConnections());
    emit connectionClosed(wrapper->mAcc, wrapper->mTube, conn, error, message);
}

} // Tp
