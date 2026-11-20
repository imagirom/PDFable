#include "Signer.h"

#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrl>

#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/provider.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/ts.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace ink {

static void initCrypto()
{
    static bool done = false;
    if (done) return;
    done = true;
    OSSL_PROVIDER_try_load(nullptr, "default", 1);
    OSSL_PROVIDER_try_load(nullptr, "legacy", 1);   // old .pfx files use RC2/3DES
}

static QString sslError(const char *what)
{
    char buf[256] = "";
    unsigned long e = ERR_get_error();
    if (e) ERR_error_string_n(e, buf, sizeof buf);
    ERR_clear_error();
    return e ? QStringLiteral("%1: %2").arg(QString::fromLatin1(what), QString::fromLatin1(buf)) : QString::fromLatin1(what);
}

static QString nameEntry(X509_NAME *n, int nid)
{
    int idx = X509_NAME_get_index_by_NID(n, nid, -1);
    if (idx < 0) return QString();
    unsigned char *utf8 = nullptr;
    int len = ASN1_STRING_to_UTF8(&utf8, X509_NAME_ENTRY_get_data(X509_NAME_get_entry(n, idx)));
    QString s = len > 0 ? QString::fromUtf8(reinterpret_cast<char *>(utf8), len) : QString();
    OPENSSL_free(utf8);
    return s;
}

static QDateTime asn1Time(const ASN1_TIME *t)
{
    struct tm tm{};
    if (!t || !ASN1_TIME_to_tm(t, &tm)) return QDateTime();
    return QDateTime(QDate(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday), QTime(tm.tm_hour, tm.tm_min, tm.tm_sec), Qt::UTC);
}

static QByteArray certDer(X509 *x)
{
    unsigned char *p = nullptr;
    int n = i2d_X509(x, &p);
    QByteArray der = n > 0 ? QByteArray(reinterpret_cast<char *>(p), n) : QByteArray();
    OPENSSL_free(p);
    return der;
}

DigitalId::~DigitalId()
{
    if (key) EVP_PKEY_free(static_cast<EVP_PKEY *>(key));
    if (cert) X509_free(static_cast<X509 *>(cert));
    if (chain) sk_X509_pop_free(static_cast<STACK_OF(X509) *>(chain), X509_free);
}

std::shared_ptr<DigitalId> loadPkcs12(const QString &path, const QString &password, QString *err)
{
    initCrypto();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return nullptr;
    }
    QByteArray bytes = f.readAll();
    const unsigned char *p = reinterpret_cast<const unsigned char *>(bytes.constData());
    PKCS12 *p12 = d2i_PKCS12(nullptr, &p, bytes.size());
    if (!p12) {
        if (err) *err = QStringLiteral("Not a PKCS#12 (.p12/.pfx) file");
        return nullptr;
    }
    EVP_PKEY *key = nullptr;
    X509 *cert = nullptr;
    STACK_OF(X509) *ca = nullptr;
    int ok = PKCS12_parse(p12, password.toUtf8().constData(), &key, &cert, &ca);
    PKCS12_free(p12);
    if (!ok || !key || !cert) {
        if (err) *err = QStringLiteral("Wrong password, or the file holds no private key");
        ERR_clear_error();
        if (key) EVP_PKEY_free(key);
        if (cert) X509_free(cert);
        if (ca) sk_X509_pop_free(ca, X509_free);
        return nullptr;
    }
    auto id = std::make_shared<DigitalId>();
    id->key = key;
    id->cert = cert;
    id->chain = ca;
    X509_NAME *n = X509_get_subject_name(cert);
    id->cn = nameEntry(n, NID_commonName);
    id->o = nameEntry(n, NID_organizationName);
    id->ou = nameEntry(n, NID_organizationalUnitName);
    id->email = nameEntry(n, NID_pkcs9_emailAddress);
    id->c = nameEntry(n, NID_countryName);
    id->notAfter = asn1Time(X509_get0_notAfter(cert));
    return id;
}

static void addExt(X509 *cert, int nid, const char *value)
{
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    if (X509_EXTENSION *ex = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value)) {
        X509_add_ext(cert, ex, -1);
        X509_EXTENSION_free(ex);
    }
}

