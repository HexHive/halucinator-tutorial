#!/usr/bin/env python

import logging
import time

import sys
from halucinator.external_devices.external_device import HALucinatorZMQConn, HALucinatorExternalDevice

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


huart2 = 0x200000ec

def message_received(message):
    print(message)

def main():
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

        for line in sys.stdin:
            uart.send_line(huart2, line)
    except KeyboardInterrupt:
        pass
    time.sleep(5)
    log.info("Shutting Down")
    halzmq.shutdown()

if __name__ == '__main__':
    main()    
