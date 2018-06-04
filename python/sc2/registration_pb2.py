# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: registration.proto

import sys
_b=sys.version_info[0]<3 and (lambda x:x) or (lambda x:x.encode('latin1'))
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from google.protobuf import reflection as _reflection
from google.protobuf import symbol_database as _symbol_database
from google.protobuf import descriptor_pb2
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor.FileDescriptor(
  name='registration.proto',
  package='sc2.reg',
  syntax='proto3',
  serialized_pb=_b('\n\x12registration.proto\x12\x07sc2.reg\"\x8a\x01\n\x0cTalkToServer\x12%\n\x08register\x18\x01 \x01(\x0b\x32\x11.sc2.reg.RegisterH\x00\x12\'\n\tkeepalive\x18\x02 \x01(\x0b\x32\x12.sc2.reg.KeepaliveH\x00\x12\x1f\n\x05leave\x18\x03 \x01(\x0b\x32\x0e.sc2.reg.LeaveH\x00\x42\t\n\x07payload\"!\n\x08Register\x12\x15\n\rmy_ip_address\x18\x01 \x01(\r\"\x1d\n\tKeepalive\x12\x10\n\x08my_nonce\x18\x01 \x01(\x05\"\x19\n\x05Leave\x12\x10\n\x08my_nonce\x18\x01 \x01(\x05\"]\n\nTellClient\x12!\n\x06inform\x18\x01 \x01(\x0b\x32\x0f.sc2.reg.InformH\x00\x12!\n\x06notify\x18\x02 \x01(\x0b\x32\x0f.sc2.reg.NotifyH\x00\x42\t\n\x07payload\"L\n\x06Inform\x12\x14\n\x0c\x63lient_nonce\x18\x01 \x01(\x05\x12\x19\n\x11keepalive_seconds\x18\x02 \x01(\x02\x12\x11\n\tneighbors\x18\x03 \x03(\r\"\x1b\n\x06Notify\x12\x11\n\tneighbors\x18\x01 \x03(\rb\x06proto3')
)




_TALKTOSERVER = _descriptor.Descriptor(
  name='TalkToServer',
  full_name='sc2.reg.TalkToServer',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='register', full_name='sc2.reg.TalkToServer.register', index=0,
      number=1, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='keepalive', full_name='sc2.reg.TalkToServer.keepalive', index=1,
      number=2, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='leave', full_name='sc2.reg.TalkToServer.leave', index=2,
      number=3, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
    _descriptor.OneofDescriptor(
      name='payload', full_name='sc2.reg.TalkToServer.payload',
      index=0, containing_type=None, fields=[]),
  ],
  serialized_start=32,
  serialized_end=170,
)


_REGISTER = _descriptor.Descriptor(
  name='Register',
  full_name='sc2.reg.Register',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='my_ip_address', full_name='sc2.reg.Register.my_ip_address', index=0,
      number=1, type=13, cpp_type=3, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=172,
  serialized_end=205,
)


_KEEPALIVE = _descriptor.Descriptor(
  name='Keepalive',
  full_name='sc2.reg.Keepalive',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='my_nonce', full_name='sc2.reg.Keepalive.my_nonce', index=0,
      number=1, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=207,
  serialized_end=236,
)


_LEAVE = _descriptor.Descriptor(
  name='Leave',
  full_name='sc2.reg.Leave',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='my_nonce', full_name='sc2.reg.Leave.my_nonce', index=0,
      number=1, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=238,
  serialized_end=263,
)


_TELLCLIENT = _descriptor.Descriptor(
  name='TellClient',
  full_name='sc2.reg.TellClient',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='inform', full_name='sc2.reg.TellClient.inform', index=0,
      number=1, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='notify', full_name='sc2.reg.TellClient.notify', index=1,
      number=2, type=11, cpp_type=10, label=1,
      has_default_value=False, default_value=None,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
    _descriptor.OneofDescriptor(
      name='payload', full_name='sc2.reg.TellClient.payload',
      index=0, containing_type=None, fields=[]),
  ],
  serialized_start=265,
  serialized_end=358,
)