bool createSelfSignedId(const QString &path, const QString &password, const QString &name, const QString &org,
                        const QString &email, const QString &country, int years, QString *err)
{
    initCrypto();
    EVP_PKEY *key = EVP_RSA_gen(3072);
    if (!key) {
        if (err) *err = sslError("key generation failed");
        return false;
    }
    X509 *cert = X509_new();
    X509_set_version(cert, 2);
    unsigned char serial[16];
    RAND_bytes(serial, sizeof serial);
    serial[0] &= 0x7f;
    BIGNUM *bn = BN_bin2bn(serial, sizeof serial, nullptr);
    BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert));
    BN_free(bn);
    X509_gmtime_adj(X509_getm_notBefore(cert), -300);
    X509_gmtime_adj(X509_getm_notAfter(cert), long(60) * 60 * 24 * 365 * qBound(1, years, 30));
    X509_set_pubkey(cert, key);
    X509_NAME *n = X509_get_subject_name(cert);
    auto add = [&](const char *field, const QString &v) {
        if (!v.trimmed().isEmpty())
            X509_NAME_add_entry_by_txt(n, field, MBSTRING_UTF8, reinterpret_cast<const unsigned char *>(v.trimmed().toUtf8().constData()), -1, -1, 0);
    };
    add("C", country.left(2).toUpper());
    add("O", org);
    add("CN", name);
    add("emailAddress", email);
    X509_set_issuer_name(cert, n);
    addExt(cert, NID_basic_constraints, "critical,CA:FALSE");
    addExt(cert, NID_key_usage, "critical,digitalSignature,nonRepudiation");
    addExt(cert, NID_subject_key_identifier, "hash");
    bool ok = X509_sign(cert, key, EVP_sha256()) > 0;
    PKCS12 *p12 = ok ? PKCS12_create(password.toUtf8().constData(), name.toUtf8().constData(), key, cert, nullptr, 0, 0, 0, 0, 0) : nullptr;
    QByteArray der;
    if (p12) {
        unsigned char *p = nullptr;
        int len = i2d_PKCS12(p12, &p);
        if (len > 0) der = QByteArray(reinterpret_cast<char *>(p), len);
        OPENSSL_free(p);
        PKCS12_free(p12);
    }
    X509_free(cert);
    EVP_PKEY_free(key);
    if (der.isEmpty()) {
        if (err) *err = sslError("could not create the digital ID");
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(der) != der.size()) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}

int cmsMaxSize(const DigitalId &id, bool withTimestamp)
{
    int n = 4096 + EVP_PKEY_get_size(static_cast<EVP_PKEY *>(id.key));
    n += i2d_X509(static_cast<X509 *>(id.cert), nullptr);
    STACK_OF(X509) *ca = static_cast<STACK_OF(X509) *>(id.chain);
    for (int i = 0; ca && i < sk_X509_num(ca); ++i) n += i2d_X509(sk_X509_value(ca, i), nullptr);
    if (withTimestamp) n += 16384;   // timestamp token incl. the TSA certificate chain
    return n;
}

static QByteArray httpPost(const QString &url, const QByteArray &body, QString *err)
{
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/timestamp-query"));
    req.setRawHeader("Accept", "application/timestamp-reply");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam.post(req, body);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(20000);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    QByteArray out;
    if (reply->error() != QNetworkReply::NoError) {
        if (err) *err = QStringLiteral("Timestamp server: %1").arg(reply->errorString());
    } else
        out = reply->readAll();
    delete reply;
    return out;
}

