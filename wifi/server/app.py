"""Single-device camera service. Device v1 contract: ../INTERFACE_REVIEW.txt.

One Uvicorn worker; SQLite plus fsync'ed chunk files. No direct hardware access.
"""
from contextlib import asynccontextmanager, contextmanager
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import struct
import threading
import time
from typing import Annotated, Literal
import uuid
import zlib

from fastapi import Depends, FastAPI, HTTPException, Query, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import FileResponse, JSONResponse, Response
from fastapi.security import HTTPBasic, HTTPBasicCredentials
from pydantic import BaseModel, ConfigDict, Field, StrictBool, StrictInt

HEX_ID = r'^[0-9a-f]{32}$'
Id = Annotated[str, Field(pattern=HEX_ID)]
ACTIVE = ('queued', 'claimed', 'dispatched')
TERMINAL = ('succeeded', 'failed', 'unknown', 'expired')
KINDS = ('capture', 'set_parameters', 'timelapse_start', 'timelapse_stop')
STATIC = Path(__file__).parent / 'static'


def fail(status, code, message, **extra):
    raise HTTPException(status, {'code': code, 'message': message, **extra})


class Model(BaseModel):
    model_config = ConfigDict(extra='forbid')


class McuStatus(Model):
    exposure: StrictInt | None = None
    gain: StrictInt | None = None
    recording: StrictBool | None = None
    interval_s: StrictInt | None = None
    rtc_valid: StrictBool | None = None
    sd_ready: StrictBool | None = None
    busy: StrictBool | None = None
    age_ms: Annotated[int, Field(strict=True, ge=0, le=4294967295)]


class Heartbeat(Model):
    protocol: Literal[1]
    boot_id: Id
    firmware: Annotated[str, Field(min_length=1, max_length=64)]
    uptime_s: Annotated[int, Field(strict=True, ge=0, le=4294967295)]
    wifi_rssi_dbm: Annotated[int, Field(strict=True, ge=-127, le=0)]
    free_heap_bytes: Annotated[int, Field(strict=True, ge=0, le=1048576)]
    mcu_online: StrictBool
    mcu: McuStatus | None = None


class CommandInput(Model):
    request_id: Id
    kind: Literal['capture', 'set_parameters', 'timelapse_start', 'timelapse_stop']
    args: dict = Field(default_factory=dict)


class Event(Model):
    event_id: Id
    lease_id: Id
    state: Literal['dispatched', 'succeeded', 'failed', 'unknown']
    code: Annotated[str, Field(pattern=r'^[A-Z0-9_]{1,48}$')]
    result: dict = Field(default_factory=dict)


class Photo(Model):
    photo_id: Id
    name: Annotated[str, Field(pattern=r'^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$')]
    size: Annotated[int, Field(strict=True, ge=54, le=8 * 1024 * 1024)]
    crc32: Annotated[str, Field(pattern=r'^[0-9a-f]{8}$')]
    content_type: Literal['image/bmp']
    captured_at: Annotated[int, Field(strict=True, ge=946684800, le=4102444799)] | None = None


@dataclass
class Settings:
    data: Path
    device_id: str
    device_token: str
    admin_user: str
    admin_password: str
    storage_bytes: int = 2 * 1024**3

    @classmethod
    def environment(cls):
        return cls(Path(os.environ.get('CAMERA_DATA', './data')),
                   os.environ['CAMERA_DEVICE_ID'], os.environ['CAMERA_DEVICE_TOKEN'],
                   os.environ.get('CAMERA_ADMIN_USER', 'admin'),
                   os.environ['CAMERA_ADMIN_PASSWORD'],
                   int(os.environ.get('CAMERA_STORAGE_BYTES', str(2 * 1024**3))))


