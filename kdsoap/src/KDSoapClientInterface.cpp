#include "KDSoapClientInterface.h"
#include "KDSoapClientInterface_p.h"
#include "KDSoapMessage_p.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QAuthenticator>
#include <QDebug>
#include <QBuffer>
#include <QXmlStreamWriter>
#include <QDateTime>

static const char* xmlSchemaInstanceNS = "http://www.w3.org/1999/XMLSchema-instance";
static const char* soapEncodingNS = "http://schemas.xmlsoap.org/soap/encoding/";

KDSoapClientInterface::KDSoapClientInterface(const QString& endPoint, const QString& messageNamespace)
    : d(new Private)
{
    d->m_endPoint = endPoint;
    d->m_messageNamespace = messageNamespace;
}

KDSoapClientInterface::~KDSoapClientInterface()
{
    d->m_thread.stop();
    d->m_thread.wait();
    delete d;
}

static QString variantToTextValue(const QVariant& value)
{
    switch (value.userType())
    {
    case QVariant::Char:
        // fall-through
    case QVariant::String:
        return value.toString();
    case QVariant::Url:
        // xmlpatterns/data/qatomicvalue.cpp says to do this:
        return value.toUrl().toString();
    case QVariant::ByteArray:
        return QString::fromLatin1(value.toByteArray().toBase64());
    case QVariant::Int:
        // fall-through
    case QVariant::LongLong:
        // fall-through
    case QVariant::UInt:
        return QString::number(value.toLongLong());
    case QVariant::ULongLong:
        return QString::number(value.toULongLong());
    case QVariant::Bool:
    case QMetaType::Float:
    case QVariant::Double:
        return value.toString();
    case QVariant::Time:
        return value.toDateTime().toString();
    case QVariant::Date:
        return QDateTime(value.toDate(), QTime(), Qt::UTC).toString();
    case QVariant::DateTime:
        return value.toDateTime().toString();
    default:
        if (value.userType() == qMetaTypeId<float>())
            return QString::number(value.value<float>());

        qDebug() << QString::fromLatin1("QVariants of type %1 are not supported in "
                                        "KDSoap, see the documentation").arg(QLatin1String(value.typeName()));
        return value.toString();
    }
}

static QString variantToXMLType(const QVariant& value)
{
    switch (value.userType())
    {
    case QVariant::Char:
        // fall-through
    case QVariant::String:
        // fall-through
    case QVariant::Url:
        return QLatin1String("xsd:string");
    case QVariant::ByteArray:
        return QLatin1String("xsd:base64Binary");
    case QVariant::Int:
        // fall-through
    case QVariant::LongLong:
        // fall-through
    case QVariant::UInt:
        return QLatin1String("xsd:int");
    case QVariant::ULongLong:
        return QLatin1String("xsd:unsignedInt");
    case QVariant::Bool:
        return QLatin1String("xsd:boolean");
    case QMetaType::Float:
        return QLatin1String("xsd:float");
    case QVariant::Double:
        return QLatin1String("xsd:double");
    case QVariant::Time:
        return QLatin1String("xsd:time"); // correct? xmlpatterns fallsback to datetime because of missing timezone
    case QVariant::Date:
        return QLatin1String("xsd:date");
    case QVariant::DateTime:
        return QLatin1String("xsd:dateTime");
    default:
        if (value.userType() == qMetaTypeId<float>())
            return QLatin1String("xsd:float");
        qDebug() << QString::fromLatin1("QVariants of type %1 are not supported in "
                                        "KDSoap, see the documentation").arg(QLatin1String(value.typeName()));
        return QString();
    }
}

KDSoapClientInterface::Private::Private()
    : m_authentication()
{
    connect(&m_accessManager, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
            this, SLOT(_kd_slotAuthenticationRequired(QNetworkReply*,QAuthenticator*)));
}

