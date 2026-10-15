"""Fetch the pinned official release into the isolated Arduino download cache.

GitHub API asset download avoids a release-page HEAD request that fails on some
networks. Verify the official ESP8266 package-index SHA256 before installing.
"""
import hashlib
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / '.tools/downloads/packages/esp8266-3.1.2.zip'
SHA256 = 'b3f47686d7783c120c2f10bf82788f921c53db8642cc87012599abb6e335b182'
SIZE = 39269515
URL = 'https://api.github.com/repos/esp8266/Arduino/releases/assets/100205356'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    if TARGET.exists() and TARGET.stat().st_size == SIZE and digest(TARGET) == SHA256:
        print('ESP8266 3.1.2 archive already verified')
        return
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    temporary = TARGET.with_suffix('.zip.part')
    request = urllib.request.Request(URL, headers={'Accept': 'application/octet-stream',
                                                 'User-Agent': 'ESP8266-offline-build'})
    total = 0
    with urllib.request.urlopen(request, timeout=45) as response, temporary.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > SIZE:
                raise ValueError('Official archive exceeds pinned size')
            output.write(chunk)
    if total != SIZE or digest(temporary) != SHA256:
        raise ValueError('Official archive checksum/size mismatch; not installing')
    temporary.replace(TARGET)
    print('ESP8266 3.1.2 official archive SHA256 verified')


if __name__ == '__main__':
    main()
