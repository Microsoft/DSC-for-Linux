#!/usr/bin/env python2

"""Script to generate certificate and key file."""

from OpenSSL import crypto
from socket import gethostname
import argparse


def generate_cert_and_key(cert_name, key_name):
    pub_key = crypto.PKey()
    pub_key.generate_key(crypto.TYPE_RSA, 1024)

    cert = crypto.X509()  # Self-signed
    cert.get_subject().C = "US"  # Country code
    cert.get_subject().ST = "WA"  # State
    cert.get_subject().L = "Redmond"  # Location
    cert.get_subject().O = "Microsoft"  # Org
    cert.get_subject().OU = "AzureAutomation"  # Org unit
    cert.get_subject().CN = gethostname()  # Common name
    cert.set_serial_number(666)
    cert.gmtime_adj_notBefore(0)  # Now
    cert.gmtime_adj_notAfter(10 * 365 * 24 * 60 * 60)  # 10 years from now
    cert.set_issuer(cert.get_subject())
    cert.set_pubkey(pub_key)
    cert.sign(pub_key, 'sha1')

    open(cert_name, "wt").write(crypto.dump_certificate(crypto.FILETYPE_PEM, cert))
    open(key_name, "wt").write(crypto.dump_privatekey(crypto.FILETYPE_PEM, pub_key))

    print "Certificate thumprint : " + str(cert.digest("sha1").replace(":", ""))
    print cert_name + " created"
    print key_name + " created"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-certfilename', help='Certificate file name', default="certificate.crt")
    parser.add_argument('-keyfilename', help='Private key file name', default="private.key")
    args = parser.parse_args()
    generate_cert_and_key(args.certfilename, args.keyfilename)

if __name__ == "__main__":
    main()