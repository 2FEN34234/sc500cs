"""Verify certificate identity with the exact BearSSL sources bundled in Core 3.1.2.

This validates certificate logic on the host, not an ESP TLS socket handshake.
Only ephemeral public certificates are written; private test keys stay in memory.
"""
import datetime
import ipaddress
from pathlib import Path
import subprocess
import sys
import tempfile

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID


def main():
    executable = Path(sys.argv[1]).resolve()
    now = datetime.datetime.now(datetime.timezone.utc)
    day = datetime.timedelta(days=1)

    def ca(label):
        key = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, label)])
        cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
                .public_key(key.public_key()).serial_number(x509.random_serial_number())
                .not_valid_before(now - day).not_valid_after(now + day * 7)
                .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
                .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
                .sign(key, hashes.SHA256()))
        return key, cert

    root_key, root = ca('OFFLINE_TEST_CA')
    _, wrong_root = ca('OFFLINE_WRONG_CA')

    def leaf(name, san, expired=False):
        key = ec.generate_private_key(ec.SECP256R1())
        return (x509.CertificateBuilder()
                .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)]))
                .issuer_name(root.subject).public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(now - day * 2)
                .not_valid_after(now - day if expired else now + day)
                .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                .add_extension(x509.SubjectAlternativeName([san]), critical=False)
                .sign(root_key, hashes.SHA256()))

    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        certs = {'root': root, 'wrong_root': wrong_root,
                 'ip': leaf('101.43.33.186', x509.IPAddress(ipaddress.ip_address('101.43.33.186'))),
                 'dns': leaf('camera.test', x509.DNSName('camera.test')),
                 'expired': leaf('camera.test', x509.DNSName('camera.test'), expired=True)}
        for name, cert in certs.items():
            (folder / (name + '.pem')).write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        tests = [('IP_SAN', 'ip', '101.43.33.186', 'root', False),
                 ('DNS_SAN', 'dns', 'camera.test', 'root', True),
                 ('WRONG_HOST', 'dns', 'other.test', 'root', False),
                 ('WRONG_CA', 'dns', 'camera.test', 'wrong_root', False),
                 ('EXPIRED', 'expired', 'camera.test', 'root', False)]
        for label, cert, host, anchor, accepted in tests:
            run = subprocess.run([str(executable), 'verify', '-sni', host,
                                  '-CA', str(folder / (anchor + '.pem')),
                                  str(folder / (cert + '.pem'))], capture_output=True, text=True, timeout=10)
            output = (run.stdout + run.stderr).strip()
            assert (run.returncode == 0) == accepted, (label, run.returncode, output)
            print(f'{label}: {"ACCEPT" if accepted else "REJECT"} | {output}')
        print('PASS: bundled BearSSL requires DNS SAN for this configuration; wrong host/CA and expiry rejected')


if __name__ == '__main__':
    main()
