#!/usr/bin/env python3

import logging
import logging.config
import os
import random
import signal
import socket
import struct
import sys
import time

import zmq

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), '../python/sc2'))

import registration_pb2 as reg

from argparse import ArgumentParser
from argparse import ArgumentDefaultsHelpFormatter

LOG_LEVELS = {"DEBUG":logging.DEBUG,
              "INFO":logging.INFO,
              "WARNING":logging.WARNING,
              "ERROR":logging.ERROR,
              "CRITICAL":logging.CRITICAL}



def parse_args(argv):
    '''Handle command line options and return as dict'''

    if argv is None:
        argv = sys.argv
    else:
        sys.argv.extend(argv)


    # Setup argument parser
    parser = ArgumentParser( formatter_class=ArgumentDefaultsHelpFormatter)
    parser.add_argument("--server-ip", default="127.0.0.1", help="IP address of Collaboration Server")
    parser.add_argument("--server-port", default=5556, type=int, help="Port the server is listening on")
    parser.add_argument("--client-port", default=5557, type=int, help="Port the client listens to for messages from the server")
    parser.add_argument("--message-timeout", default=5.0, type=float, help="Timeout for messages sent to clients")
    parser.add_argument("--keepalive", default=30.0, type=float, help="Maximum value for client keepalive timer")
    parser.add_argument("--log-config-filename", default="collab_server_logging.conf",
                        help="Config file for logging module")

    # Process arguments
    args = vars(parser.parse_args())

    return args

def ip_int_to_string(ip_int):
    '''
    Convert integer formatted IP to IP string
    '''
    return socket.inet_ntoa(struct.pack('!L',ip_int))

