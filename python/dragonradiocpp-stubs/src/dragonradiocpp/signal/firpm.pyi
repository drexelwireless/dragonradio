from __future__ import annotations
import typing
__all__ = ['AFP', 'AFP_INVALID', 'AMPLITUDE_DISCONTINUITY', 'AMPLITUDE_VECTOR_MISMATCH', 'COEFFICIENT_SET_INVALID', 'CONVERGENCE_WARNING', 'EXCHANGE_FAILURE', 'FREQUENCY_INVALID_INTERVAL', 'PMOutput', 'SCALING', 'SCALING_INVALID', 'SUCCESS', 'Status', 'Strategy', 'UNIFORM', 'UNKNOWN_FAILURE', 'WEIGHT_DISCONTINUITY', 'WEIGHT_NEGATIVE', 'WEIGHT_VECTOR_MISMATCH', 'firpm', 'firpm1f', 'firpm1f2']
class PMOutput:
    def __repr__(self) -> str:
        ...
    @property
    def delta(self) -> float:
        """
        The final reference error
        """
    @property
    def h(self) -> list[float]:
        """
        Final filter coefficients
        """
    @property
    def iter(self) -> int:
        """
        Number of iterations that were necessary to achieve convergence
        """
    @property
    def q(self) -> Status:
        """
        status code for the output object
        """
    @property
    def x(self) -> list[float]:
        """
        Reference set used to generate the final filter
        """
class Status:
    """
    Members:
    
      SUCCESS
    
      FREQUENCY_INVALID_INTERVAL
    
      AMPLITUDE_VECTOR_MISMATCH
    
      AMPLITUDE_DISCONTINUITY
    
      WEIGHT_NEGATIVE
    
      WEIGHT_VECTOR_MISMATCH
    
      WEIGHT_DISCONTINUITY
    
      SCALING_INVALID
    
      AFP_INVALID
    
      COEFFICIENT_SET_INVALID
    
      EXCHANGE_FAILURE
    
      CONVERGENCE_WARNING
    
      UNKNOWN_FAILURE
    """
    AFP_INVALID: typing.ClassVar[Status]  # value = <Status.AFP_INVALID: 8>
    AMPLITUDE_DISCONTINUITY: typing.ClassVar[Status]  # value = <Status.AMPLITUDE_DISCONTINUITY: 3>
    AMPLITUDE_VECTOR_MISMATCH: typing.ClassVar[Status]  # value = <Status.AMPLITUDE_VECTOR_MISMATCH: 2>
    COEFFICIENT_SET_INVALID: typing.ClassVar[Status]  # value = <Status.COEFFICIENT_SET_INVALID: 9>
    CONVERGENCE_WARNING: typing.ClassVar[Status]  # value = <Status.CONVERGENCE_WARNING: 11>
    EXCHANGE_FAILURE: typing.ClassVar[Status]  # value = <Status.EXCHANGE_FAILURE: 10>
    FREQUENCY_INVALID_INTERVAL: typing.ClassVar[Status]  # value = <Status.FREQUENCY_INVALID_INTERVAL: 1>
    SCALING_INVALID: typing.ClassVar[Status]  # value = <Status.SCALING_INVALID: 7>
    SUCCESS: typing.ClassVar[Status]  # value = <Status.SUCCESS: 0>
    UNKNOWN_FAILURE: typing.ClassVar[Status]  # value = <Status.UNKNOWN_FAILURE: 12>
    WEIGHT_DISCONTINUITY: typing.ClassVar[Status]  # value = <Status.WEIGHT_DISCONTINUITY: 6>
    WEIGHT_NEGATIVE: typing.ClassVar[Status]  # value = <Status.WEIGHT_NEGATIVE: 4>
    WEIGHT_VECTOR_MISMATCH: typing.ClassVar[Status]  # value = <Status.WEIGHT_VECTOR_MISMATCH: 5>
    __members__: typing.ClassVar[dict[str, Status]]  # value = {'SUCCESS': <Status.SUCCESS: 0>, 'FREQUENCY_INVALID_INTERVAL': <Status.FREQUENCY_INVALID_INTERVAL: 1>, 'AMPLITUDE_VECTOR_MISMATCH': <Status.AMPLITUDE_VECTOR_MISMATCH: 2>, 'AMPLITUDE_DISCONTINUITY': <Status.AMPLITUDE_DISCONTINUITY: 3>, 'WEIGHT_NEGATIVE': <Status.WEIGHT_NEGATIVE: 4>, 'WEIGHT_VECTOR_MISMATCH': <Status.WEIGHT_VECTOR_MISMATCH: 5>, 'WEIGHT_DISCONTINUITY': <Status.WEIGHT_DISCONTINUITY: 6>, 'SCALING_INVALID': <Status.SCALING_INVALID: 7>, 'AFP_INVALID': <Status.AFP_INVALID: 8>, 'COEFFICIENT_SET_INVALID': <Status.COEFFICIENT_SET_INVALID: 9>, 'EXCHANGE_FAILURE': <Status.EXCHANGE_FAILURE: 10>, 'CONVERGENCE_WARNING': <Status.CONVERGENCE_WARNING: 11>, 'UNKNOWN_FAILURE': <Status.UNKNOWN_FAILURE: 12>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: int) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: int) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class Strategy:
    """
    Members:
    
      UNIFORM
    
      SCALING
    
      AFP
    """
    AFP: typing.ClassVar[Strategy]  # value = <Strategy.AFP: 2>
    SCALING: typing.ClassVar[Strategy]  # value = <Strategy.SCALING: 1>
    UNIFORM: typing.ClassVar[Strategy]  # value = <Strategy.UNIFORM: 0>
    __members__: typing.ClassVar[dict[str, Strategy]]  # value = {'UNIFORM': <Strategy.UNIFORM: 0>, 'SCALING': <Strategy.SCALING: 1>, 'AFP': <Strategy.AFP: 2>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    def __init__(self, value: int) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: int) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
