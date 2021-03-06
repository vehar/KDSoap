/****************************************************************************
** Copyright (C) 2010-2020 Klaralvdalens Datakonsult AB, a KDAB Group company, info@kdab.com.
** All rights reserved.
**
** This file is part of the KD Soap library.
**
** Licensees holding valid commercial KD Soap licenses may use this file in
** accordance with the KD Soap Commercial License Agreement provided with
** the Software.
**
**
** This file may be distributed and/or modified under the terms of the
** GNU Lesser General Public License version 2.1 and version 3 as published by the
** Free Software Foundation and appearing in the file LICENSE.LGPL.txt included.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** Contact info@kdab.com if any conditions of this licensing are not
** clear to you.
**
**********************************************************************/

#include <QNetworkReply> //may define QT_NO_OPENSSL. krazy:exclude=includes

#ifndef QT_NO_OPENSSL
#include "KDSoapSslHandler.h"

KDSoapSslHandler::KDSoapSslHandler(QObject *parent)
    : QObject(parent), m_reply(nullptr)
{
}

KDSoapSslHandler::~KDSoapSslHandler()
{
}

void KDSoapSslHandler::ignoreSslErrors()
{
    Q_ASSERT(m_reply);
    m_reply->ignoreSslErrors();
}

void KDSoapSslHandler::ignoreSslErrors(const QList<QSslError> &errors)
{
    Q_ASSERT(m_reply);
    m_reply->ignoreSslErrors(errors);
}

void KDSoapSslHandler::handleSslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    m_reply = reply;
    Q_ASSERT(m_reply);
    Q_EMIT sslErrors(this, errors);
}
#endif
