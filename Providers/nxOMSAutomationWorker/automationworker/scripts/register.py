#!/usr/bin/env python2
#
# Copyright (C) Microsoft Corporation, All rights reserved.

"""Script for registration against AzureAutomation agent service."""

import datetime as datetime
import hashlib
import base64
import json
import hmac
import socket
import argparse

import requests
import requests.packages.urllib3
from requests.packages.urllib3.exceptions import InsecureRequestWarning
from OpenSSL import crypto


class AgentService:
    def __init__(self, endpoint, account_key, cert_path, cert_key_path, worker_group_name, machine_id):
        self.protocol_version = "2.0"
        self.endpoint = endpoint
        self.account_key = account_key
        self.cert_path = cert_path
        self.cert_key_path = cert_key_path
        self.worker_group_name = worker_group_name
        self.machine_id = machine_id.lower()

        requests.packages.urllib3.disable_warnings(InsecureRequestWarning)  # disable ssl warnings
        requests.packages.urllib3.disable_warnings()  # disable insecure platform warning

    @staticmethod
    def encode_base64_sha256(key, msg):
        message = bytes(msg).encode('utf-8')
        secret = bytes(key).encode('utf-8')

        signature = base64.b64encode(hmac.new(secret, message, digestmod=hashlib.sha256).digest())
        return signature

    def compute_hmac(self, date, key, payload):
        sha256_hash = hashlib.sha256()
        sha256_hash.update(json.dumps(payload))
        encoded_payload = base64.b64encode(sha256_hash.digest())
        str_to_sign = encoded_payload + "\n" + date  # Based on AgentService contract

        signature = self.encode_base64_sha256(key, str_to_sign)
        return signature

    def register_worker(self):
        cert = crypto.load_certificate(crypto.FILETYPE_PEM, open(self.cert_path).read())
        cert_thumbprint = cert.digest("sha1").replace(":", "")

        date = datetime.datetime.utcnow().isoformat() + "0-00:00"
        payload = {'RunbookWorkerGroup': self.worker_group_name,
                   "MachineName": socket.gethostname(),
                   "IpAddress": socket.gethostbyname(socket.gethostname()),
                   "Thumbprint": cert_thumbprint,
                   "Issuer": str(cert.get_issuer()),
                   "Subject": str(cert.get_subject())}

        signature = self.compute_hmac(date, self.account_key, payload)

        header = {'Authorization': 'Shared ' + signature,
                  'ProtocolVersion': self.protocol_version,
                  'x-ms-date': date,
                  "Content-Type": "application/json"}

        url = "{0}/HybridV2(MachineId='{1}')".format(self.endpoint, self.machine_id)

        req = requests.put(url, cert=(self.cert_path, self.cert_key_path), verify=True, headers=header,
                           data=json.dumps(payload))

        if req.status_code is not 200:
            print "Agentservice : Failed to register worker. Status [{0}], Reason [{1}]".format(req.status_code,
                                                                                                req.reason)
            return

        print "Agentservice : Registration complete, status [" + str(req.status_code) + "]"
        print "Machine name : " + str(socket.gethostname())
        print "Machine id : " + str(self.machine_id)
        print "Worker group name : " + str(self.worker_group_name)
        print "Certificate thumbprint : " + str(cert_thumbprint)

    def deregister_worker(self):
        cert = crypto.load_certificate(crypto.FILETYPE_PEM, open(self.cert_path).read())
        cert_thumbprint = cert.digest("sha1").replace(":", "")

        date = datetime.datetime.utcnow().isoformat() + "0-00:00"
        payload = {"Thumbprint": cert_thumbprint,
                   "Issuer": str(cert.get_issuer()),
                   "Subject": str(cert.get_subject())}

        signature = self.compute_hmac(date, self.account_key, payload)

        header = {'Authorization': 'Shared ' + signature,
                  'ProtocolVersion': self.protocol_version,
                  'x-ms-date': date,
                  "Content-Type": "application/json"}

        url = "{0}/Hybrid(MachineId='{1}')".format(self.endpoint, self.machine_id)

        req = requests.delete(url, cert=(self.cert_path, self.cert_key_path), verify=True, headers=header,
                              data=json.dumps(payload))

        if req.status_code is not 200:
            print "Agent service : Failed to de-register worker. Status [{0}], Reason [{1}]".format(req.status_code,
                                                                                                    req.reason)
            return

        print "Agent service : De-registration complete, status [" + str(req.status_code) + "]"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-url', help='Registration url', required=True)
    parser.add_argument('-accountkey', help='Automation account key', required=True)
    parser.add_argument('-workergroupname', help='worker group name', required=True)
    parser.add_argument('-cert', help='Certificate file path', required=True)
    parser.add_argument('-key', help='Private key file path', required=True)
    parser.add_argument('-machineid', help='Machine id', required=True)

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--register', action='store_true', help='Register the worker')
    group.add_argument('--deregister', action='store_true', help='De-register the worker')
    args = parser.parse_args()

    agent = AgentService(args.url, args.accountkey, args.cert, args.key, args.workergroupname,
                         args.machineid)

    if args.register:
        agent.register_worker()
    elif args.deregister:
        agent.deregister_worker()


if __name__ == "__main__":
    main()