QNetworkRequest KDSoapClientInterface::Private::prepareRequest(const QString &method, const QString& action)
{
    QNetworkRequest request(QUrl(this->m_endPoint));
    // Seems SOAP-1.2 uses application/soap+xml instead of text/xml.
    // TODO: SOAP-1.2 support: "application/soap+xml;charset=utf-8;action=" + soapAction.toUtf8()
    request.setHeader(QNetworkRequest::ContentTypeHeader, QLatin1String("text/xml;charset=utf-8"));
    // The soap action seems to be namespace + method in most cases, but not always
    // (e.g. urn:GoogleSearchAction for google).
    QString soapAction = action;
    if (soapAction.isEmpty()) {
        // Does the namespace always end with a '/'?
        soapAction = this->m_messageNamespace + /*QChar::fromLatin1('/') +*/ method;
    }
    //qDebug() << "soapAction=" << soapAction;
    request.setRawHeader("SoapAction", soapAction.toUtf8());
    
    // FIXME need to find out which version of Qt this is no longer necessary
    // without that the server might respond with gzip compressed data and
    // Qt 4.6.2 fails to decode that properly
    //
    // happens with retrieval calls in against SugarCRM 5.5.1 running on Apache 2.2.15
    // when the response seems to reach a certain size threshold
    request.setRawHeader( "Accept-Encoding", "compress" );

    return request;
}

class KDSoapNamespacePrefixes : public QMap<QString /*ns*/, QString /*prefix*/>
{
public:
    void writeNamespace(QXmlStreamWriter& writer, const QString& ns, const QString& prefix) {
        //qDebug() << "writeNamespace" << ns << prefix;
        insert(ns, prefix);
        writer.writeNamespace(ns, prefix);
    }
    QString resolve(const QString& ns, const QString& localName) const {
        const QString prefix = value(ns);
        if (prefix.isEmpty()) {
            qWarning("ERROR: Namespace not found: %s (for localName %s)", qPrintable(ns), qPrintable(localName));
        }
        return prefix + QLatin1Char(':') + localName;
    }
};

QBuffer* KDSoapClientInterface::Private::prepareRequestBuffer(const QString& method, const KDSoapMessage& message, const KDSoapHeaders& headers)
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.writeStartDocument();

    const QString soapNS = QString::fromLatin1("http://schemas.xmlsoap.org/soap/envelope/");
    const QString xmlSchemaNS = QString::fromLatin1("http://www.w3.org/1999/XMLSchema");

    KDSoapNamespacePrefixes namespacePrefixes;

    namespacePrefixes.writeNamespace(writer, soapNS, QLatin1String("soap"));
    namespacePrefixes.writeNamespace(writer, QLatin1String(soapEncodingNS), QLatin1String("soap-enc"));
    namespacePrefixes.writeNamespace(writer, xmlSchemaNS, QLatin1String("xsd"));
    namespacePrefixes.writeNamespace(writer, QLatin1String(xmlSchemaInstanceNS), QLatin1String("xsi"));

    // Also insert known variants
    namespacePrefixes.insert(QString::fromLatin1("http://www.w3.org/2001/XMLSchema"), QString::fromLatin1("xsd"));
    namespacePrefixes.insert(QString::fromLatin1("http://www.w3.org/2001/XMLSchema-instance"), QString::fromLatin1("xsi"));

    writer.writeStartElement(soapNS, QLatin1String("Envelope"));
    writer.writeAttribute(soapNS, QLatin1String("encodingStyle"), QLatin1String("http://schemas.xmlsoap.org/soap/encoding/"));

    if (!headers.isEmpty() || !m_persistentHeaders.isEmpty()) {
        // This writeNamespace line adds the xmlns:n1 to <Envelope>, which looks ugly and unusual (and breaks all unittests)
        // However it's the best solution in case of headers, otherwise we get n1 in the header and n2 in the body,
        // and xsi:type attributes that refer to n1, which isn't defined in the body...
        namespacePrefixes.writeNamespace(writer, this->m_messageNamespace, QLatin1String("n1") /*make configurable?*/);
        writer.writeStartElement(soapNS, QLatin1String("Header"));
        Q_FOREACH(const KDSoapMessage& header, m_persistentHeaders) {
            writeArguments(namespacePrefixes, writer, header.d->args, header.use());
        }
        Q_FOREACH(const KDSoapMessage& header, headers) {
            writeArguments(namespacePrefixes, writer, header.d->args, header.use());
        }
        writer.writeEndElement(); // Header
    } else {
        // So in the standard case (no headers) we just rely on Qt calling it n1 and insert it into the map.
        // Calling this after the writeStartElement(method) below leads to a double-definition of n1.
        namespacePrefixes.insert(this->m_messageNamespace, QString::fromLatin1("n1"));
    }

    writer.writeStartElement(soapNS, QLatin1String("Body"));
    writer.writeStartElement(this->m_messageNamespace, method);

    // Arguments
    const KDSoapValueList args = message.d->args;
    writeArguments(namespacePrefixes, writer, args, message.use());

    writer.writeEndElement(); // <method>
    writer.writeEndElement(); // Body
    writer.writeEndElement(); // Envelope
    writer.writeEndDocument();

    if (qgetenv("KDSOAP_DEBUG").toInt()) {
        qDebug() << data;
    }

    QBuffer* buffer = new QBuffer;
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);
    return buffer;
}