// RFC 3161 token over `imprint` (SHA-256 of the signature value); returns DER of the TimeStampToken.
static QByteArray fetchTimestamp(const QString &url, const unsigned char imprint[SHA256_DIGEST_LENGTH], QString *err)
{
    TS_REQ *req = TS_REQ_new();
    TS_REQ_set_version(req, 1);
    TS_MSG_IMPRINT *mi = TS_MSG_IMPRINT_new();
    X509_ALGOR *alg = X509_ALGOR_new();
    X509_ALGOR_set0(alg, OBJ_nid2obj(NID_sha256), V_ASN1_NULL, nullptr);
    TS_MSG_IMPRINT_set_algo(mi, alg);
    TS_MSG_IMPRINT_set_msg(mi, const_cast<unsigned char *>(imprint), SHA256_DIGEST_LENGTH);
    TS_REQ_set_msg_imprint(req, mi);
    X509_ALGOR_free(alg);
    TS_MSG_IMPRINT_free(mi);
    unsigned char nb[8];
    RAND_bytes(nb, sizeof nb);
    nb[0] &= 0x7f;
    BIGNUM *bn = BN_bin2bn(nb, sizeof nb, nullptr);
    ASN1_INTEGER *nonce = BN_to_ASN1_INTEGER(bn, nullptr);
    TS_REQ_set_nonce(req, nonce);
    ASN1_INTEGER_free(nonce);
    BN_free(bn);
    TS_REQ_set_cert_req(req, 1);
    unsigned char *p = nullptr;
    int len = i2d_TS_REQ(req, &p);
    QByteArray body(reinterpret_cast<char *>(p), len > 0 ? len : 0);
    OPENSSL_free(p);
    TS_REQ_free(req);

    QByteArray respBytes = httpPost(url, body, err);
    if (respBytes.isEmpty()) return QByteArray();
    const unsigned char *rp = reinterpret_cast<const unsigned char *>(respBytes.constData());
    TS_RESP *resp = d2i_TS_RESP(nullptr, &rp, respBytes.size());
    QByteArray token;
    if (resp) {
        long status = ASN1_INTEGER_get(TS_STATUS_INFO_get0_status(TS_RESP_get_status_info(resp)));
        PKCS7 *tok = TS_RESP_get_token(resp);
        TS_TST_INFO *tst = TS_RESP_get_tst_info(resp);
        bool imprintOk = false;
        if (tst) {
            ASN1_OCTET_STRING *m = TS_MSG_IMPRINT_get_msg(TS_TST_INFO_get_msg_imprint(tst));
            imprintOk = m && ASN1_STRING_length(m) == SHA256_DIGEST_LENGTH &&
                        !memcmp(ASN1_STRING_get0_data(m), imprint, SHA256_DIGEST_LENGTH);
        }
        if ((status == 0 || status == 1) && tok && imprintOk) {
            unsigned char *tp = nullptr;
            int tl = i2d_PKCS7(tok, &tp);
            if (tl > 0) token = QByteArray(reinterpret_cast<char *>(tp), tl);
            OPENSSL_free(tp);
        }
        TS_RESP_free(resp);
    }
    if (token.isEmpty() && err) *err = QStringLiteral("The timestamp server returned an invalid or rejected response");
    return token;
}

QByteArray cmsSign(const DigitalId &id, const QByteArray &data, const QString &tsaUrl, QString *err)
{
    initCrypto();
    unsigned int flags = CMS_DETACHED | CMS_BINARY | CMS_NOSMIMECAP | CMS_PARTIAL | CMS_CADES;
#ifdef CMS_NO_SIGNING_TIME
    flags |= CMS_NO_SIGNING_TIME;   // PAdES: the signing time lives in the signature dictionary (/M)
#endif
    BIO *in = BIO_new_mem_buf(data.constData(), int(data.size()));
    CMS_ContentInfo *cms = CMS_sign(nullptr, nullptr, static_cast<STACK_OF(X509) *>(id.chain), in, flags);
    QByteArray out;
    CMS_SignerInfo *si = cms ? CMS_add1_signer(cms, static_cast<X509 *>(id.cert), static_cast<EVP_PKEY *>(id.key), EVP_sha256(), flags) : nullptr;
    if (!si || CMS_final(cms, in, nullptr, flags) != 1) {
        if (err) *err = sslError("signing failed");
    } else {
        bool ok = true;
        if (!tsaUrl.trimmed().isEmpty()) {
            ASN1_OCTET_STRING *sig = CMS_SignerInfo_get0_signature(si);
            unsigned char h[SHA256_DIGEST_LENGTH];
            SHA256(ASN1_STRING_get0_data(sig), size_t(ASN1_STRING_length(sig)), h);
            QByteArray token = fetchTimestamp(tsaUrl.trimmed(), h, err);
            ok = !token.isEmpty() &&
                 CMS_unsigned_add1_attr_by_NID(si, NID_id_smime_aa_timeStampToken, V_ASN1_SEQUENCE, token.constData(), int(token.size())) == 1;
            if (!ok && err && err->isEmpty()) *err = sslError("could not embed the timestamp");
        }
        if (ok) {
            unsigned char *p = nullptr;
            int len = i2d_CMS_ContentInfo(cms, &p);
            if (len > 0) out = QByteArray(reinterpret_cast<char *>(p), len);
            OPENSSL_free(p);
        }
    }
    if (cms) CMS_ContentInfo_free(cms);
    BIO_free(in);
    return out;
}

static QDateTime tokenTime(const unsigned char *der, long len)
{
    QDateTime t;
    const unsigned char *p = der;
    if (PKCS7 *p7 = d2i_PKCS7(nullptr, &p, len)) {
        if (TS_TST_INFO *tst = PKCS7_to_TS_TST_INFO(p7)) {
            t = asn1Time(TS_TST_INFO_get_time(tst));
            TS_TST_INFO_free(tst);
        }
        PKCS7_free(p7);
    }
    ERR_clear_error();
    return t;
}

