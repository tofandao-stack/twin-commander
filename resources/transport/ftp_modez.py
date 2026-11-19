"""Bounded FTP MODE Z adapter. Input is JSON on stdin; no credentials in argv."""
import io, ftplib, ipaddress, json, os, socket, ssl, sys, time, urllib.parse, zlib
cfg = json.load(sys.stdin)
url = urllib.parse.urlsplit(cfg['url'])
deadline = time.monotonic() + 300
limit = 2 * 1024 * 1024 if cfg['method'] == 'list' else 4 * 1024**3
class ActiveMixin:
    def makeport(self):
        address = self.sock.getsockname()[0]
        ports = range(cfg['portFrom'], cfg['portTo'] + 1) if cfg['range'] else [0]
        listener = None
        for port in ports:
            candidate = socket.socket(self.af, socket.SOCK_STREAM)
            try:
                candidate.bind((address, port)); candidate.listen(1); listener = candidate; break
            except OSError:
                candidate.close()
        if listener is None: raise OSError('No active FTP port is available')
        advertised = cfg['address'] if cfg['external'] and cfg['address'] != '-' else address
        if self.af == socket.AF_INET: self.sendport(socket.gethostbyname(advertised), listener.getsockname()[1])
        else: self.sendeprt(advertised, listener.getsockname()[1])
        listener.settimeout(15)
        return listener
class Plain(ActiveMixin, ftplib.FTP): pass
class Secure(ActiveMixin, ftplib.FTP_TLS):
    def connect(self, host='', port=0, timeout=15, source_address=None):
        self.host = host; self.port = port or 990; self.timeout = timeout
        raw = socket.create_connection((host, self.port), timeout, source_address)
        self.af = raw.family; self.sock = self.context.wrap_socket(raw, server_hostname=host)
        self.file = self.sock.makefile('r', encoding=self.encoding)
        self.welcome = self.getresp()
        return self.welcome

def transfer():
    client = Secure(context=ssl.create_default_context(cafile=os.environ.get('CURL_CA_BUNDLE'))) if url.scheme == 'ftps' else Plain()
    try:
        client.connect(url.hostname, url.port or (990 if url.scheme == 'ftps' else 21), timeout=15)
        client.login(cfg['user'] or 'anonymous', cfg['password'] or ('anonymous@' if not cfg['user'] else ''))
        if isinstance(client, Secure): client.prot_p()
        client.set_pasv(cfg['passive'])
        peer = ipaddress.ip_address(client.sock.getpeername()[0])
        compressed = not (cfg['skipLAN'] and (peer.is_private or peer.is_loopback or peer.is_link_local))
        if compressed: client.voidcmd('MODE Z')
        path = urllib.parse.unquote(url.path)
        if '\r' in path or '\n' in path: raise ValueError('Invalid newline in remote path')
        if cfg['method'] == 'list':
            client.cwd(path or '/')
            command = 'NLST -a' if cfg['hidden'] else 'NLST'
        else: command = ('RETR ' if cfg['method'] == 'download' else 'STOR ') + path
        client.voidcmd('TYPE A' if cfg['ascii'] else 'TYPE I')
        connection = client.transfercmd(command)
        total = 0
        if cfg['method'] == 'upload':
            compressor = zlib.compressobj() if compressed else None
            carry = b''
            with os.fdopen(os.dup(cfg['fd']), 'rb') as source:
                while True:
                    block = source.read(65536)
                    if not block: break
                    total += len(block)
                    if total > limit or time.monotonic() > deadline: raise ValueError('Transfer size/time limit exceeded')
                    if cfg['ascii']:
                        block = carry + block; carry = b'\r' if block.endswith(b'\r') else b''
                        if carry: block = block[:-1]
                        block = block.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
                    connection.sendall(compressor.compress(block) if compressed else block)
                if carry: connection.sendall(compressor.compress(carry) if compressed else carry)
                if compressor: connection.sendall(compressor.flush())
        else:
            decompressor = zlib.decompressobj() if compressed else None
            output = io.BytesIO() if cfg['method'] == 'list' else os.fdopen(os.open(cfg['output'], os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600), 'wb')
            carry = b''
            try:
                while True:
                    block = connection.recv(65536)
                    if not block: break
                    if time.monotonic() > deadline: raise ValueError('Transfer time limit exceeded')
                    if decompressor:
                        block = decompressor.decompress(block, limit - total + 1)
                        if decompressor.unconsumed_tail: raise ValueError('Expanded MODE Z limit exceeded')
                    total += len(block)
                    if total > limit: raise ValueError('Expanded MODE Z limit exceeded')
                    if cfg['ascii'] and cfg['method'] != 'list':
                        block = carry + block; carry = b'\r' if block.endswith(b'\r') else b''
                        if carry: block = block[:-1]
                        block = block.replace(b'\r\n', b'\n')
                    output.write(block)
                if carry: output.write(carry)
                if decompressor and (not decompressor.eof or decompressor.unused_data): raise ValueError('Truncated or trailing MODE Z stream')
                output.flush()
            finally:
                if cfg['method'] != 'list': output.close()
        if isinstance(connection, ssl.SSLSocket): connection.unwrap()
        connection.close(); client.voidresp()
        if cfg['method'] == 'list': return output.getvalue()
    finally:
        client.close()

attempts = max(cfg['retries'], 1 if cfg['stall'] and cfg['method'] == 'download' else 0) if cfg['method'] != 'upload' else 0
for attempt in range(attempts + 1):
    try:
        listing = transfer()
        if listing is not None: sys.stdout.buffer.write(listing); sys.stdout.buffer.flush()
        break
    except Exception as error:
        if attempt >= attempts or time.monotonic() + cfg['delay'] >= deadline:
            print(str(error)[:8192], file=sys.stderr); sys.exit(1)
        time.sleep(cfg['delay'])
