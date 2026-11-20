// Certificate based signatures: PKCS#12 digital IDs, CMS/CAdES signing (PAdES baseline),
// RFC 3161 timestamps and signature verification. Built on OpenSSL.
#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <memory>

namespace ink {

struct DigitalId {
    void *key = nullptr;    // EVP_PKEY*
    void *cert = nullptr;   // X509*
    void *chain = nullptr;  // STACK_OF(X509)*
    QString cn, o, ou, email, c;
    QDateTime notAfter;
    ~DigitalId();
};

std::shared_ptr<DigitalId> loadPkcs12(const QString &path, const QString &password, QString *err);

bool createSelfSignedId(const QString &path, const QString &password, const QString &name,
                        const QString &org, const QString &email, const QString &country,
                        int years, QString *err);

// Upper bound of the DER size cmsSign() will return.
int cmsMaxSize(const DigitalId &id, bool withTimestamp);

// Detached CAdES-BES signature over `data` (SHA-256, ESS signing-certificate-v2).
// If tsaUrl is set a signature timestamp is embedded (PAdES B-T).
QByteArray cmsSign(const DigitalId &id, const QByteArray &data, const QString &tsaUrl, QString *err);

struct VerifyResult {
    bool parsed = false, digestOk = false, trusted = false, selfSigned = false, hasTimestamp = false;
    QString signerCn, issuerCn, error, trustError;
    QDateTime timestampTime, notBefore, notAfter;
    QByteArray certDer;
};

// Verify a detached CMS signature. `at` is the time used for certificate validity
// (invalid = now). extraTrusted: DER certificates the user chose to trust.
VerifyResult cmsVerify(const QByteArray &cms, const QByteArray &data, const QDateTime &at,
                       const QList<QByteArray> &extraTrusted);

// Parse an RFC 3161 token (document timestamp) for display.
VerifyResult tsTokenInfo(const QByteArray &token);

} // namespace ink
