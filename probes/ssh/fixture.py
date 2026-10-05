#!/usr/bin/env python3
"""Disposable SSH/SFTP fixture. Requires paramiko; never runs shell commands.

python fixture.py --directory /private/scratch [--bind 127.0.0.1] [--port 22222]
The directory receives fresh, test-only keys and matching/wrong known_hosts.
For hardware, bind to a runner LAN address and pass --host its advertised IPv4.
"""
import argparse
import io
import os
from pathlib import Path
import socket
import threading
import time

import paramiko


class Server(paramiko.ServerInterface):
    def __init__(self, key):
        self.key = key

    def get_allowed_auths(self, username):
        return "publickey"

    def check_auth_publickey(self, username, key):
        print("AUTH publickey", flush=True)
        if username == "fixture" and key == self.key:
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def check_channel_request(self, kind, channel_id):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_exec_request(self, channel, command):
        if command != b"printf 'Xbox SSH online\\n'; printf 'stderr-ok\\n' >&2; exit 7":
            return False

        def output():
            channel.sendall(b"Xbox SSH online\n")
            channel.sendall_stderr(b"stderr-ok\n")
            channel.send_exit_status(7)
            channel.shutdown_write()
            channel.close()

        threading.Thread(target=output, daemon=True).start()
        return True


class Handle(paramiko.SFTPHandle):
    def __init__(self, content, flags):
        super().__init__(flags)
        self.content = content

    def read(self, offset, length):
        self.content.seek(offset)
        return self.content.read(length)

    def write(self, offset, data):
        self.content.seek(offset)
        self.content.write(data)
        return paramiko.SFTP_OK


class Files(paramiko.SFTPServerInterface):
    def __init__(self, server, *args, **kwargs):
        super().__init__(server, *args, **kwargs)
        self.files = {}

    def open(self, path, flags, attr):
        if path != "probe.bin":
            return paramiko.SFTP_PERMISSION_DENIED
        if flags & os.O_CREAT:
            self.files.setdefault(path, io.BytesIO())
        if path not in self.files:
            return paramiko.SFTP_NO_SUCH_FILE
        if flags & os.O_TRUNC:
            self.files[path].truncate(0)
        return Handle(self.files[path], flags)

    def remove(self, path):
        if path not in self.files:
            return paramiko.SFTP_NO_SUCH_FILE
        del self.files[path]
        return paramiko.SFTP_OK


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=22222)
    args = parser.parse_args()
    os.umask(0o077)
    args.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    host_key = paramiko.RSAKey.generate(2048)
    user_key = paramiko.RSAKey.generate(2048)
    wrong_key = paramiko.RSAKey.generate(2048)
    user_key.write_private_key_file(str(args.directory / "id_rsa"))
    host_key.write_private_key_file(str(args.directory / "host_key"))
    for name, key in [("known_hosts", host_key), ("wrong_hosts", wrong_key)]:
        (args.directory / name).write_text(
            f"[{args.host}]:{args.port} {key.get_name()} {key.get_base64()}\n")
    (args.directory / "unknown_hosts").write_text("")

    def connection(client):
        transport = paramiko.Transport(client)
        try:
            transport.add_server_key(host_key)
            transport.set_subsystem_handler("sftp", paramiko.SFTPServer, Files)
            transport.start_server(server=Server(user_key))
            while transport.is_active():
                time.sleep(0.05)
        except (EOFError, OSError, paramiko.SSHException) as error:
            print(f"fixture connection ended: {type(error).__name__}", flush=True)
        finally:
            transport.close()

    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.bind, args.port))
        listener.listen()
        print(f"READY {args.host}:{args.port}", flush=True)
        while True:
            client, _ = listener.accept()
            threading.Thread(target=connection, args=(client,), daemon=True).start()


if __name__ == "__main__":
    main()
