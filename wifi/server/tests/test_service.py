import json
import sqlite3
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import uuid
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fastapi.testclient import TestClient
from app import Settings, create_app


def bmp():
    width, height = 32, 16
    pixels = bytes([70, 120, 190]) * width * height
    return (struct.pack('<2sIHHI', b'BM', 54 + len(pixels), 0, 0, 54)
            + struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 24, 0, len(pixels), 0, 0, 0, 0) + pixels)


class ServiceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.settings = Settings(Path(self.temp.name), '1' * 32, 't' * 40, 'test-admin', 'p' * 24)
        self.app = create_app(self.settings)
        self.client = TestClient(self.app)
        self.client.__enter__()
        self.prefix = '/api/v1/devices/' + self.settings.device_id
        self.headers = {'Authorization': 'Bearer ' + self.settings.device_token}
        self.auth = (self.settings.admin_user, self.settings.admin_password)
        self.ui_headers = {'X-Camera-UI': '1'}
        self.beat = dict(protocol=1, boot_id='2' * 32, firmware='test-only', uptime_s=10,
                         wifi_rssi_dbm=-50, free_heap_bytes=25000, mcu_online=True,
                         mcu=dict(exposure=3990, gain=80, recording=False, interval_s=2,
                                  rtc_valid=True, sd_ready=True, busy=False, age_ms=0))

    def tearDown(self):
        self.client.__exit__(None, None, None)
        self.temp.cleanup()

    def heartbeat(self):
        return self.client.post(self.prefix + '/heartbeat', json=self.beat, headers=self.headers)

    def status(self):
        return self.client.get('/api/ui/status', auth=self.auth).json()

    def command(self, **updates):
        value = dict(request_id=uuid.uuid4().hex, kind='capture', args={})
        value.update(updates)
        return self.client.post('/api/ui/commands', json=value, auth=self.auth, headers=self.ui_headers)

    def claim(self):
        return self.client.get(self.prefix + '/commands/next', headers=self.headers)

    def event(self, claim, state='succeeded', event_id=None):
        return self.client.post(self.prefix + '/commands/' + claim['command_id'] + '/events',
                                json=dict(event_id=event_id or uuid.uuid4().hex, lease_id=claim['lease_id'],
                                          state=state, code='OK', result={}), headers=self.headers)

    def begin_photo(self, data=None, **updates):
        data = bmp() if data is None else data
        meta = dict(photo_id=uuid.uuid4().hex, name='T0000018.BMP', size=len(data),
                    crc32=f'{zlib.crc32(data):08x}', content_type='image/bmp', captured_at=None)
        meta.update(updates)
        response = self.client.post(self.prefix + '/photos', json=meta, headers=self.headers)
        return meta, response

    def chunk(self, identifier, offset, body):
        return self.client.put(f'{self.prefix}/photos/{identifier}/chunks/{offset}', content=body,
                               headers={**self.headers, 'Content-Type': 'application/octet-stream'})

    def complete(self, identifier):
        return self.client.post(f'{self.prefix}/photos/{identifier}/complete', json={}, headers=self.headers)

    def test_authentication_isolation_and_csrf(self):
        self.assertEqual(self.client.get('/').status_code, 401)
        self.assertEqual(self.client.get('/', headers=self.headers).status_code, 401)
        self.assertEqual(self.client.get(self.prefix + '/time', auth=self.auth).status_code, 401)
        self.assertEqual(self.client.get('/api/v1/devices/' + '3' * 32 + '/time', headers=self.headers).status_code, 403)
        self.assertEqual(self.client.post('/api/ui/commands', json={}, auth=self.auth).status_code, 403)
        self.assertEqual(self.client.get('/', auth=self.auth).status_code, 200)
        self.assertEqual(self.client.get('/healthz').json(), {'ok': True})

    def test_body_limits_and_invalid_fields(self):
        response = self.client.post(self.prefix + '/heartbeat', content=b'x' * 2049, headers=self.headers)
        self.assertEqual(response.status_code, 413)
        bad = {**self.beat, 'unexpected': 'field'}
        response = self.client.post(self.prefix + '/heartbeat', json=bad, headers=self.headers)
        self.assertEqual(response.status_code, 422)
        self.assertIn('error', response.json())
        self.assertNotIn(self.settings.device_token, response.text)

    def test_heartbeat_freshness_time_and_missing_mcu(self):
        with patch('app.time.time', return_value=1000):
            response = self.heartbeat()
            self.assertEqual(response.json()['server_time'], 1000)
            self.assertEqual(int(response.headers['content-length']), len(response.content))
            self.assertEqual(response.headers['cache-control'], 'no-store')
            self.assertTrue(self.status()['mcu_online'])
        with patch('app.time.time', return_value=1091):
            self.assertFalse(self.status()['online'])
            self.assertEqual(self.command().status_code, 409)
        self.beat['mcu_online'] = False
        self.beat['mcu'] = None
        self.heartbeat()
        self.assertFalse(self.status()['mcu_online'])
        self.assertEqual(self.claim().status_code, 204)

    def test_command_queue_claim_events_and_no_reexecution(self):
        self.heartbeat()
        identifier = uuid.uuid4().hex
        queued = self.command(request_id=identifier)
        self.assertEqual(queued.status_code, 200)
        self.assertEqual(queued.json()['state'], 'queued')
        self.assertEqual(self.command(request_id=identifier).json(), queued.json())
        self.assertEqual(self.command(request_id=identifier, kind='timelapse_stop').status_code, 409)
        self.assertEqual(self.command().status_code, 409)
        claim = self.claim().json()
        self.assertEqual(self.claim().json()['lease_id'], claim['lease_id'])
        dispatched = self.event(claim, 'dispatched')
        self.assertEqual(dispatched.status_code, 200)
        self.assertEqual(self.claim().status_code, 204)
        event_id = uuid.uuid4().hex
        finished = self.event(claim, event_id=event_id)
        self.assertEqual(finished.status_code, 200)
        self.assertEqual(self.event(claim, event_id=event_id).json(), finished.json())
        self.assertEqual(self.event(claim).status_code, 200)
        self.assertEqual(self.event(claim, 'dispatched').status_code, 409)
        self.assertEqual(self.event(claim, 'failed', event_id).status_code, 409)
        self.assertEqual(self.claim().status_code, 204)

    def test_queue_expiry_and_uncertain_lease(self):
        with patch('app.time.time', return_value=1000):
            self.heartbeat()
            self.command()
        with patch('app.time.time', return_value=1121):
            self.heartbeat()
            self.assertEqual(self.claim().status_code, 204)
            self.assertEqual(self.status()['commands'][0]['state'], 'expired')
            self.command()
            claim = self.claim().json()
        with patch('app.time.time', return_value=1242):
            self.heartbeat()
            self.assertEqual(self.claim().status_code, 204)
            self.assertEqual(self.status()['commands'][0]['state'], 'unknown')
            self.assertEqual(self.event(claim).status_code, 409)

    def test_command_ids_increase_across_restart_clock_rollback_and_history_pruning(self):
        previous = 0
        for _ in range(20):
            self.heartbeat()
            queued = self.command()
            self.assertEqual(queued.status_code, 200)
            value = int(queued.json()['command_id'], 16)
            self.assertGreater(value, previous)
            previous = value
            self.assertEqual(self.event(self.claim().json()).status_code, 200)
        self.client.__exit__(None, None, None)
        # Pruning history must not reset the durable high-water allocation.
        with sqlite3.connect(self.settings.data / 'camera.sqlite3') as db:
            db.execute('DELETE FROM events')
            db.execute('DELETE FROM commands')
        db.close()
        self.app = create_app(self.settings)
        self.client = TestClient(self.app)
        self.client.__enter__()
        with patch('app.time.time', return_value=1000):
            self.heartbeat()
            self.assertGreater(int(self.command().json()['command_id'], 16), previous)

    def test_command_id_migration_and_exhaustion_are_safe(self):
        self.heartbeat()
        old = self.command().json()['command_id']
        self.event(self.claim().json())
        legacy = 'f' * 30 + '00'
        with sqlite3.connect(self.settings.data / 'camera.sqlite3') as db:
            db.execute('UPDATE commands SET id=? WHERE id=?', (legacy, old))
            db.execute("DELETE FROM service_meta WHERE key='command_highwater'")
        db.close()
        self.assertEqual(self.command().json()['command_id'], 'f' * 30 + '01')
        self.event(self.claim().json())
        with sqlite3.connect(self.settings.data / 'camera.sqlite3') as db:
            db.execute("UPDATE service_meta SET value=? WHERE key='command_highwater'", ('f' * 32,))
        db.close()
        response = self.command()
        self.assertEqual(response.status_code, 409)
        self.assertEqual(response.json()['error']['code'], 'ID_EXHAUSTED')

    def test_device_reboot_prevents_command_redelivery(self):
        self.heartbeat()
        self.command()
        self.claim()
        self.beat['boot_id'] = 'f' * 32
        self.heartbeat()
        self.assertEqual(self.claim().status_code, 204)
        self.assertEqual(self.status()['commands'][0]['state'], 'unknown')

    def test_parameter_validation(self):
        self.heartbeat()
        for args in ({'exposure': 3991, 'gain': 80}, {'exposure': 100, 'gain': 0},
                     {'exposure': True, 'gain': 80}, {'exposure': 100}):
            self.assertEqual(self.command(kind='set_parameters', args=args).status_code, 422)
        self.assertEqual(self.command(kind='timelapse_start', args={'interval_s': 3}).status_code, 422)
        self.assertEqual(self.command(kind='set_parameters', args={'exposure': 3990, 'gain': 80}).status_code, 200)

    def test_photo_resume_duplicate_conflict_and_completion(self):
        data = bmp()
        meta, begun = self.begin_photo(data)
        identifier = meta['photo_id']
        self.assertEqual(begun.status_code, 201)
        self.assertEqual(self.complete(identifier).status_code, 409)
        self.assertEqual(self.chunk(identifier, 512, data[512:1024]).status_code, 409)
        self.assertEqual(self.chunk(identifier, 0, data[:512]).status_code, 200)
        self.assertEqual(self.chunk(identifier, 0, data[:512]).json()['next_offset'], 512)
        wrong = bytes([data[0] ^ 1]) + data[1:512]
        self.assertEqual(self.chunk(identifier, 0, wrong).status_code, 409)
        self.assertEqual(self.client.get('/api/ui/photos', auth=self.auth).json()['photos'], [])
        # Restart server, preserving persistent data and the upload offset.
        self.client.__exit__(None, None, None)
        self.client = TestClient(create_app(self.settings))
        self.client.__enter__()
        resumed = self.client.post(self.prefix + '/photos', json=meta, headers=self.headers)
        self.assertEqual(resumed.json()['next_offset'], 512)
        for offset in range(512, len(data), 512):
            self.assertEqual(self.chunk(identifier, offset, data[offset:offset + 512]).status_code, 200)
        self.assertEqual(self.complete(identifier).json()['complete'], True)
        self.assertEqual(self.complete(identifier).status_code, 200)
        gallery = self.client.get('/api/ui/photos', auth=self.auth).json()['photos']
        self.assertEqual(len(gallery), 1)
        self.assertEqual(gallery[0]['captured_at'], None)
        self.assertEqual(self.client.get(gallery[0]['url']).status_code, 401)
        self.assertEqual(self.client.get(gallery[0]['url'], auth=self.auth).content, data)
        self.assertEqual(self.chunk(identifier, 0, data[:512]).status_code, 200)
        changed = {**meta, 'name': 'OTHER.BMP'}
        self.assertEqual(self.client.post(self.prefix + '/photos', json=changed, headers=self.headers).status_code, 409)

    def test_bad_crc_never_enters_gallery(self):
        data = bmp()
        meta, _ = self.begin_photo(data, crc32='00000000')
        for offset in range(0, len(data), 512):
            self.chunk(meta['photo_id'], offset, data[offset:offset + 512])
        self.assertEqual(self.complete(meta['photo_id']).json()['error']['code'], 'CRC_MISMATCH')
        self.assertEqual(self.client.get('/api/ui/photos', auth=self.auth).json()['photos'], [])

    def test_recover_rename_before_database_commit(self):
        data = bmp()
        meta, _ = self.begin_photo(data)
        identifier = meta['photo_id']
        for offset in range(0, len(data), 512):
            self.chunk(identifier, offset, data[offset:offset + 512])
        files = self.settings.data / 'photos'
        # Simulate a crash after atomic rename, before complete=1 reaches SQLite.
        (files / (identifier + '.part')).replace(files / (identifier + '.bmp'))
        self.assertFalse(self.client.get(f'{self.prefix}/photos/{identifier}', headers=self.headers).json()['complete'])
        self.assertEqual(self.complete(identifier).status_code, 200)
        self.assertEqual(len(self.client.get('/api/ui/photos', auth=self.auth).json()['photos']), 1)

    def test_event_idempotency_ignores_json_object_key_order(self):
        self.heartbeat()
        self.command()
        claim = self.claim().json()
        event = dict(event_id=uuid.uuid4().hex, lease_id=claim['lease_id'], state='succeeded',
                     code='OK', result={'a': 1, 'b': 2})
        path = self.prefix + '/commands/' + claim['command_id'] + '/events'
        first = self.client.post(path, json=event, headers=self.headers)
        self.assertEqual(first.status_code, 200)
        event['result'] = {'b': 2, 'a': 1}
        self.assertEqual(self.client.post(path, json=event, headers=self.headers).json(), first.json())

    def test_invalid_bmp_path_and_oversized_chunk(self):
        data = b'not a BMP' * 10
        meta, _ = self.begin_photo(data)
        self.assertEqual(self.chunk(meta['photo_id'], 0, b'x' * 513).status_code, 413)
        self.chunk(meta['photo_id'], 0, data)
        self.assertEqual(self.complete(meta['photo_id']).status_code, 422)
        _, response = self.begin_photo(name='../bad.bmp')
        self.assertEqual(response.status_code, 422)

    def test_storage_quota_reserves_incomplete_uploads(self):
        self.settings.storage_bytes = len(bmp())
        self.assertEqual(self.begin_photo()[1].status_code, 201)
        self.assertEqual(self.begin_photo()[1].status_code, 507)

    def test_identity_cannot_change_over_existing_data(self):
        changed = Settings(self.settings.data, 'a' * 32, 'k' * 40, 'admin', 'p' * 24)
        with self.assertRaises(ValueError):
            create_app(changed)


if __name__ == '__main__':
    unittest.main()
