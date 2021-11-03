#!/usr/bin/env python

import base64
import logging
import random
import string
import time
import sys
from halucinator.external_devices.external_device import HALucinatorZMQConn, HALucinatorExternalDevice
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, NoEncryption
from OpenSSL import crypto, SSL

log = logging.getLogger("UartPeripheral")
log.setLevel(logging.DEBUG)


class UartPeripheral(HALucinatorExternalDevice):

    def __init__(self, zmqconn, received_callback=None):
        super(UartPeripheral, self).__init__('UARTPublisher', zmqconn)
        self.pending = ''
        self.recv_callback = received_callback
        self.register_handler('write', self.write_handler)

    """
    Write Handler receives events sent over ZeroMQ from HALucinator
    """
    def write_handler(self, ioserver, msg):
        if msg['chars'][-1:] != b'\n':
            self.pending += msg['chars'].decode("utf-8").strip("\r\n")
        else:
            if self.pending == '':
                self.pending = msg['chars'].decode("utf-8").strip("\r\n")
            self.recv_callback(self.pending)
            self.pending = ''

    """
    send_line sends a command over ZeroMQ to over ZeroMQ to the Uart Device.
    """
    def send_line(self, id, chars):
        chars = chars.strip("\n")
        chars += "\r"
        log.debug("SENDLINE %s", chars.encode("utf-8"))
        for char in chars:
            d = {'id': id, 'chars': char}
            self.send_message('rx_data', d)

"""
This function takes some of the usual properties you would expect 
of a certificate. 
It returns a tuple: DER-encoded certificate, DER-encoded private key.

Note: probably this should be done with cryptography and not 
pyopenssl.
"""
def selfsigned_certificate(emailAddress="emailAddress",
             commonName="commonName",
             countryName="NT",
             localityName="localityName",
             stateOrProvinceName="stateOrProvinceName",
             organizationName="organizationName",
             organizationUnitName="organizationUnitName",
             serialNumber=0,
             validityStartInSeconds=0,
             validityEndInSeconds=10*365*24*60*60):

    # For small keys and fast generation, lets do this 
    # using P-256:
    q = ec.generate_private_key(ec.SECP256R1)
    q_der = q.private_bytes(encoding=Encoding.DER, format=PrivateFormat.TraditionalOpenSSL, encryption_algorithm=NoEncryption())
    k = crypto.load_privatekey(crypto.FILETYPE_ASN1, q_der)

    cert = crypto.X509()
    cert.get_subject().C = countryName
    cert.get_subject().ST = stateOrProvinceName
    cert.get_subject().L = localityName
    cert.get_subject().O = organizationName
    cert.get_subject().OU = organizationUnitName
    cert.get_subject().CN = commonName
    cert.get_subject().emailAddress = emailAddress
    cert.set_serial_number(serialNumber)
    cert.gmtime_adj_notBefore(0)
    cert.gmtime_adj_notAfter(validityEndInSeconds)
    cert.set_issuer(cert.get_subject())
    cert.set_pubkey(k)
    cert.sign(k, 'sha512')

    return (crypto.dump_certificate(crypto.FILETYPE_ASN1, cert), q_der)

def random_string(length):
    return ''.join(random.SystemRandom().choice(string.ascii_uppercase + string.digits) for _ in range(length))

huart2 = 0x200000ec

reply_received = False

def message_received(message):
    global reply_received
    print(message)
    if len(message) > 0:
        reply_received = True

def main():
    global reply_received
    logging.basicConfig()

    halzmq = HALucinatorZMQConn(5556, 5555) 
    uart = UartPeripheral(halzmq, received_callback=message_received)

    halzmq.start()
    uart.wait()

    try:
        print("Sending login, then admin, then password, waiting 1 second between each.")
        time.sleep(1)
        uart.send_line(huart2, "login")
        time.sleep(1)
        uart.send_line(huart2, "admin")
        time.sleep(1)
        uart.send_line(huart2, "123456")
        time.sleep(1)
        print("Done")

        for i in range(0, 100):

            candidatecert, candidatekey = selfsigned_certificate(countryName='CH', 
                                                                 commonName=random_string(20),
                                                                 localityName=random_string(12))

            cert = base64.b64encode(candidatecert).decode("utf-8")

            uart.send_line(huart2, "cert")
            time.sleep(1)

            reply_received = False
            uart.send_line(huart2, cert)
            time.sleep(1)
            if reply_received == False:
                # looks like we killed the firmware!
                print("BUG FOUND!")
                return
            else:
                continue

    except KeyboardInterrupt:
        pass
    time.sleep(5)
    log.info("Shutting Down")
    halzmq.shutdown()

if __name__ == '__main__':
    main()    
