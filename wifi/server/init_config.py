"""Generate local deployment credentials, never print or overwrite them."""
import os
from pathlib import Path
import secrets
import argparse
import ipaddress
import re

parser = argparse.ArgumentParser(description='Generate private server config with a DNS TLS identity.')
parser.add_argument('--host', required=True, help='DNS hostname resolving to 101.43.33.186; not an IP or URL')
args = parser.parse_args()
host = args.host.lower()
try:
    ipaddress.ip_address(host)
except ValueError:
    pass
else:
    parser.error('ESP8266 Core 3.1.2 requires a matching DNS certificate identity, not a numeric IP.')
if (len(host) > 253 or '.' not in host or
        any(not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', label) for label in host.split('.')) or
        host.endswith(('.invalid', '.example', '.test', '.localhost'))):
    parser.error('Provide a real DNS hostname, without scheme, path, port or placeholder suffix.')

target = Path(__file__).with_name('.env')
text = (
    f'SITE_ADDRESS={host}\n'
    f'CAMERA_DEVICE_ID={secrets.token_hex(16)}\n'
    f'CAMERA_DEVICE_TOKEN={secrets.token_hex(32)}\n'
    'CAMERA_ADMIN_USER=admin\n'
    f'CAMERA_ADMIN_PASSWORD={secrets.token_hex(24)}\n'
    'CAMERA_STORAGE_BYTES=2147483648\n'
)
fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as f:
    f.write(text)
print('Created .env (private). Existing configurations are never overwritten.')