class CollabServer(object):
    '''
    Top level object that runs the collaboration server
    '''

    def __init__(self, host="127.0.0.1", server_port=5556,
                 client_port=5557, keepalive=30.0, message_timeout=5.0,
                 log_config_filename="logging.conf"):

        # set up logging
        logging.config.fileConfig(log_config_filename)
        self.log = logging.getLogger("collab_server")

        # Store off args for later use
        self.host = host
        self.server_port = server_port
        self.client_port = client_port

        self.keepalive = float(keepalive)

        # initialize client tracking dict
        self.clients = {}

        # set up message handlers
        self.msg_handlers = {
                             "register":self.handle_register,
                             "keepalive":self.handle_keepalive,
                             "leave":self.handle_leave
                            }

        # This controls how long the server will try to send messages to a client endpoint before
        # throwing a warning and giving up
        self.message_timeout = float(message_timeout)

    def setup(self):
        '''
        Set up initial zeromq connections.

        The server needs to start up its main listener for incoming client registration messages
        and set up a poller to allow it to service all its client connections without blocking
        '''
        self.z_context = zmq.Context()

        self.listen_socket = self.z_context.socket(zmq.PULL)

        # initialize the main listening socket
        self.listen_socket.bind("tcp://%s:%s" % (self.host,self.server_port))
        self.log.info("Collaboration Server listening on host %s and port %i", 
                      self.host, self.server_port)

        # set up the poller
        self.poller = zmq.Poller()
        self.poller.register(self.listen_socket, zmq.POLLIN)


    def teardown(self):
        '''
        Close out zeroMQ connections and zeroMQ context cleanly
        '''

        self.log.debug("Shutting down sockets")

        # shut down main listening socket
        self.poller.unregister(self.listen_socket)
        self.listen_socket.close()

        # Clean up resources allocated to each client
        nonce_list = self.clients.keys()
        for nonce in nonce_list:
            self.cleanup_client(nonce)

        self.z_context.term()

        self.log.info("shutdown complete")

    def make_nonce(self):
        '''
        Make a unique random nonce for each client to make it more difficult for one client to
        inadvertently respond on behalf of another client
        '''
        return random.randint(0, 2**(31)-1)


    def send_with_timeout(self, sock, message, timeout):
        '''
        Try to send a message to a client with some timeout to prevent a single client from
        taking down the collaboration server for everyone
        '''
        tick = time.time()

        tock = time.time()

        success = False

        # check if a client socket is open and ready to accept a message. If the client socket
        # is ready, send the message. If we reach the timeout before a client socket appears to be
        # ready, give up on the message and log an error
        while tock-tick < timeout and success == False:
            
            self.log.debug("Trying to send message")
            socks = dict(self.poller.poll())
        
            if sock in socks and socks[sock] == zmq.POLLOUT:
                self.log.debug("Socket ready, sending")
                sock.send(message.SerializeToString())
                success = True
            else:
                self.log.warn("Tried to send message, endpoint is not connected. Retrying")
                time.sleep(1)
                tock=time.time()

        if not success:
            self.log.error("Could not send message after %f seconds", timeout)
        else:
            self.log.debug("Message sent")

        return

    def list_clients(self):
        '''
        Generate a list of active client IP addresses
        '''
        client_addresses = [val["ip_address"] for key, val in self.clients.items()]

        return client_addresses

    def add_client(self, ip):
        '''
        Set up the zeroMQ resources and client tracking structs for a new client. Also inform
        existing clients that a new peer has arrived
        '''
        self.log.debug("adding client")
        nonce = self.make_nonce()

        ip_address = ip_int_to_string(ip)

        self.log.debug("trying to connect to client at IP: %s and port %i",
                       ip_address, self.client_port)

        # create a socket for sending updates back to client
        client_socket = self.z_context.socket(zmq.PUSH)
        client_socket.connect("tcp://%s:%i" % (ip_address,self.client_port))

        # add socket to poller
        self.poller.register(client_socket, zmq.POLLOUT)

        # store off new client
        self.clients[nonce] = {"ip_address":ip,
                               "keepalive_counter":self.keepalive,
                               "socket":client_socket}


        self.log.debug("getting current list of clients")
        client_addresses = self.list_clients()
        client_address_strings = [ip_int_to_string(s) for s in client_addresses]

        self.log.debug("list of clients: %s",client_address_strings)

        # create an INFORM message for the new client
        message = reg.TellClient()
        message.inform.client_nonce = nonce
        message.inform.keepalive_seconds = self.keepalive
        message.inform.neighbors.extend(client_addresses)

        self.log.debug("sending inform message to client: %s", message)
        
        # send message INFORM message to new client
        self.send_with_timeout(client_socket, message, self.message_timeout)

        # notify existing clients of the new client
        self.send_notify_messages()
        return

    def cleanup_client(self, nonce):
        '''
        Clean up any resources allocated for the client with the given nonce
        '''
        # close socket to stale client
        client_socket = self.clients[nonce]["socket"]
        self.poller.unregister(client_socket)

        client_socket.setsockopt(zmq.LINGER, 0)
        client_socket.close()

        ip = self.clients[nonce]["ip_address"]
        ip_string = ip_int_to_string(ip)
        self.log.debug("Removing client with nonce %i and ip %s",
                       nonce, ip_string)

        # remove client from client list
        del self.clients[nonce]

    def handle_register(self, message):
        '''
        A new client sent in a register message. Add the client to the clients we are tracking
        and send any messages required to enroll the client
        '''

        ip = message.register.my_ip_address

        ip_string = ip_int_to_string(ip)

        self.log.info("Received Register message: IP address was %s", ip_string)

        self.add_client(ip)
        
        return

    def handle_keepalive(self, message):
        '''
        A client sent in a keepalive. Update the client tracking structs to refresh the client's
        keepalive timer
        '''

        nonce = message.keepalive.my_nonce
        client_ip_str = ip_int_to_string(self.clients[nonce]["ip_address"])
        self.log.info("Received Keepalive message with nonce %i, client IP %s. Resetting timer", 
                      nonce, client_ip_str)

        self.clients[nonce]["keepalive_counter"] = self.keepalive

        return

    def handle_leave(self, message):
        '''
        A client has informed us that it is leaving. Clean up any resources allocated to it and
        inform its peers that it is leaving
        '''
        nonce = message.leave.my_nonce

        client_ip_str = ip_int_to_string(self.clients[nonce]["ip_address"])

        self.log.info("Received Leave message with nonce %s, client IP %s", nonce, client_ip_str)
        self.cleanup_client(nonce)
        self.send_notify_messages()

        return

    def send_notify_messages(self):
        '''
        Inform all clients of the current state of the client list
        '''

        # construct a notify message
        message = reg.TellClient()
        message.notify.neighbors.extend(self.list_clients())

        self.log.debug("Sending notify message %s", message)

        # send a notify message to each client in the client list
        for nonce, client in self.clients.items():

            self.send_with_timeout(client["socket"], message, self.message_timeout)
            
            self.log.debug("message sent to client at ip %s",
                           ip_int_to_string(client["ip_address"]))

    def decrement_keepalives(self):
        '''
        decrease the keepalive counter for each client
        '''

        tock = time.time()
        # compute elapsed time by getting the current time and subtracting the last time that this
        # method ran
        elapsed_time = tock - self.tick

        # decrement each client's keepalive counter
        for nonce in self.clients:

            current_counter = self.clients[nonce]["keepalive_counter"]

            self.clients[nonce]["keepalive_counter"]= current_counter - elapsed_time

        # update the tick counter so we can compute the elasped time for the next iteration
        self.tick = tock


    def expire_stale_clients(self):
        '''
        Check each client for an expired keepalive counter. If any clients are expired, remove
        the expired clients from the tracking list and notify its peers that it has left
        '''
        expired_nonces = []

        # check each client for an expired timer
        for nonce, client in self.clients.items():
            if client["keepalive_counter"] < 0:
                self.log.debug("Client with nonce %i is expired", nonce)
                expired_nonces.append(nonce)

        # clean up resources allocated to each expired client
        for nonce in expired_nonces:
            self.cleanup_client(nonce)

        # send a notify message to each remaining client if any clients have expired
        if len(expired_nonces) > 0:
            self.send_notify_messages()

    def run(self):
        '''
        Run the server's event loop
        '''

        # initialize the tick counter used in keepalive tracking
        self.tick = time.time()

        while True:

            # decrement keepalive counters and check if clients are stale
            self.decrement_keepalives()
            self.expire_stale_clients()

            socks = dict(self.poller.poll())

            if self.listen_socket in socks and socks[self.listen_socket] == zmq.POLLIN:

                self.log.debug("processing message from client")
                raw_message = self.listen_socket.recv()


                message = reg.TalkToServer.FromString(raw_message)

                self.log.debug("message was %s", message)

                try:
                    handler = self.msg_handlers[message.WhichOneof("payload")]
                    handler(message)

                except KeyError as err:
                    self.log.error("received invalid message type %s", err)

            else:
                time.sleep(0.5)

def handle_sigterm(signal, frame):
    '''
    Catch SIGTERM and signal the script to exit gracefully
    '''
    raise KeyboardInterrupt

def main(argv=None):

    print("Collaboration Server starting, CTRL-C to exit")    

    # parse command line args
    args = parse_args(argv)       
   
    collab_server = CollabServer(host=args["server_ip"],
                                 server_port=args["server_port"],
                                 client_port=args["client_port"],
                                 keepalive=args["keepalive"],
                                 message_timeout=args["message_timeout"],
                                 log_config_filename=args["log_config_filename"])

    collab_server.setup()

    # have the collab server handle SIGTERM
    signal.signal(signal.SIGTERM, handle_sigterm)

    try:
        collab_server.run()
    except KeyboardInterrupt:
        print("interrupt received, stopping...")
        collab_server.teardown()

if __name__ == "__main__":

    main()

