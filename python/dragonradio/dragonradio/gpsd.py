# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for communicating with gpsd using asyncio"""
import asyncio
import json
import json.decoder
import logging
import time

import dateutil.parser

logger = logging.getLogger('gpsd')

DEFAULT_GPSD_SERVER = '127.0.0.1'
DEFAULT_GPSD_PORT = 6000

class GPSDClient:
    """A client for communicating with gpsd"""
    def __init__(self, loc,
                 loop=None,
                 server_host=DEFAULT_GPSD_SERVER,
                 server_port=DEFAULT_GPSD_PORT):
        self.loc = loc
        self.loop = loop
        self.server_host = server_host
        self.server_port = server_port
        self.reader = None
        self.writer = None

        self.gpsd_task = loop.create_task(self.run())

    async def stop(self):
        """Stop gpsd reader"""
        self.gpsd_task.cancel()

        await self.gpsd_task

        if self.writer:
            self.writer.close()
            self.writer = None

    async def connect(self):
        """Connect to gpsd"""
        self.reader, self.writer = await asyncio.open_connection(self.server_host,
                                                                 self.server_port,
                                                                 loop=self.loop)

    async def watch(self, enable):
        """Set whether or not to watch GPS info"""
        if enable:
            e = 'true'
        else:
            e = 'false'

        command = '?WATCH={{"enable":{0},"json":true}}'.format(e)

        self.writer.write(bytes(command, encoding='utf-8'))

    async def run(self):
        """Run gpsd listener"""
        wait_to_connect = False

        while True:
            try:
                if wait_to_connect:
                    await asyncio.sleep(1)
                    wait_to_connect = False

                await self.connect()
                await self.watch(True)

                while True:
                    raw = await self.reader.readline()
                    data = json.loads(raw.decode(encoding='utf-8'))
                    if data['class'] == 'TPV':
                        t = time.time()

                        if 'time' in data:
                            dt = dateutil.parser.parse(data['time'])
                            t = time.mktime(dt.timetuple())
                        else:
                            t = time.time()

                        for attr in ['lat', 'lon', 'alt']:
                            if attr in data:
                                setattr(self.loc, attr, data[attr])
                                self.loc.timestamp = t
            except ConnectionError:
                wait_to_connect = True
            except json.decoder.JSONDecodeError:
                wait_to_connect = True
            except asyncio.CancelledError:
                return
            except: # pylint: disable=bare-except
                logger.exception('Could not obtain GPS location')
                break
