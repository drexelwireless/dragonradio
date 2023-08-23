# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from functools import wraps
import logging
import os
import pandas as pd
import pickle
from threading import RLock
import time
from weakref import WeakValueDictionary

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
    def __init__(self, key):
        self.key = key
        """Path to cached data"""

        self.attrname = None
        """Attribute name"""

        self.func = None
        """Function to compute DataFrame"""

        self.lock = RLock()
        """Lock"""

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
    def __init__(self, cache_path=None):
        self._cache_path = cache_path
        """Path to cache holding serialized DataFrames"""

        self._lock = RLock()
        """Lock on cache"""

        self._df_cache = WeakValueDictionary()
        """DataFrame cache"""

    @property
    def cache_path(self):
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
            self._df_cache[key] = df

            if self._cache_path is None:
                return

            parquet_path = os.path.join(self.cache_path, key + '.parquet')

            os.makedirs(os.path.dirname(parquet_path), exist_ok=True)

            with FileLock(parquet_path):
                df.to_parquet(parquet_path, engine=self.engine, index=True)
