# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import logging
import os
import pandas as pd
from pathlib import Path
from threading import RLock
import time
from typing import Callable, Optional
from weakref import WeakValueDictionary
import xdg

logger = logging.getLogger(__name__)

class FileLock(object):
    def __init__(self, path):
        self._lockpath = path + '.lock'
        """Path of lock file"""

        self._lock = None
        """Lock file descriptor"""

    def __enter__(self):
        while True:
            try:
                self._lock = os.open(self._lockpath, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                break
            except FileExistsError:
                time.sleep(1)

        return self._lock

    def __exit__(self, _type, _value, _traceback):
        os.close(self._lock)
        os.remove(self._lockpath)

class cached_dataframe_property:
    key: str
    """Path to cached data"""

    attrname: Optional[str] = None
    """Attribute name"""

    func: Callable[['DataFrameCache'], Optional[pd.DataFrame]]
    """Function to compute DataFrame"""

    lock: RLock
    """Lock"""

    def __init__(self, key: str):
        self.key = key
        self.lock = RLock()

    def __call__(self, func):
        self.func = func
        self.__doc__ = func.__doc__
        return self

    def __set_name__(self, owner, name):
        if self.attrname is None:
            self.attrname = name
        elif name != self.attrname:
            raise TypeError(
                "Cannot assign the same cached_dataframe_property "
                "to two different names "
                f"({self.attrname!r} and {name!r})."
            )

    def __get__(self, instance, owner=None):
        if instance is None:
            return self

        if self.attrname is None:
            raise TypeError(
                "Cannot use cached_dataframe_property instance "
                "without calling __set_name__ on it.")

        df = instance.get_dataframe(self.key)
        if df is not None:
            return df

        df = self.func(instance)

        instance.set_dataframe(self.key, df)

        return df

class DataFrameCache:
    _cache_path: Path
    """Path to cache holding serialized DataFrames"""

    _lock: RLock
    """Lock on cache"""

    _df_cache: WeakValueDictionary
    """DataFrame cache"""

    def __init__(self, cache_path: Optional[Path]=None):
        if cache_path is None:
            self._cache_path = xdg.xdg_cache_home() / 'dragonradio' / 'dataframe'
        else:
            self._cache_path = Path(cache_path)

        self._lock = RLock()
        self._df_cache = WeakValueDictionary()

    @property
    def cache_path(self) -> Path:
        """Path to cache holding serialized DataFrames"""
        return self._cache_path

    #engine='pyarrow'
    # We need fastparquet for time deltas. See:
    #   https://pandas.pydata.org/pandas-docs/stable/io.html#parquet
    engine='fastparquet'

    def get_dataframe(self, key):
        with self._lock:
            # Try DataFrame cache
            if key in self._df_cache:
                return self._df_cache[key]

            if self._cache_path is None:
                return None

            # Now look in the file system cache
            parquet_path = os.path.join(self.cache_path, key + '.parquet')

            os.makedirs(os.path.dirname(parquet_path), exist_ok=True)

            with FileLock(parquet_path):
                if not os.path.isfile(parquet_path):
                    return None

                df = pd.read_parquet(parquet_path, engine=self.engine)

                self._df_cache[key] = df
                return df

    def set_dataframe(self, key, df):
        with self._lock:
            if df is None:
                return

            self._df_cache[key] = df

            if self._cache_path is None:
                return

            parquet_path = os.path.join(self.cache_path, key + '.parquet')

            os.makedirs(os.path.dirname(parquet_path), exist_ok=True)

            with FileLock(parquet_path):
                df.to_parquet(parquet_path, engine=self.engine, index=True)