def firpm(n: int, f: list[float], a: list[float], w: list[float], fs: float = 2.0, eps: float = 0.01, nmax: int = 4, strategy: Strategy = Strategy.UNIFORM, depth: int = 0, rstrategy: Strategy = Strategy.UNIFORM, prec: int = 165) -> PMOutput:
    """
    Use the Remez exchange algorithm to design an equiripple filter
    """
def firpm1f(n: int, f: list[float], a: list[float], w: list[float], fs: float = 2.0, eps: float = 0.01, nmax: int = 4, strategy: Strategy = Strategy.UNIFORM, depth: int = 0, rstrategy: Strategy = Strategy.UNIFORM, prec: int = 165) -> PMOutput:
    """
    Use the Remez exchange algorithm to design a filter with 1/f rolloff
    """
def firpm1f2(n: int, f: list[float], a: list[float], w: list[float], fs: float = 2.0, eps: float = 0.01, nmax: int = 4, strategy: Strategy = Strategy.UNIFORM, depth: int = 0, rstrategy: Strategy = Strategy.UNIFORM, prec: int = 165) -> PMOutput:
    """
    Use the Remez exchange algorithm to design a filter with 1/f^2 rolloff
    """
AFP: Strategy  # value = <Strategy.AFP: 2>
AFP_INVALID: Status  # value = <Status.AFP_INVALID: 8>
AMPLITUDE_DISCONTINUITY: Status  # value = <Status.AMPLITUDE_DISCONTINUITY: 3>
AMPLITUDE_VECTOR_MISMATCH: Status  # value = <Status.AMPLITUDE_VECTOR_MISMATCH: 2>
COEFFICIENT_SET_INVALID: Status  # value = <Status.COEFFICIENT_SET_INVALID: 9>
CONVERGENCE_WARNING: Status  # value = <Status.CONVERGENCE_WARNING: 11>
EXCHANGE_FAILURE: Status  # value = <Status.EXCHANGE_FAILURE: 10>
FREQUENCY_INVALID_INTERVAL: Status  # value = <Status.FREQUENCY_INVALID_INTERVAL: 1>
SCALING: Strategy  # value = <Strategy.SCALING: 1>
SCALING_INVALID: Status  # value = <Status.SCALING_INVALID: 7>
SUCCESS: Status  # value = <Status.SUCCESS: 0>
UNIFORM: Strategy  # value = <Strategy.UNIFORM: 0>
UNKNOWN_FAILURE: Status  # value = <Status.UNKNOWN_FAILURE: 12>
WEIGHT_DISCONTINUITY: Status  # value = <Status.WEIGHT_DISCONTINUITY: 6>
WEIGHT_NEGATIVE: Status  # value = <Status.WEIGHT_NEGATIVE: 4>
WEIGHT_VECTOR_MISMATCH: Status  # value = <Status.WEIGHT_VECTOR_MISMATCH: 5>
