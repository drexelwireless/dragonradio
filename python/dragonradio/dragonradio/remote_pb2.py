# -*- coding: utf-8 -*-
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: remote.proto
"""Generated protocol buffer code."""
from google.protobuf.internal import enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import message as _message
from google.protobuf import reflection as _reflection
from google.protobuf import symbol_database as _symbol_database
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x0cremote.proto\x12\x12\x64ragonradio.remote\"@\n\x06Status\x12(\n\x05state\x18\x01 \x01(\x0e\x32\x19.dragonradio.remote.State\x12\x0c\n\x04info\x18\x02 \x01(\t\"!\n\x10MandatedOutcomes\x12\r\n\x05goals\x18\x01 \x01(\t\"\"\n\x0b\x45nvironment\x12\x13\n\x0b\x65nvironment\x18\x01 \x01(\t\"\xeb\x01\n\x07Request\x12\x11\n\ttimestamp\x18\x01 \x01(\x01\x12\x39\n\rradio_command\x18\x02 \x01(\x0e\x32 .dragonradio.remote.RadioCommandH\x00\x12H\n\x18update_mandated_outcomes\x18\x03 \x01(\x0b\x32$.dragonradio.remote.MandatedOutcomesH\x00\x12=\n\x12update_environment\x18\x04 \x01(\x0b\x32\x1f.dragonradio.remote.EnvironmentH\x00\x42\t\n\x07payload\"C\n\x08Response\x12,\n\x06status\x18\x01 \x01(\x0b\x32\x1a.dragonradio.remote.StatusH\x00\x42\t\n\x07payload*[\n\x05State\x12\x07\n\x03OFF\x10\x00\x12\x0b\n\x07\x42OOTING\x10\x01\x12\t\n\x05READY\x10\x02\x12\n\n\x06\x41\x43TIVE\x10\x03\x12\x0c\n\x08STOPPING\x10\x04\x12\x0c\n\x08\x46INISHED\x10\x05\x12\t\n\x05\x45RROR\x10\x06*/\n\x0cRadioCommand\x12\t\n\x05START\x10\x00\x12\x08\n\x04STOP\x10\x01\x12\n\n\x06STATUS\x10\x02\x62\x06proto3')

_STATE = DESCRIPTOR.enum_types_by_name['State']
State = enum_type_wrapper.EnumTypeWrapper(_STATE)
_RADIOCOMMAND = DESCRIPTOR.enum_types_by_name['RadioCommand']
RadioCommand = enum_type_wrapper.EnumTypeWrapper(_RADIOCOMMAND)
OFF = 0
BOOTING = 1
READY = 2
ACTIVE = 3
STOPPING = 4
FINISHED = 5
ERROR = 6
START = 0
STOP = 1
STATUS = 2


_STATUS = DESCRIPTOR.message_types_by_name['Status']
_MANDATEDOUTCOMES = DESCRIPTOR.message_types_by_name['MandatedOutcomes']
_ENVIRONMENT = DESCRIPTOR.message_types_by_name['Environment']
_REQUEST = DESCRIPTOR.message_types_by_name['Request']
_RESPONSE = DESCRIPTOR.message_types_by_name['Response']
Status = _reflection.GeneratedProtocolMessageType('Status', (_message.Message,), {
  'DESCRIPTOR' : _STATUS,
  '__module__' : 'remote_pb2'
  # @@protoc_insertion_point(class_scope:dragonradio.remote.Status)
  })
_sym_db.RegisterMessage(Status)

MandatedOutcomes = _reflection.GeneratedProtocolMessageType('MandatedOutcomes', (_message.Message,), {
  'DESCRIPTOR' : _MANDATEDOUTCOMES,
  '__module__' : 'remote_pb2'
  # @@protoc_insertion_point(class_scope:dragonradio.remote.MandatedOutcomes)
  })
_sym_db.RegisterMessage(MandatedOutcomes)

Environment = _reflection.GeneratedProtocolMessageType('Environment', (_message.Message,), {
  'DESCRIPTOR' : _ENVIRONMENT,
  '__module__' : 'remote_pb2'
  # @@protoc_insertion_point(class_scope:dragonradio.remote.Environment)
  })
_sym_db.RegisterMessage(Environment)

Request = _reflection.GeneratedProtocolMessageType('Request', (_message.Message,), {
  'DESCRIPTOR' : _REQUEST,
  '__module__' : 'remote_pb2'
  # @@protoc_insertion_point(class_scope:dragonradio.remote.Request)
  })
_sym_db.RegisterMessage(Request)

Response = _reflection.GeneratedProtocolMessageType('Response', (_message.Message,), {
  'DESCRIPTOR' : _RESPONSE,
  '__module__' : 'remote_pb2'
  # @@protoc_insertion_point(class_scope:dragonradio.remote.Response)
  })
_sym_db.RegisterMessage(Response)

if _descriptor._USE_C_DESCRIPTORS == False:

  DESCRIPTOR._options = None
  _STATE._serialized_start=480
  _STATE._serialized_end=571
  _RADIOCOMMAND._serialized_start=573
  _RADIOCOMMAND._serialized_end=620
  _STATUS._serialized_start=36
  _STATUS._serialized_end=100
  _MANDATEDOUTCOMES._serialized_start=102
  _MANDATEDOUTCOMES._serialized_end=135
  _ENVIRONMENT._serialized_start=137
  _ENVIRONMENT._serialized_end=171
  _REQUEST._serialized_start=174
  _REQUEST._serialized_end=409
  _RESPONSE._serialized_start=411
  _RESPONSE._serialized_end=478
# @@protoc_insertion_point(module_scope)
