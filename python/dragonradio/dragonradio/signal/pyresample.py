# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from typing import Optional

import numpy as np
from numpy.typing import ArrayLike

def fdupsample(U: int, D: int, h: Optional[ArrayLike], sig: ArrayLike, theta: float=0, P: int=128*3*25+1) -> np.ndarray:
    # Overlap factor
    V = 4

    # Length of FFT
    N = V*(P-1)

    # Number of new samples consumed per input block
    L = N-(P-1)

    # Validate rates
    if D != 1:
        raise ValueError("Decimation rate must be 1")

    if N % U != 0:
        raise ValueError(f"Decimation rate must divide FFT size, but {D:} does not divide {N:}")

    if theta*N % 1.0 != 0:
        raise ValueError(f"Frequency shift must consist of integer number of FFT bins, but {N*theta:} is not an integer")

    # Number of FFT bins to rotate
    Nrot = int(N*theta)

    # Perform mixing, convolution, and interpolation in frequency space
    off = 0
    upsampled = np.zeros(0)

    while off < len(sig):
        # Buffer source block
        if off == 0:
            # Handle first block by inserting zeros
            x = np.append(np.zeros((P-1)//U), sig[:L//U])
            off = (L-(P-1))//U
        else:
            x = sig[off:off+N//U]
            off += L//U

        # Perform FFT
        X = np.fft.fft(x, N//U)

        # Interpolate
        XU = np.zeros(N, dtype='complex128')

        XU[:N//U//2] = X[:N//U//2]
        XU[N-(N//U//2):] = X[N//U-(N//U//2):]

        # Mix by rotating FFT bins
        XU = np.roll(XU, Nrot)

        # Perform inverse FFT
        y = np.fft.ifft(XU, N)

        upsampled = np.append(upsampled, y[(P-1):N])

    # Compensate for interpolation
    upsampled *= U

    return upsampled

def fddownsample(U: int, D: int, h: ArrayLike, sig: ArrayLike, theta: float=0, P: int=128*3*25+1) -> np.ndarray:
    # Overlap factor
    V = 4

    # Length of FFT
    N = V*(P-1)

    # Number of new samples consumed per input block
    L = N-(P-1)

    # Validate rates
    if U != 1:
        raise ValueError("Interpolation rate must be 1")

    if N % D != 0:
        raise ValueError(f"Decimation rate must divide FFT size, but {D:} does not divide {N:}")

    if theta*N % 1.0 != 0:
        raise ValueError(f"Frequency shift must consist of integer number of FFT bins, but {N*theta:} is not an integer")

    # Compute frequency-space filter
    H = np.fft.fft(h, N)

    # Compensate for size difference between input FFT (N) and output FFT (D).
    H /= D

    # Number of FFT bins to rotate
    Nrot = int(N*theta)

    # Perform mixing, convolution, and decimation in frequency space
    off = 0
    downsampled = np.zeros(0)

    while off < len(sig):
        # Buffer source block
        if off == 0:
            # Handle first block by inserting zeros
            x = np.append(np.zeros(P-1), sig[:L])
            off = L-(P-1)
        else:
            x = sig[off:off+N]
            off += L

        # Perform FFT
        X = np.fft.fft(x, N)

        # Mix by rotating FFT bins
        X = np.roll(X, -Nrot)

        # Convolve
        X2 = X * H

        # Decimate
        XD = np.zeros(N//D, dtype='complex128')

        for i in range (0, D):
            XD += X2[i*N//D:(i+1)*N//D]

        # Perform inverse FFT
        y = np.fft.ifft(XD, N//D)

        downsampled = np.append(downsampled, y[(P-1)//D:N//D])

    # Correct for tail end of signal
    delay = (len(h)-1)//2

    return downsampled[delay//D:(delay+len(sig)*U)//D]

def fdresample(U: int, D: int, h: Optional[np.ndarray], sig: np.ndarray, theta: float=0, P: int=2**7*3*5**3*7+1):
    # Overlap factor
    V = 4

    # Length of FFT
    N = V*(P-1)

    # Number of new samples consumed per input block
    L = N-(P-1)

    # Validate rates
    if N % U != 0:
        raise ValueError(f"Interpolation rate must divide FFT size, but {U:} does not divide {N:}")

    if N % D != 0:
        raise ValueError(f"Decimation rate must divide FFT size, but {D:} does not divide {N:}")

    # Compute and validate number of frequency bins to rotate
    Nrot = N*theta

    if U/D < 1.0:
        Nrot /= U
    elif U/D > 1.0:
        Nrot /= D

    Nrot_int = round(Nrot)
    if abs(Nrot - Nrot_int) > 1e-5:
        raise ValueError(f"Frequency shift must consist of integer number of FFT bins, but {Nrot:} is not an integer")

    Nrot = Nrot_int

    # Compute compensation factor. We compensate for interpolation by
    # multiplying by U and compensate for repeating the signal D times during
    # downsampling by dividing by D.
    k = U/D

    # Compute frequency-space filter
    if h is not None:
        H = k*np.fft.fft(h, N)
    else:
        H = None

    # Perform mixing, convolution, and interpolation in frequency space
    off = 0
    resampled = np.zeros(0)

    while off < len(sig):
        # Buffer source block
        if off == 0:
            # Handle first block by inserting zeros
            x = np.append(np.zeros((P-1)//U), sig[:L//U])
            off = (L-(P-1))//U
        else:
            x = sig[off:off+N//U]
            off += L//U

        # Perform FFT
        X = np.fft.fft(x, N//U)

        if U/D < 1.0:
            X = np.roll(X, -Nrot)

        # Interpolate
        XU = np.zeros(N, dtype='complex128')

        if h is not None:
            # Duplicate
            l = N//U
            l2 = l//2

            for i in range(0, U):
                XU[i*l:i*l+l2] = X[:l2]
                XU[N-i*l-l2:N-i*l] = X[l2:]

            # Convolve
            XU = XU*H
        else:
            XU[:N//U//2] = X[:N//U//2]
            XU[N-(N//U//2):] = X[N//U-(N//U//2):]

        # Decimate
        XD = np.zeros(N//D, dtype='complex128')

        for i in range (0, D):
            XD += XU[i*N//D:(i+1)*N//D]

        # Mix up by rotating FFT bins
        if U/D > 1.0:
            XD = np.roll(XD, Nrot)

        # Perform inverse FFT
        y = np.fft.ifft(XD, N//D)

        resampled = np.append(resampled, y[(P-1)//D:N//D])

    # Compensate for resampling if we didn't filter
    if H is None:
        resampled *= k

    # Correct for filter delay and tail end of signal
    if h is None:
        delay = 0
    else:
        delay = (len(h)-1)//2

    return resampled[delay//D:(delay+len(sig)*U)//D]