VerifyResult tsTokenInfo(const QByteArray &token)
{
    initCrypto();
    VerifyResult r;
    r.timestampTime = tokenTime(reinterpret_cast<const unsigned char *>(token.constData()), token.size());
    r.parsed = r.timestampTime.isValid();
    r.hasTimestamp = r.parsed;
    if (!r.parsed) r.error = QStringLiteral("Unreadable timestamp token");
    return r;
}

VerifyResult cmsVerify(const QByteArray &cmsDer, const QByteArray &data, const QDateTime &at, const QList<QByteArray> &extraTrusted)
{
    initCrypto();
    VerifyResult r;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(cmsDer.constData());
    CMS_ContentInfo *cms = d2i_CMS_ContentInfo(nullptr, &p, cmsDer.size());
    if (!cms) {
        r.error = QStringLiteral("The signature data cannot be read");
        ERR_clear_error();
        return r;
    }
    r.parsed = true;
    BIO *in = BIO_new_mem_buf(data.constData(), int(data.size()));
    r.digestOk = CMS_verify(cms, nullptr, nullptr, in, nullptr, CMS_BINARY | CMS_NO_SIGNER_CERT_VERIFY) == 1;
    if (!r.digestOk) r.error = QStringLiteral("The document was altered or the signature is corrupt");
    ERR_clear_error();
    BIO_free(in);

    STACK_OF(X509) *signers = CMS_get0_signers(cms);
    STACK_OF(X509) *certs = CMS_get1_certs(cms);
    X509 *signer = signers && sk_X509_num(signers) > 0 ? sk_X509_value(signers, 0)
                 : (certs && sk_X509_num(certs) > 0 ? sk_X509_value(certs, 0) : nullptr);
    if (signer) {
        r.signerCn = nameEntry(X509_get_subject_name(signer), NID_commonName);
        r.issuerCn = nameEntry(X509_get_issuer_name(signer), NID_commonName);
        r.notBefore = asn1Time(X509_get0_notBefore(signer));
        r.notAfter = asn1Time(X509_get0_notAfter(signer));
        r.certDer = certDer(signer);
        r.selfSigned = X509_check_issued(signer, signer) == X509_V_OK;

        X509_STORE *store = X509_STORE_new();
        auto addDer = [&](const QByteArray &der) {
            const unsigned char *q = reinterpret_cast<const unsigned char *>(der.constData());
            if (X509 *x = d2i_X509(nullptr, &q, der.size())) {
                X509_STORE_add_cert(store, x);
                X509_free(x);
            }
        };
        const QList<QSslCertificate> sys = QSslConfiguration::systemCaCertificates();
        for (const QSslCertificate &c : sys) addDer(c.toDer());
        for (const QByteArray &d : extraTrusted) addDer(d);
        X509_STORE_CTX *ctx = X509_STORE_CTX_new();
        X509_STORE_CTX_init(ctx, store, signer, certs);
        X509_VERIFY_PARAM *param = X509_STORE_CTX_get0_param(ctx);
        X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_PARTIAL_CHAIN);
        if (at.isValid()) X509_VERIFY_PARAM_set_time(param, time_t(at.toSecsSinceEpoch()));
        r.trusted = X509_verify_cert(ctx) == 1;
        if (!r.trusted) r.trustError = QString::fromLatin1(X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx)));
        X509_STORE_CTX_free(ctx);
        X509_STORE_free(store);
    } else
        r.trustError = QStringLiteral("no signer certificate embedded");

    STACK_OF(CMS_SignerInfo) *sis = CMS_get0_SignerInfos(cms);
    if (sis && sk_CMS_SignerInfo_num(sis) > 0) {
        CMS_SignerInfo *si = sk_CMS_SignerInfo_value(sis, 0);
        int idx = CMS_unsigned_get_attr_by_NID(si, NID_id_smime_aa_timeStampToken, -1);
        if (idx >= 0) {
            X509_ATTRIBUTE *attr = CMS_unsigned_get_attr(si, idx);
            ASN1_TYPE *t = X509_ATTRIBUTE_get0_type(attr, 0);
            if (t && t->type == V_ASN1_SEQUENCE) {
                r.timestampTime = tokenTime(t->value.sequence->data, t->value.sequence->length);
                r.hasTimestamp = r.timestampTime.isValid();
            }
        }
    }
    if (signers) sk_X509_free(signers);
    if (certs) sk_X509_pop_free(certs, X509_free);
    CMS_ContentInfo_free(cms);
    ERR_clear_error();
    return r;
}

} // namespace ink
