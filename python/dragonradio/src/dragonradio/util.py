from pathlib import Path

import h5py # type: ignore
import pandas as pd
import numpy as np

def save_dataframe(df: pd.DataFrame, path: Path, key: str):
    """Save pandas DataFrame as HDF5"""
    # Convert DataFrame to contiguous numpy array
    arr = np.ascontiguousarray(df.to_numpy())

    with h5py.File(path, 'w') as f:
        # Write array to hdf dataset
        dset = f.create_dataset(key, arr.shape, arr.dtype)
        dset.write_direct(arr)

        # Add columns as an attribute
        dset.attrs['columns'] = df.columns.values

def load_dataframe(path: Path, key: str) -> pd.DataFrame:
    """Read pandas DataFrame from HDF5"""
    with h5py.File(path, 'r') as f:
        # Read DataSet from file
        dset = f[key]

        # Get column names from 'columns' attribute
        columns = dset.attrs['columns']

        # Create numpy array to hold data
        arr = np.empty(dset.shape, dtype=dset.dtype)

        # Read DataSet into array
        dset.read_direct(arr)

        # Create DataFrame from numpy array and columns
        return pd.DataFrame(arr, columns=columns)