void KDSoapClientInterface::Private::writeArguments(KDSoapNamespacePrefixes& namespacePrefixes, QXmlStreamWriter& writer, const KDSoapValueList& args, KDSoapMessage::Use use)
{
    KDSoapValueListIterator it(args);
    while (it.hasNext()) {
        const KDSoapValue& argument = it.next();
        const QVariant value = argument.value();
        if (value.canConvert<KDSoapValueList>()) {
            writer.writeStartElement(this->m_messageNamespace, argument.name());
            const KDSoapValueList list = value.value<KDSoapValueList>();
            if (use == KDSoapMessage::EncodedUse) {
                // use=encoded means writing out xsi:type attributes. http://www.eherenow.com/soapfight.htm taught me that.
                writer.writeAttribute(QLatin1String(xmlSchemaInstanceNS), QLatin1String("type"), namespacePrefixes.resolve(list.typeNs(), list.type()));
                const bool isArray = !list.arrayType().isEmpty();
                if (isArray) {
                    writer.writeAttribute(QLatin1String(soapEncodingNS), QLatin1String("arrayType"), namespacePrefixes.resolve(list.arrayTypeNs(), list.arrayType()) + QLatin1Char('[') + QString::number(list.count()) + QLatin1Char(']'));
                }
            }
            writeArguments(namespacePrefixes, writer, list, use); // recursive call
            writer.writeEndElement();
        } else {
            const QString type = variantToXMLType(value);
            if (!type.isEmpty()) {
                writer.writeStartElement(this->m_messageNamespace, argument.name());
                writer.writeAttribute(QLatin1String(xmlSchemaInstanceNS), QLatin1String("type"), type);
                writer.writeCharacters(variantToTextValue(value));
                writer.writeEndElement();
            }
        }
    }
}

KDSoapPendingCall KDSoapClientInterface::asyncCall(const QString &method, const KDSoapMessage &message, const QString& soapAction, const KDSoapHeaders& headers)
{
    QBuffer* buffer = d->prepareRequestBuffer(method, message, headers);
    QNetworkRequest request = d->prepareRequest(method, soapAction);
    QNetworkReply* reply = d->m_accessManager.post(request, buffer);
    return KDSoapPendingCall(reply, buffer);
}

KDSoapMessage KDSoapClientInterface::call(const QString& method, const KDSoapMessage &message, const QString& soapAction, const KDSoapHeaders& headers)
{
    // Problem is: I don't want a nested event loop here. Too dangerous for GUI programs.
    // I wanted a socket->waitFor... but we don't have access to the actual socket in QNetworkAccess.
    // So the only option that remains is a thread and acquiring a semaphore...
    KDSoapThreadTaskData* task = new KDSoapThreadTaskData(this, method, message, soapAction, headers);
    task->m_authentication = d->m_authentication;
    d->m_thread.enqueue(task);
    if (!d->m_thread.isRunning())
        d->m_thread.start();
    task->waitForCompletion();
    KDSoapMessage ret = task->returnArguments();
    delete task;
    return ret;
}

void KDSoapClientInterface::callNoReply(const QString &method, const KDSoapMessage &message, const QString &soapAction, const KDSoapHeaders& headers)
{
    QBuffer* buffer = d->prepareRequestBuffer(method, message, headers);
    QNetworkRequest request = d->prepareRequest(method, soapAction);
    QNetworkReply* reply = d->m_accessManager.post(request, buffer);
    QObject::connect(reply, SIGNAL(finished()), reply, SLOT(deleteLater()));
}

void KDSoapClientInterface::Private::_kd_slotAuthenticationRequired(QNetworkReply* reply, QAuthenticator* authenticator)
{
    m_authentication.handleAuthenticationRequired(reply, authenticator);
}

void KDSoapClientInterface::setAuthentication(const KDSoapAuthentication &authentication)
{
    d->m_authentication = authentication;
}

void KDSoapClientInterface::setHeader(const QString& name, const KDSoapMessage &header)
{
    d->m_persistentHeaders[name] = header;
}

#include "moc_KDSoapClientInterface_p.cpp"