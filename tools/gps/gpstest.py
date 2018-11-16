#!/usr/bin/python3
import asyncio
import json
import time
import traceback

class GPSDClientProtocol:
    def __init__(self, loop=None, server_host=None, server_port=None):
        self.loop = loop
        self.server_host = server_host
        self.server_port = server_port
        self.writer = None

    def __del__(self):
        self.close()

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
        while True:
            try:
                await self.connect()
                await self.watch(True)

                while True:
                    raw = await self.reader.readline()
                    data = json.loads(raw.decode(encoding='utf-8'))
                    if data['class'] == 'TPV':
                        print()
                        for f in ['lat', 'lon', 'alt', 'time']:
                            if f in data:
                                print("%s: %s" % (f, data[f]))
            except Exception as e:
                print(traceback.format_exc())
                time.sleep(1)

def main():
    loop = asyncio.get_event_loop()
    client = GPSDClientProtocol(loop=loop, server_host='127.0.0.1', server_port=6000)
    try:
        loop.run_until_complete(client.run())
    finally:
        client.close()
        loop.close()

if __name__ == '__main__':
    main()
