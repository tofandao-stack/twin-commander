"""Loopback-only FTP fixture. Implements only commands exercised by curl tests."""
import pathlib
import socket
import socketserver
import sys
import ssl

ROOT = pathlib.Path(sys.argv[1]).resolve()
TLS = None
if len(sys.argv) > 2:
    TLS = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    TLS.load_cert_chain(sys.argv[2], sys.argv[3])


class Handler(socketserver.StreamRequestHandler):
    def setup(self):
        if TLS: self.request = TLS.wrap_socket(self.request, server_side=True)
        super().setup()

    def handle(self):
        self.cwd = ROOT
        self.data = None
        self.send('220 test fixture')
        try:
            while True:
                line = self.rfile.readline(8192)
                if not line:
                    return
                command, _, argument = line.decode('utf-8').strip().partition(' ')
                command = command.upper()
                if command == 'USER': self.send('331 password required')
                elif command == 'PASS': self.send('230 logged in' if argument == 'secret' else '530 denied')
                elif command == 'SYST': self.send('215 UNIX Type: L8')
                elif command == 'FEAT': self.send('211 no extensions')
                elif command == 'PWD': self.send('257 "/' + str(self.cwd.relative_to(ROOT)).replace('.', '') + '"')
                elif command == 'TYPE': self.send('200 type set')
                elif command == 'CWD':
                    path = self.path(argument)
                    if path.is_dir(): self.cwd = path; self.send('250 directory changed')
                    else: self.send('550 no directory')
                elif command in ('EPSV', 'PASV'):
                    if self.data: self.data.close()
                    self.data = socket.socket()
                    self.data.bind(('127.0.0.1', 0))
                    self.data.listen(1)
                    port = self.data.getsockname()[1]
                    self.send(f'229 Entering Extended Passive Mode (|||{port}|)' if command == 'EPSV'
                              else f'227 Entering Passive Mode (127,0,0,1,{port // 256},{port % 256})')
                elif command == 'SIZE':
                    path = self.path(argument)
                    self.send(f'213 {path.stat().st_size}' if path.is_file() else '550 missing')
                elif command in ('NLST', 'LIST', 'RETR', 'STOR'):
                    if self.data is None: self.send('425 no data connection'); continue
                    if command == 'RETR' and not self.path(argument).is_file(): self.send('550 missing'); continue
                    self.send('150 opening data')
                    connection, _ = self.data.accept()
                    if TLS: connection = TLS.wrap_socket(connection, server_side=True)
                    with connection:
                        if command in ('NLST', 'LIST'):
                            names = sorted(p.name for p in self.cwd.iterdir()
                                           if argument == '-a' or not p.name.startswith('.'))
                            connection.sendall(('\r\n'.join(names) + '\r\n').encode())
                        elif command == 'RETR': connection.sendall(self.path(argument).read_bytes())
                        else:
                            with self.path(argument).open('wb') as output:
                                while True:
                                    data = connection.recv(65536)
                                    if not data: break
                                    output.write(data)
                        if TLS:
                            connection.settimeout(3)
                            try: connection.unwrap()
                            except (OSError, ValueError): pass
                    self.data.close(); self.data = None
                    self.send('226 complete')
                elif command == 'QUIT': self.send('221 goodbye'); return
                else: self.send('200 okay')
        except (OSError, ValueError):
            return
        finally:
            if self.data: self.data.close()

    def path(self, name):
        path = (ROOT / name.lstrip('/') if name.startswith('/') else self.cwd / name).resolve()
        path.relative_to(ROOT)
        return path

    def send(self, message):
        self.wfile.write((message + '\r\n').encode()); self.wfile.flush()


with socketserver.ThreadingTCPServer(('127.0.0.1', 0), Handler) as server:
    server.daemon_threads = True
    print(server.server_address[1], flush=True)
    server.serve_forever()
