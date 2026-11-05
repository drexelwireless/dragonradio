#!/usr/bin/env python3
"""Application traffic for full-stack simulation over real tap interfaces (P2).

Runs inside one node's network namespace, alongside that node's radio, and
uses the kernel's UDP and TCP stacks over the radio's tap device:

  --udp-send DEST:PORT:PPS:SIZE   paced UDP datagrams (sequence, send time, hash)
  --udp-recv PORT                 verify, deduplicate and time received datagrams
  --tcp-server PORT               sink bulk streams and echo request/response
  --tcp-bulk DEST:PORT:BYTES      send BYTES of a seeded stream; the server
                                  returns its byte count and SHA-256
  --tcp-rr DEST:PORT:COUNT:SIZE   COUNT sequential request/response exchanges

All clients start at --start-at (host CLOCK_MONOTONIC, shared by every
namespace on the host) and stop sending after --duration; results are written
as JSON after --linger more seconds.
"""
import argparse
import hashlib
import json
import socket
import struct
import threading
import time

UDP_HDR = struct.Struct('!4sIQqI')   # magic, src port, seq, send ns, size
UDP_MAGIC = b'DRNU'
BULK_MAGIC = b'DRNB'
RR_MAGIC = b'DRNR'


def stream_bytes(seed, n):
    """Deterministic pseudo-random bytes (SHA-256 counter mode)."""
    out = bytearray()
    counter = 0
    while len(out) < n:
        out += hashlib.sha256(seed + counter.to_bytes(8, 'big')).digest()
        counter += 1
    return bytes(out[:n])


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('connection closed')
        buf += chunk
    return bytes(buf)


def connect_retry(dest, port, deadline):
    while True:
        try:
            return socket.create_connection((dest, port), timeout=5)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.2)


def percentile(xs, q):
    if not xs:
        return None
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(q*(len(xs) - 1))))]


