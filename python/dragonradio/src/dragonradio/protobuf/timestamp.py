# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for protobuf over UDP, TCP, and ZMQ"""
_PICOSEC_PER_SEC: float = 1e12

def setTimestamp(self, ts: float):
    """Set timestamp (sec)"""
    self.seconds = int(ts)
    self.picoseconds = int((ts % 1)*_PICOSEC_PER_SEC)

def getTimestamp(self) -> float:
    """Get timestamp (sec)"""
    return self.seconds + self.picoseconds/_PICOSEC_PER_SEC
