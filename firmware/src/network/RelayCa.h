#pragma once

// Trust anchor for the InkAgent relay.
//
// relay.inkagent.dev is served by Caddy with a Let's Encrypt certificate. The
// whole served chain is ECDSA:
//
//   relay.inkagent.dev  <-  Let's Encrypt YE2  <-  ISRG Root YE  <-  ISRG Root X2
//
// So the anchor is ISRG Root X2, self-signed, P-384. Trusting it verifies the
// chain using the intermediates the server already sends.
//
// ISRG Root X1 was pinned here too and had to be removed. It is RSA-4096, it is
// only reachable via the X2 cross-certificate, and it is not needed: X2 alone
// validates the live chain. Carrying it made the device do RSA-4096 work during
// the handshake, where wolfSSL is built with FP_MAX_BITS 8192 and a small
// stack, so each fast-math temporary is ~2 KB — against a largest free block of
// around 39 KB once TLS is up. Pairing failed with wolfSSL -188
// (ASN_NO_SIGNER_E) as a result: no usable signer.
//
// Pin the root, never the leaf or the intermediate: leaves rotate every sixty
// days and Let's Encrypt rotates intermediates on its own schedule, so pinning
// either would brick the AI feature on a device nobody is updating.
//
// If Let's Encrypt stops chaining to an ISRG root, this needs updating in a
// release before the change lands. Check what is actually served with:
//   openssl s_client -connect relay.inkagent.dev:443 -showcerts
// and confirm the anchor still validates it:
//   openssl verify -CAfile x2.pem -untrusted <intermediates> <leaf>
//
// ISRG Root X2 expires 2040-09-17.

namespace inkagent {

inline constexpr char kRelayRootCAs[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"
    "CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"
    "R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"
    "MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"
    "ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"
    "EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"
    "+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"
    "ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"
    "AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"
    "zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"
    "tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"
    "/q4AaOeMSQ+2b1tbFfLn\n"
    "-----END CERTIFICATE-----\n";

}  // namespace inkagent
