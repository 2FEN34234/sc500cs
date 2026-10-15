"""Actual ESP state code + actual MCU wire transport + actual FastAPI service.

UART hardware/MCU camera operations and HTTP/TLS sockets are simulated. No public
server is contacted and all credentials below are ephemeral local test values.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'server'))
from app import Settings, create_app
from fastapi.testclient import TestClient


def main():
    with tempfile.TemporaryDirectory() as temp:
        settings = Settings(Path(temp), '0' * 32, 'test-only-token-' * 3,
                            'local-test', 'test-only-browser-' * 2)
        with TestClient(create_app(settings)) as client:
            process = subprocess.Popen([str(ROOT / 'build' / 'integration_driver.exe')],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            count = 0
            command_id = None
            lost_chunk_reply = False
            lost_event_reply = False
            done = False
            try:
                for line in process.stdout:
                    message = json.loads(line)
                    if message.get('done'):
                        assert message['actions'] == message['commits'] == 1
                        done = True
                        break
                    count += 1
                    assert count < 300, 'Firmware did not converge'
                    path = urlsplit(message['url']).path
                    payload = bytes.fromhex(message['hex'])
                    binary = '/chunks/' in path
                    headers = {'Authorization': 'Bearer ' + settings.device_token,
                               'Content-Type': 'application/octet-stream' if binary else 'application/json'}
                    response = client.request(message['method'], path, content=payload, headers=headers)
                    assert response.status_code < 400, (path, response.status_code, response.text)
                    if path.endswith('/heartbeat') and command_id is None:
                        created = client.post('/api/ui/commands', auth=(settings.admin_user, settings.admin_password),
                                              headers={'X-Camera-UI': '1'},
                                              json={'request_id': uuid.uuid4().hex, 'kind': 'capture', 'args': {}})
                        assert created.status_code in (200, 201), created.text
                        command_id = created.json()['command_id']
                    code, body = response.status_code, response.text
                    # The server persists the write/event; the device loses its response.
                    if binary and not lost_chunk_reply:
                        code, body, lost_chunk_reply = -1, '', True
                    if path.endswith('/events') and not lost_event_reply:
                        code, body, lost_event_reply = -1, '', True
                    process.stdin.write(json.dumps({'code': code, 'body': body}) + '\n')
                    process.stdin.flush()
                process.wait(timeout=10)
                assert process.returncode == 0, process.stderr.read()
                assert done and lost_chunk_reply and lost_event_reply
                auth = (settings.admin_user, settings.admin_password)
                status = client.get('/api/ui/status', auth=auth).json()
                assert any(c['command_id'] == command_id and c['state'] == 'succeeded'
                           for c in status['commands']), status
                gallery = client.get('/api/ui/photos', auth=auth).json()
                assert len(gallery['photos']) == 1
                downloaded = client.get('/api/ui/photos/' + '3' * 32 + '/file', auth=auth)
                assert downloaded.status_code == 200 and len(downloaded.content) == 1590
                assert downloaded.content[:2] == b'BM'
                print(f'PASS: ESP firmware -> MCU transport -> server; {count} HTTP requests, '
                      'lost event/chunk replies recovered, one capture, one commit, valid BMP download')
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