_INFORM = _descriptor.Descriptor(
  name='Inform',
  full_name='sc2.reg.Inform',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='client_nonce', full_name='sc2.reg.Inform.client_nonce', index=0,
      number=1, type=5, cpp_type=1, label=1,
      has_default_value=False, default_value=0,
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='keepalive_seconds', full_name='sc2.reg.Inform.keepalive_seconds', index=1,
      number=2, type=2, cpp_type=6, label=1,
      has_default_value=False, default_value=float(0),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
    _descriptor.FieldDescriptor(
      name='neighbors', full_name='sc2.reg.Inform.neighbors', index=2,
      number=3, type=13, cpp_type=3, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=360,
  serialized_end=436,
)


_NOTIFY = _descriptor.Descriptor(
  name='Notify',
  full_name='sc2.reg.Notify',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    _descriptor.FieldDescriptor(
      name='neighbors', full_name='sc2.reg.Notify.neighbors', index=0,
      number=1, type=13, cpp_type=3, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None, file=DESCRIPTOR),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  syntax='proto3',
  extension_ranges=[],
  oneofs=[
  ],
  serialized_start=438,
  serialized_end=465,
)

_TALKTOSERVER.fields_by_name['register'].message_type = _REGISTER
_TALKTOSERVER.fields_by_name['keepalive'].message_type = _KEEPALIVE
_TALKTOSERVER.fields_by_name['leave'].message_type = _LEAVE
_TALKTOSERVER.oneofs_by_name['payload'].fields.append(
  _TALKTOSERVER.fields_by_name['register'])
_TALKTOSERVER.fields_by_name['register'].containing_oneof = _TALKTOSERVER.oneofs_by_name['payload']
_TALKTOSERVER.oneofs_by_name['payload'].fields.append(
  _TALKTOSERVER.fields_by_name['keepalive'])
_TALKTOSERVER.fields_by_name['keepalive'].containing_oneof = _TALKTOSERVER.oneofs_by_name['payload']
_TALKTOSERVER.oneofs_by_name['payload'].fields.append(
  _TALKTOSERVER.fields_by_name['leave'])
_TALKTOSERVER.fields_by_name['leave'].containing_oneof = _TALKTOSERVER.oneofs_by_name['payload']
_TELLCLIENT.fields_by_name['inform'].message_type = _INFORM
_TELLCLIENT.fields_by_name['notify'].message_type = _NOTIFY
_TELLCLIENT.oneofs_by_name['payload'].fields.append(
  _TELLCLIENT.fields_by_name['inform'])
_TELLCLIENT.fields_by_name['inform'].containing_oneof = _TELLCLIENT.oneofs_by_name['payload']
_TELLCLIENT.oneofs_by_name['payload'].fields.append(
  _TELLCLIENT.fields_by_name['notify'])
_TELLCLIENT.fields_by_name['notify'].containing_oneof = _TELLCLIENT.oneofs_by_name['payload']
DESCRIPTOR.message_types_by_name['TalkToServer'] = _TALKTOSERVER
DESCRIPTOR.message_types_by_name['Register'] = _REGISTER
DESCRIPTOR.message_types_by_name['Keepalive'] = _KEEPALIVE
DESCRIPTOR.message_types_by_name['Leave'] = _LEAVE
DESCRIPTOR.message_types_by_name['TellClient'] = _TELLCLIENT
DESCRIPTOR.message_types_by_name['Inform'] = _INFORM
DESCRIPTOR.message_types_by_name['Notify'] = _NOTIFY
_sym_db.RegisterFileDescriptor(DESCRIPTOR)

TalkToServer = _reflection.GeneratedProtocolMessageType('TalkToServer', (_message.Message,), dict(
  DESCRIPTOR = _TALKTOSERVER,
  __module__ = 'registration_pb2'
  # @@protoc_insertion_point(class_scope:sc2.reg.TalkToServer)
  ))
_sym_db.RegisterMessage(TalkToServer)

Register = _reflection.GeneratedProtocolMessageType('Register', (_message.Message,), dict(
  DESCRIPTOR = _REGISTER,
  __module__ = 'registration_pb2'
  # @@protoc_insertion_point(class_scope:sc2.reg.Register)
  ))
_sym_db.RegisterMessage(Register)

Keepalive = _reflection.GeneratedProtocolMessageType('Keepalive', (_message.Message,), dict(
  DESCRIPTOR = _KEEPALIVE,
  __module__ = 'registration_pb2'
  # @@protoc_insertion_point(class_scope:sc2.reg.Keepalive)
  ))
_sym_db.RegisterMessage(Keepalive)

Leave = _reflection.GeneratedProtocolMessageType('Leave', (_message.Message,), dict(
  DESCRIPTOR = _LEAVE,
  __module__ = 'registration_pb2'
  # @@protoc_insertion_point(class_scope:sc2.reg.Leave)
  ))
_sym_db.RegisterMessage(Leave)

TellClient = _reflection.GeneratedProtocolMessageType('TellClient', (_message.Message,), dict(
  DESCRIPTOR = _TELLCLIENT,
  __module__ = 'registration_pb2'
  # @@protoc_insertion_point(class_scope:sc2.reg.TellClient)
  ))
_sym_db.RegisterMessage(TellClient)

Inform = _reflection.GeneratedProtocolMessageType('Inform', (_message.Message,), dict(
  DESCRIPTOR = _INFORM,
  __module__ = 'registration_pb2'
  # @@protoc_insertion_point(class_scope:sc2.reg.Inform)
  ))
_sym_db.RegisterMessage(Inform)

Notify = _reflection.GeneratedProtocolMessageType('Notify', (_message.Message,), dict(
  DESCRIPTOR = _NOTIFY,
  __module__ = 'registration_pb2'
  # @@protoc_insertion_point(class_scope:sc2.reg.Notify)
  ))
_sym_db.RegisterMessage(Notify)


# @@protoc_insertion_point(module_scope)