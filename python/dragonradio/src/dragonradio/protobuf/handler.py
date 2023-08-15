# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for protobuf over UDP, TCP, and ZMQ"""
from abc import ABC, abstractmethod
from functools import wraps
import inspect
import logging
from pprint import pformat
import re
from typing import Any, Callable, ClassVar, Coroutine, Dict, Optional, Type, TypeVar
from typing_extensions import Concatenate, ParamSpec, Self

from google.protobuf.message import Message
from google.protobuf.json_format import MessageToDict

logger = logging.getLogger('protobuf')

class HandlerTable:
    """Protobuf message handler table"""
    message_types: Dict[str, int]
    """Map from message names to message numbers"""

    message_handlers: Dict[str, Callable[['ProtobufHandler', Message], Coroutine[Any, Any, None]]]
    """Map from message names to handlers"""

    def __init__(self):
        self.message_types = {}
        self.message_handlers = {}

class ProtobufHandler:
    """A class that handles protobuf messages"""
    handlers: ClassVar[Dict[str, HandlerTable]]
    """Map from message class name to handler table"""

    @classmethod
    def handle_protobuf(cls, msg_cls: Type[Message]):
        """Build a handler table for a protobuf message type.

        This function will search the class's methods for annotations indicating
        they handle the specified message type.
        """
        msg_cls_name: str = msg_cls.__name__
        table = HandlerTable()

        if not hasattr(cls, 'handlers'):
            cls.handlers = {}

        cls.handlers[msg_cls_name] = table

        for field in msg_cls.DESCRIPTOR.fields:
            table.message_types[field.name] = field.number

        for _, f in inspect.getmembers(cls, predicate=inspect.isfunction):
            if hasattr(f, 'message_name'):
                cname, fname = re.split(r'\.', getattr(f, 'message_name'))

                if cname == msg_cls_name:
                    if fname not in table.message_types:
                        raise ValueError(f"Illegal message type '{fname:}' for class {msg_cls_name:}")

                    table.message_handlers[fname] = f

    def find_handler(self, cls: Type[Message], msg: Message) -> Optional[Callable[[Self, Message], Coroutine[Any, Any, None]]]:
        """Find the handler associated with a protobuf message"""
        return self.handlers[cls.__name__].message_handlers.get(msg.WhichOneof('payload'))

    async def dispatch(self, cls: Type[Message], msg: Message):
        f = self.find_handler(cls, msg)
        if f is None:
            logger.error('Received unsupported message type: %s', msg)
        else:
            await f(self, msg)

H = TypeVar("H", bound=Type[ProtobufHandler])

def handler(message_type: Type[Message]) -> Callable[[H], H]:
    """Add automatic support for handling protobuf messages with a payload
    structure. Should be used to decorate a class handling protobuf messages.

    Args:
        cls (class): The protobuf message class handled
    """
    def decorator(cls: H) -> H:
        cls.handle_protobuf(message_type)
        return cls

    return decorator

def handle(name: str):
    """
    Indicate that a method handles a specific message. Should be used to
    decorate a method of a class that handles protobuf messages.

    Args:
        name (str): The name of the message the function handles.
    """
    def decorator(f):
        f.message_name = name
        return f

    return decorator

class ProtobufTransport(ABC):
    @abstractmethod
    async def send_msg(self, msg: Message):
        """Send a message"""
        pass

T = TypeVar("T", bound=ProtobufTransport)
M = TypeVar("M", bound=Message)
P = ParamSpec("P")

def send(cls: Type[M]):
    """
    Automatically add support to a function for constructing and sending a
    protobuf message. Should be used to decorate the methods of a
    ProtobufTransport subclass.

    Args:
        cls (class): The message class to send.
    """
    def decorator(f: Callable[Concatenate[T, M, P], Coroutine[Any, Any, None]]) -> Callable[Concatenate[T, P], Coroutine[Any, Any, None]]:
        @wraps(f)
        async def wrapper(self, *args: P.args, **kwargs: P.kwargs):
            msg = cls()

            await f(self, msg, *args, **kwargs)

            logger.debug('Sending message %s', pformat(MessageToDict(msg)))
            await self.send_msg(msg)
        return wrapper

    return decorator
