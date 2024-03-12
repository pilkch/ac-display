#!/usr/bin/env python
#
# This is based on https://github.com/rsc-dev/pyproxy/
# We need to forward AC UDP messages from 127.0.0.1:9996 to <linux ip>:9997

__author__      = 'Radoslaw Matusiak'
__copyright__   = 'Copyright (c) 2016 Radoslaw Matusiak'
__license__     = 'MIT'
__version__     = '0.1'


"""
TCP/UDP proxy.
"""

import argparse
import signal
import logging
import select
import socket
import struct
from time import sleep

FORMAT = '%(asctime)-15s %(levelname)-10s %(message)s'
logging.basicConfig(format=FORMAT)
LOGGER = logging.getLogger()

LOCAL_DATA_HANDLER = lambda x:x
REMOTE_DATA_HANDLER = lambda x:x

BUFFER_SIZE = 2 ** 10  # 1024. Keep buffer size as power of 2.


def udp_proxy(src, dst):
    """Run UDP proxy.
    
    Arguments:
    src -- Source IP address and port string. I.e.: '127.0.0.1:8000'
    dst -- Destination IP address and port. I.e.: '127.0.0.1:8888'
    """
    LOGGER.debug('Starting UDP proxy...')
    LOGGER.debug('Src: {}'.format(src))
    LOGGER.debug('Dst: {}'.format(dst))
    
    src_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    src_socket.connect(ip_to_tuple(src))
    src_socket.setblocking(0)

    dst_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dst_socket.bind(ip_to_tuple(dst))
    dst_socket.setblocking(0)

    dst_address = ip_to_tuple(dst)

    listening_sockets = [src_socket, dst_socket]

    TIMEOUT_SECONDS = 1

    LOGGER.debug('Looping proxy (press Ctrl-Break to stop)...')
    while True:
        #LOGGER.debug('Calling select')
        notified_socket_list = select.select(listening_sockets, [], [], TIMEOUT_SECONDS)[0]
        for notified_socket in notified_socket_list:
            if (notified_socket == dst_socket):
                #LOGGER.debug('Reading from dst')
                try:
                    data, dst_address = dst_socket.recvfrom(BUFFER_SIZE)
                    if (data) :
                        #LOGGER.debug('Read from dst, writing to ' + str(src))
                        src_socket.send(data)
                except BlockingIOError:
                    LOGGER.debug('dst BlockingIOError')
                    pass
            elif (notified_socket == src_socket):
                #LOGGER.debug('Reading from src')
                try:
                    data = src_socket.recv(BUFFER_SIZE)
                    if (data) :
                        #LOGGER.debug('Read from src, writing to ' + str(dst_address))
                        dst_socket.sendto(data, dst_address)
                except BlockingIOError:
                    LOGGER.debug('src BlockingIOError')
                    pass

# end-of-function udp_proxy    

def ip_to_tuple(ip):
    """Parse IP string and return (ip, port) tuple.
    
    Arguments:
    ip -- IP address:port string. I.e.: '127.0.0.1:8000'.
    """
    ip, port = ip.split(':')
    return (ip, int(port))
# end-of-function ip_to_tuple


def main():
    """Main method."""
    parser = argparse.ArgumentParser(description='TCP/UPD proxy.')
    
    parser.add_argument('-d', '--dst', required=True, help='Destination IP and port, i.e.: 192.168.0.4:9997')
    
    output_group = parser.add_mutually_exclusive_group()
    output_group.add_argument('-q', '--quiet', action='store_true', help='Be quiet')
    output_group.add_argument('-v', '--verbose', action='store_true', help='Be loud')
    
    args = parser.parse_args()
    
    if args.quiet:
        LOGGER.setLevel(logging.CRITICAL)
    if args.verbose:
        LOGGER.setLevel(logging.NOTSET)
    
    udp_proxy("127.0.0.1:9996", args.dst)

# end-of-function main    


if __name__ == '__main__':
    main()
