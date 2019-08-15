import asyncio
from concurrent.futures import CancelledError
import dateutil.parser
import json
import json.decoder
import logging
import time
import traceback

logger = logging.getLogger('gpsd')

DEFAULT_GPSD_SERVER = '127.0.0.1'
DEFAULT_GPSD_PORT = 6000

class GPSLocation(object):
    def __init__(self):
        self.lat = 0
        self.lon = 0
        self.alt = 0
        self.timestamp = 0

    def __str__(self):
        return 'GPSLocation(lat={},lon={},alt={},timestamp={})'.format(self.lat, self.lon, self.alt, self.timestamp)

class GPSDClient:
    def __init__(self, loc, loop=None, server_host=DEFAULT_GPSD_SERVER, server_port=DEFAULT_GPSD_PORT):
        self.loc = loc
        self.loop = loop
        self.server_host = server_host
        self.server_port = server_port
        self.reader = None
        self.writer = None

        loop.create_task(self.run())

    def close(self):
        if self.writer:
            self.writer.close()
            self.writer = None

    async def connect(self):
        self.reader, self.writer = await asyncio.open_connection(self.server_host,
                                                                 self.server_port,
                                                                 loop=self.loop)

    async def watch(self, enable):
        if enable:
            e = 'true'
        else:
            e = 'false'

        command = '?WATCH={{"enable":{0},"json":true}}'.format(e)

        self.writer.write(bytes(command, encoding='utf-8'))

    async def run(self):
        try:
            while True:
                try:
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
                    #logger.debug('Connection error')
                    await asyncio.sleep(1)
                except json.decoder.JSONDecodeError:
                    #logger.debug('JSON decoding error')
                    await asyncio.sleep(1)
                except CancelledError:
                    break
                except Exception as e:
                    logger.exception('Could not obtain GPS location')
                    break
        except CancelledError:
            return