class App:
    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.results = {'udp_send': [], 'udp_recv': {}, 'tcp_server': [], 'tcp_bulk': [], 'tcp_rr': [], 'errors': []}
        self.stop = threading.Event()
        # UDP latency clock: CLOCK_MONOTONIC (one host) or the wall clock (--wall-clock).
        self.stamp_ns = time.time_ns if getattr(args, 'wall_clock', False) else time.monotonic_ns
        self.results['udp_clock'] = 'wall' if getattr(args, 'wall_clock', False) else 'monotonic'

    def error(self, what, exc):
        with self.lock:
            self.results['errors'].append(f'{what}: {exc!r}')

    # UDP
    def udp_send(self, dest, port, pps, size):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sent = 0
        failures = 0
        t0 = self.args.start_at
        end = t0 + self.args.duration
        while True:
            t = t0 + sent/pps
            if t >= end:
                break
            delay = t - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            body = stream_bytes(b'udp%d:%d' % (port, sent), size - UDP_HDR.size - 32)
            hdr = UDP_HDR.pack(UDP_MAGIC, port, sent, self.stamp_ns(), size)
            try:
                sock.sendto(hdr + body + hashlib.sha256(hdr + body).digest(), (dest, port))
            except OSError:
                failures += 1
            sent += 1
        with self.lock:
            self.results['udp_send'].append({'dest': dest, 'port': port, 'pps': pps, 'size': size,
                                             'sent': sent, 'send_failures': failures})

    def udp_recv(self, port):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('0.0.0.0', port))
        sock.settimeout(0.2)
        seen = set()
        st = {'unique': 0, 'duplicates': 0, 'corrupt': 0, 'out_of_order': 0, 'bytes': 0,
              'latency': [], 'first_rx': None, 'last_rx': None, 'max_seq': -1}
        while not self.stop.is_set():
            try:
                data, _ = sock.recvfrom(65536)
            except socket.timeout:
                continue
            now = self.stamp_ns()
            if len(data) < UDP_HDR.size + 32:
                st['corrupt'] += 1
                continue
            magic, sport, seq, tx_ns, size = UDP_HDR.unpack_from(data)
            if magic != UDP_MAGIC or size != len(data) or hashlib.sha256(data[:-32]).digest() != data[-32:]:
                st['corrupt'] += 1
                continue
            if seq in seen:
                st['duplicates'] += 1
                continue
            seen.add(seq)
            if seq < st['max_seq']:
                st['out_of_order'] += 1
            st['max_seq'] = max(st['max_seq'], seq)
            st['unique'] += 1
            st['bytes'] += size
            st['latency'].append((now - tx_ns)*1e-9)
            st['first_rx'] = st['first_rx'] or now*1e-9
            st['last_rx'] = now*1e-9
        lat = st.pop('latency')
        st['seqs_missing_below_max'] = st['max_seq'] + 1 - st['unique']
        st['latency_p50'] = percentile(lat, 0.5)
        st['latency_p95'] = percentile(lat, 0.95)
        st['latency_max'] = max(lat) if lat else None
        with self.lock:
            self.results['udp_recv'][str(port)] = st

    # TCP server: first 4 bytes select bulk or request/response.
    def tcp_server(self, port):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(('0.0.0.0', port))
        srv.listen(8)
        srv.settimeout(0.2)
        while not self.stop.is_set():
            try:
                conn, peer = srv.accept()
            except socket.timeout:
                continue
            threading.Thread(target=self.tcp_serve, args=(conn, peer), daemon=True).start()

    def tcp_serve(self, conn, peer):
        rec = {'peer': peer[0]}
        try:
            conn.settimeout(None)
            magic = recv_exact(conn, 4)
            if magic == BULK_MAGIC:
                (expect,) = struct.unpack('!Q', recv_exact(conn, 8))
                h = hashlib.sha256()
                n = 0
                t_first = None
                while n < expect:
                    chunk = conn.recv(min(65536, expect - n))
                    if not chunk:
                        break
                    t_first = t_first or time.monotonic()
                    h.update(chunk)
                    n += len(chunk)
                t_last = time.monotonic()
                conn.sendall(struct.pack('!Q', n) + h.digest())
                rec.update(kind='bulk', expected=expect, received=n, first_byte=t_first, last_byte=t_last)
            elif magic == RR_MAGIC:
                count = 0
                while True:
                    hdr = conn.recv(4)
                    if not hdr:
                        break
                    hdr += recv_exact(conn, 4 - len(hdr))
                    (size,) = struct.unpack('!I', hdr)
                    conn.sendall(hdr + recv_exact(conn, size))
                    count += 1
                rec.update(kind='rr', exchanges=count)
            else:
                rec.update(kind='unknown')
        except Exception as exc:
            rec['error'] = repr(exc)
        finally:
            conn.close()
        with self.lock:
            self.results['tcp_server'].append(rec)

    def tcp_bulk(self, dest, port, nbytes):
        rec = {'dest': dest, 'port': port, 'bytes': nbytes}
        try:
            payload = stream_bytes(b'bulk%d' % port, nbytes)
            delay = self.args.start_at - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            t0 = time.monotonic()
            sock = connect_retry(dest, port, t0 + 30)
            rec['connect_s'] = time.monotonic() - t0
            sock.settimeout(self.args.duration + self.args.linger)
            sock.sendall(BULK_MAGIC + struct.pack('!Q', nbytes) + payload)
            rec['send_done_s'] = time.monotonic() - t0
            reply = recv_exact(sock, 8 + 32)
            rec['complete_s'] = time.monotonic() - t0
            (received,) = struct.unpack('!Q', reply[:8])
            rec['received'] = received
            rec['digest_ok'] = reply[8:] == hashlib.sha256(payload).digest()
            rec['goodput_kbps'] = 8*nbytes/rec['complete_s']/1e3
            sock.close()
        except Exception as exc:
            rec['error'] = repr(exc)
        with self.lock:
            self.results['tcp_bulk'].append(rec)

    def tcp_rr(self, dest, port, count, size):
        rec = {'dest': dest, 'port': port, 'count': count, 'size': size}
        rtts = []
        try:
            delay = self.args.start_at - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            sock = connect_retry(dest, port, time.monotonic() + 30)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.settimeout(30)
            sock.sendall(RR_MAGIC)
            bad = 0
            end = self.args.start_at + self.args.duration
            for i in range(count):
                if time.monotonic() > end:
                    break
                msg = stream_bytes(b'rr%d:%d' % (port, i), size)
                t = time.monotonic()
                sock.sendall(struct.pack('!I', size) + msg)
                reply = recv_exact(sock, 4 + size)
                rtts.append(time.monotonic() - t)
                bad += reply[4:] != msg
            sock.close()
            rec.update(completed=len(rtts), mismatched=bad,
                       rtt_p50=percentile(rtts, 0.5), rtt_p95=percentile(rtts, 0.95),
                       rtt_max=max(rtts) if rtts else None)
        except Exception as exc:
            rec.update(completed=len(rtts), error=repr(exc))
        with self.lock:
            self.results['tcp_rr'].append(rec)

    def run(self):
        a = self.args
        servers = [threading.Thread(target=self.udp_recv, args=(p,), daemon=True) for p in a.udp_recv]
        servers += [threading.Thread(target=self.tcp_server, args=(p,), daemon=True) for p in a.tcp_server]
        clients = [threading.Thread(target=self.udp_send, args=f) for f in a.udp_send]
        clients += [threading.Thread(target=self.tcp_bulk, args=f) for f in a.tcp_bulk]
        clients += [threading.Thread(target=self.tcp_rr, args=f) for f in a.tcp_rr]
        for t in servers + clients:
            t.start()
        end = a.start_at + a.duration + a.linger
        for t in clients:
            t.join(max(0.0, end - time.monotonic()))
        delay = end - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        self.stop.set()
        for t in servers:
            t.join(2)
        with self.lock:
            out = dict(self.results)
        out['unfinished_clients'] = sum(t.is_alive() for t in clients)
        with open(a.results, 'w') as f:
            json.dump(out, f, indent=1)


def split(conv):
    def parse(s):
        parts = s.split(':')
        if len(parts) != len(conv):
            raise argparse.ArgumentTypeError(f'expected {len(conv)} fields in {s!r}')
        return tuple(c(p) for c, p in zip(conv, parts))
    return parse


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--udp-send', action='append', default=[], type=split((str, int, float, int)))
    parser.add_argument('--udp-recv', action='append', default=[], type=int)
    parser.add_argument('--tcp-server', action='append', default=[], type=int)
    parser.add_argument('--tcp-bulk', action='append', default=[], type=split((str, int, int)))
    parser.add_argument('--tcp-rr', action='append', default=[], type=split((str, int, int, int)))
    parser.add_argument('--start-at', type=float, required=True, help='CLOCK_MONOTONIC start time')
    parser.add_argument('--wall-clock', action='store_true',
                        help='stamp UDP packets with the wall clock (CLOCK_REALTIME), for one-way latency between '
                             'hosts whose clocks are NTP-synchronized (over the air); default CLOCK_MONOTONIC, '
                             'valid only when every node runs on one host (simulation)')
    parser.add_argument('--duration', type=float, required=True)
    parser.add_argument('--linger', type=float, default=5.0)
    parser.add_argument('--results', required=True)
    App(parser.parse_args()).run()


if __name__ == '__main__':
    main()