def validate_bmp(path, size):
    with path.open('rb') as f:
        header = f.read(54)
    if len(header) != 54 or header[:2] != b'BM':
        return False
    declared, = struct.unpack_from('<I', header, 2)
    offset, dib, width, height, planes, depth, compression = struct.unpack_from('<IIiiHHI', header, 10)
    return (declared == size and dib == 40 and offset == 54 and planes == 1
            and depth == 24 and compression == 0 and 0 < width <= 4096
            and 0 < abs(height) <= 4096
            and offset + ((width * 3 + 3) // 4 * 4) * abs(height) == size)


def create_app(settings=None):
    config = settings or Settings.environment()
    if (not re.fullmatch(HEX_ID, config.device_id) or len(config.device_token) < 32
            or len(config.admin_password) < 16 or not config.admin_user or config.storage_bytes < 54
            or config.admin_password == config.device_token):
        raise ValueError('Invalid configuration: set distinct device token and admin password.')
    config.data.mkdir(parents=True, exist_ok=True)
    files = config.data / 'photos'
    files.mkdir(exist_ok=True)
    lock = threading.RLock()
    db = sqlite3.connect(config.data / 'camera.sqlite3', check_same_thread=False)
    db.row_factory = sqlite3.Row
    db.execute('PRAGMA journal_mode=WAL')
    db.execute('PRAGMA synchronous=FULL')
    db.executescript('''
      CREATE TABLE IF NOT EXISTS device (id TEXT PRIMARY KEY, seen INTEGER, payload TEXT);
      CREATE TABLE IF NOT EXISTS commands (
        id TEXT PRIMARY KEY, request_id TEXT UNIQUE, kind TEXT, args TEXT, state TEXT,
        created INTEGER, expires INTEGER, lease_id TEXT, lease_until INTEGER,
        code TEXT, result TEXT);
      CREATE TABLE IF NOT EXISTS events (id TEXT PRIMARY KEY, command_id TEXT, payload TEXT, response TEXT);
      CREATE TABLE IF NOT EXISTS photos (
        id TEXT PRIMARY KEY, metadata TEXT, size INTEGER, crc TEXT, created INTEGER, complete INTEGER);
      CREATE TABLE IF NOT EXISTS service_meta (key TEXT PRIMARY KEY, value TEXT);
    ''')
    old = db.execute("SELECT value FROM service_meta WHERE key='device_id'").fetchone()
    if old and old['value'] != config.device_id:
        db.close()
        raise ValueError('Existing data belongs to another device_id; keep the original configuration.')
    db.execute("INSERT OR IGNORE INTO service_meta VALUES ('device_id', ?)", (config.device_id,))
    db.commit()

    @contextmanager
    def transaction():
        with lock:
            try:
                yield db
                db.commit()
            except Exception:
                db.rollback()
                raise

    @asynccontextmanager
    async def lifespan(app):
        yield
        db.close()

    app = FastAPI(title='Camera remote service', docs_url=None, redoc_url=None,
                  openapi_url=None, lifespan=lifespan)
    app.state.config = config

    @app.exception_handler(HTTPException)
    async def http_error(request, exc):
        detail = exc.detail if isinstance(exc.detail, dict) else {'code': 'HTTP_ERROR', 'message': str(exc.detail)}
        return JSONResponse({'error': detail}, status_code=exc.status_code, headers=exc.headers)

    @app.exception_handler(RequestValidationError)
    async def invalid(request, exc):
        return JSONResponse({'error': {'code': 'BAD_ARGUMENT', 'message': 'Invalid request fields.'}}, status_code=422)

    @app.middleware('http')
    async def limits_and_headers(request, call_next):
        # Bound even chunked requests before FastAPI JSON parsing. Never log bodies.
        maximum = 512 if '/chunks/' in request.url.path else 2048
        body = bytearray()
        async for chunk in request.stream():
            body.extend(chunk)
            if len(body) > maximum:
                return JSONResponse({'error': {'code': 'TOO_LARGE', 'message': 'Request body too large.'}}, status_code=413)
        request._body = bytes(body)
        response = await call_next(request)
        response.headers['Cache-Control'] = 'no-store'
        response.headers['X-Content-Type-Options'] = 'nosniff'
        response.headers['Referrer-Policy'] = 'no-referrer'
        response.headers['Content-Security-Policy'] = "default-src 'self'; img-src 'self'; style-src 'self'; script-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"
        return response

    basic = HTTPBasic(auto_error=False)

    def admin(request: Request, auth: HTTPBasicCredentials | None = Depends(basic)):
        if not auth or not (secrets.compare_digest(auth.username.encode(), config.admin_user.encode())
                           & secrets.compare_digest(auth.password.encode(), config.admin_password.encode())):
            raise HTTPException(401, {'code': 'AUTH_REQUIRED', 'message': 'Login required.'},
                                headers={'WWW-Authenticate': 'Basic realm="Camera", charset="UTF-8"'})
        if request.method not in ('GET', 'HEAD') and request.headers.get('X-Camera-UI') != '1':
            fail(403, 'CSRF_REJECTED', 'Missing UI request header.')

    def device(device_id: str, request: Request):
        expected = ('Bearer ' + config.device_token).encode()
        if not secrets.compare_digest(request.headers.get('Authorization', '').encode(), expected):
            fail(401, 'AUTH_REQUIRED', 'Device authentication failed.')
        if device_id != config.device_id:
            fail(403, 'WRONG_DEVICE', 'Token does not authorize this device.')

    def expire(conn, now):
        conn.execute("UPDATE commands SET state='expired',code='QUEUE_EXPIRED' WHERE state='queued' AND expires<=?", (now,))
        conn.execute("UPDATE commands SET state='unknown',code='LEASE_EXPIRED' WHERE state IN ('claimed','dispatched') AND lease_until<=?", (now,))

    def current(conn, now):
        row = conn.execute('SELECT * FROM device WHERE id=?', (config.device_id,)).fetchone()
        payload = json.loads(row['payload']) if row else None
        online = bool(row and now - row['seen'] < 90)
        return {'device_id': config.device_id, 'online': online,
                'last_seen': row['seen'] if row else None,
                'mcu_online': bool(online and payload['mcu_online']), 'status': payload}

    def command_view(row):
        return {'command_id': row['id'], 'kind': row['kind'], 'args': json.loads(row['args']),
                'state': row['state'], 'created_at': row['created'], 'code': row['code'],
                'result': json.loads(row['result']) if row['result'] else {}}

    @app.get('/healthz')
    def health():
        return {'ok': True}

    @app.get('/favicon.ico')
    def favicon():
        return Response(status_code=204)

    @app.get('/', dependencies=[Depends(admin)])
    def index():
        return FileResponse(STATIC / 'index.html')

    @app.get('/static/{name}', dependencies=[Depends(admin)])
    def asset(name: str):
        if name not in ('app.js', 'style.css'):
            fail(404, 'NOT_FOUND', 'Asset not found.')
        return FileResponse(STATIC / name)

    @app.get('/api/ui/status', dependencies=[Depends(admin)])
    def ui_status():
        with transaction() as conn:
            expire(conn, int(time.time()))
            result = current(conn, int(time.time()))
            result['commands'] = [command_view(r) for r in conn.execute('SELECT * FROM commands ORDER BY created DESC,rowid DESC LIMIT 20')]
            result['storage_bytes'] = conn.execute('SELECT coalesce(sum(size),0) FROM photos').fetchone()[0]
            result['storage_limit'] = config.storage_bytes
            return result

    @app.post('/api/ui/commands', dependencies=[Depends(admin)])
    def queue(cmd: CommandInput):
        args = cmd.args
        valid = ((cmd.kind in ('capture', 'timelapse_stop') and args == {}) or
                 (cmd.kind == 'set_parameters' and set(args) == {'exposure', 'gain'}
                  and type(args['exposure']) is int and 3 <= args['exposure'] <= 3990
                  and type(args['gain']) is int and 16 <= args['gain'] <= 143) or
                 (cmd.kind == 'timelapse_start' and set(args) == {'interval_s'}
                  and type(args['interval_s']) is int and args['interval_s'] in (2, 5, 10, 30, 60)))
        if not valid:
            fail(422, 'BAD_ARGUMENT', 'Unsupported command arguments.')
        encoded = json.dumps(args, sort_keys=True, separators=(',', ':'))
        now = int(time.time())
        with transaction() as conn:
            expire(conn, now)
            prior = conn.execute('SELECT * FROM commands WHERE request_id=?', (cmd.request_id,)).fetchone()
            if prior:
                if prior['kind'] != cmd.kind or prior['args'] != encoded:
                    fail(409, 'ID_CONFLICT', 'Request ID reused with different arguments.')
                return command_view(prior)
            if not current(conn, now)['mcu_online']:
                fail(409, 'DEVICE_OFFLINE', 'Device or MCU is offline.')
            if conn.execute("SELECT 1 FROM commands WHERE state IN ('queued','claimed','dispatched')").fetchone():
                fail(409, 'COMMAND_BUSY', 'Wait for the active command to finish.')
            # Opaque, increasing 128-bit IDs let a bounded MCU result cache
            # reject evicted IDs without replaying effects. Persist high-water
            # in the SAME transaction; retain it even if history is pruned.
            high = conn.execute("SELECT value FROM service_meta WHERE key='command_highwater'").fetchone()
            historical = conn.execute('SELECT MAX(id) FROM commands').fetchone()[0]
            previous = max(int(high['value'], 16) if high else 0, int(historical, 16) if historical else 0)
            next_id = previous + 1 if previous else uuid.uuid4().int
            if next_id >= 1 << 128:
                fail(409, 'ID_EXHAUSTED', 'Command identifier space exhausted.')
            identifier = f'{next_id:032x}'
            conn.execute("INSERT OR REPLACE INTO service_meta VALUES ('command_highwater', ?)", (identifier,))
            conn.execute('INSERT INTO commands VALUES (?,?,?,?,?,?,?,?,?,?,?)',
                         (identifier, cmd.request_id, cmd.kind, encoded, 'queued', now, now + 120, None, 0, None, None))
            return command_view(conn.execute('SELECT * FROM commands WHERE id=?', (identifier,)).fetchone())

    prefix = '/api/v1/devices/{device_id}'

    @app.post(prefix + '/heartbeat', dependencies=[Depends(device)])
    def heartbeat(beat: Heartbeat):
        now = int(time.time())
        with transaction() as conn:
            prior = conn.execute('SELECT payload FROM device WHERE id=?', (config.device_id,)).fetchone()
            if prior and json.loads(prior['payload'])['boot_id'] != beat.boot_id:
                conn.execute("UPDATE commands SET state='unknown',code='DEVICE_REBOOT' WHERE state IN ('claimed','dispatched')")
                conn.execute("UPDATE commands SET state='expired',code='DEVICE_REBOOT' WHERE state='queued'")
            conn.execute('INSERT OR REPLACE INTO device VALUES (?,?,?)', (config.device_id, now, beat.model_dump_json()))
            expire(conn, now)
        return {'protocol': 1, 'server_time': now, 'heartbeat_interval_s': 30}

    @app.get(prefix + '/time', dependencies=[Depends(device)])
    def server_time():
        return {'unix_s': int(time.time()), 'rtc_utc_offset_min': 480}

    @app.get(prefix + '/commands/next', dependencies=[Depends(device)])
    def next_command():
        now = int(time.time())
        with transaction() as conn:
            expire(conn, now)
            if not current(conn, now)['mcu_online']:
                return Response(status_code=204)
            row = conn.execute("SELECT * FROM commands WHERE state IN ('queued','claimed','dispatched') ORDER BY created LIMIT 1").fetchone()
            if not row or row['state'] == 'dispatched':
                return Response(status_code=204)
            if row['state'] == 'queued':
                conn.execute("UPDATE commands SET state='claimed',lease_id=?,lease_until=? WHERE id=?", (uuid.uuid4().hex, now + 120, row['id']))
                row = conn.execute('SELECT * FROM commands WHERE id=?', (row['id'],)).fetchone()
            return {'command_id': row['id'], 'lease_id': row['lease_id'],
                    'lease_s': row['lease_until'] - now, 'kind': row['kind'],
                    'args': json.loads(row['args']), 'expires_at': row['lease_until']}

    @app.post(prefix + '/commands/{command_id}/events', dependencies=[Depends(device)])
    def event(command_id: str, event: Event):
        encoded = json.dumps(event.model_dump(), sort_keys=True, separators=(',', ':'))
        with transaction() as conn:
            previous = conn.execute('SELECT * FROM events WHERE id=?', (event.event_id,)).fetchone()
            if previous:
                if previous['command_id'] != command_id or previous['payload'] != encoded:
                    fail(409, 'ID_CONFLICT', 'Event ID reused with different data.')
                return json.loads(previous['response'])
            now = int(time.time())
            expire(conn, now)
            row = conn.execute('SELECT * FROM commands WHERE id=?', (command_id,)).fetchone()
            if not row:
                fail(404, 'NOT_FOUND', 'Command not found.')
            if row['state'] == 'queued' or row['lease_id'] != event.lease_id:
                fail(409, 'LEASE_CONFLICT', 'Command lease does not match.')
            result = json.dumps(event.result, sort_keys=True, separators=(',', ':'))
            if row['state'] in TERMINAL:
                if (row['state'], row['code'], row['result']) != (event.state, event.code, result):
                    fail(409, 'TERMINAL_STATE', 'Command result is already final.')
            else:
                conn.execute('UPDATE commands SET state=?,code=?,result=?,lease_until=? WHERE id=?',
                             (event.state, event.code, result, now + 120, command_id))
            response = {'accepted': True, 'command_id': command_id, 'state': event.state}
            conn.execute('INSERT INTO events VALUES (?,?,?,?)', (event.event_id, command_id, encoded, json.dumps(response)))
            return response

    def photo_row(conn, photo_id):
        if not re.fullmatch(HEX_ID, photo_id):
            fail(422, 'BAD_ARGUMENT', 'Invalid photo ID.')
        row = conn.execute('SELECT * FROM photos WHERE id=?', (photo_id,)).fetchone()
        if not row:
            fail(404, 'NOT_FOUND', 'Photo not found.')
        return row

    def photo_path(row):
        # A completed rename followed by power loss before DB commit is recoverable.
        final = files / (row['id'] + '.bmp')
        return final if final.exists() else files / (row['id'] + '.part')

    def photo_status(row):
        path = photo_path(row)
        return {'photo_id': row['id'], 'next_offset': path.stat().st_size if path.exists() else 0,
                'complete': bool(row['complete']), 'size': row['size'], 'crc32': row['crc']}

    def sync_directory():
        if os.name != 'nt':
            fd = os.open(files, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)

    @app.post(prefix + '/photos', dependencies=[Depends(device)])
    def begin_photo(photo: Photo):
        metadata = photo.model_dump_json()
        with transaction() as conn:
            prior = conn.execute('SELECT * FROM photos WHERE id=?', (photo.photo_id,)).fetchone()
            if prior:
                if prior['metadata'] != metadata:
                    fail(409, 'ID_CONFLICT', 'Photo ID reused with different metadata.')
                return photo_status(prior)
            used = conn.execute('SELECT coalesce(sum(size),0) FROM photos').fetchone()[0]
            if used + photo.size > config.storage_bytes:
                fail(507, 'STORAGE_FULL', 'Photo storage quota exhausted.')
            conn.execute('INSERT INTO photos VALUES (?,?,?,?,?,0)',
                         (photo.photo_id, metadata, photo.size, photo.crc32, int(time.time())))
            return JSONResponse(photo_status(photo_row(conn, photo.photo_id)), status_code=201)

    @app.get(prefix + '/photos/{photo_id}', dependencies=[Depends(device)])
    def get_photo(photo_id: str):
        with transaction() as conn:
            return photo_status(photo_row(conn, photo_id))

    @app.put(prefix + '/photos/{photo_id}/chunks/{offset}', dependencies=[Depends(device)])
    async def put_chunk(photo_id: str, offset: int, request: Request):
        if request.headers.get('Content-Type', '').split(';')[0] != 'application/octet-stream':
            fail(415, 'CONTENT_TYPE', 'Use application/octet-stream.')
        data = await request.body()
        if not 1 <= len(data) <= 512 or offset < 0:
            fail(422, 'BAD_ARGUMENT', 'Chunk must contain 1..512 bytes at a nonnegative offset.')
        with transaction() as conn:
            row = photo_row(conn, photo_id)
            path = photo_path(row)
            current_size = path.stat().st_size if path.exists() else 0
            end = offset + len(data)
            if end <= current_size:
                with path.open('rb') as f:
                    f.seek(offset)
                    same = f.read(len(data)) == data
                if same:
                    return {'photo_id': photo_id, 'next_offset': current_size}
            if row['complete'] or offset != current_size or end > row['size']:
                fail(409, 'OFFSET_CONFLICT', 'Chunk does not match upload position.', expected_offset=current_size)
            try:
                with path.open('ab') as f:
                    f.write(data)
                    f.flush()
                    os.fsync(f.fileno())
                if current_size == 0:
                    sync_directory()
            except OSError:
                # Do not advance client offset on error; query status before retrying.
                fail(507, 'STORAGE_ERROR', 'Unable to persist photo chunk.')
            return {'photo_id': photo_id, 'next_offset': end}

    @app.post(prefix + '/photos/{photo_id}/complete', dependencies=[Depends(device)])
    def finish_photo(photo_id: str, body: Model):
        with transaction() as conn:
            row = photo_row(conn, photo_id)
            path = photo_path(row)
            if not path.exists() or path.stat().st_size != row['size']:
                fail(409, 'INCOMPLETE', 'Photo length is incomplete.')
            crc = 0
            with path.open('rb') as f:
                while chunk := f.read(65536):
                    crc = zlib.crc32(chunk, crc)
            if f'{crc:08x}' != row['crc']:
                fail(409, 'CRC_MISMATCH', 'Photo checksum does not match.')
            if not validate_bmp(path, row['size']):
                fail(422, 'INVALID_BMP', 'Expected an uncompressed 24-bit BMP with a 40-byte DIB header.')
            if not row['complete']:
                final = files / (photo_id + '.bmp')
                if path != final:
                    os.replace(path, final)
                sync_directory()
                conn.execute('UPDATE photos SET complete=1 WHERE id=?', (photo_id,))
            return {'photo_id': photo_id, 'complete': True, 'size': row['size'], 'crc32': row['crc']}

    @app.get('/api/ui/photos', dependencies=[Depends(admin)])
    def gallery(before: int = Query(default=9223372036854775807, ge=1, le=9223372036854775807)):
        with transaction() as conn:
            rows = conn.execute('SELECT rowid AS cursor,* FROM photos WHERE complete=1 AND rowid<? ORDER BY rowid DESC LIMIT 24', (before,)).fetchall()
            return {'photos': [{**json.loads(r['metadata']), 'uploaded_at': r['created'],
                                'url': '/api/ui/photos/' + r['id'] + '/file', 'cursor': r['cursor']} for r in rows],
                    'next_cursor': rows[-1]['cursor'] if len(rows) == 24 else None}

    @app.get('/api/ui/photos/{photo_id}/file', dependencies=[Depends(admin)])
    def download(photo_id: str, attachment: bool = False):
        with transaction() as conn:
            row = photo_row(conn, photo_id)
            if not row['complete']:
                fail(404, 'NOT_FOUND', 'Photo upload is not complete.')
            name = json.loads(row['metadata'])['name']
            return FileResponse(photo_path(row), media_type='image/bmp', filename=name,
                                content_disposition_type='attachment' if attachment else 'inline')

    return app
