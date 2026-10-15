'use strict';
const $ = id => document.getElementById(id);
const names = {capture:'拍摄照片',set_parameters:'调整曝光与增益',timelapse_start:'开始延时摄影',timelapse_stop:'停止延时摄影'};
const states = {queued:'待领取',claimed:'已领取',dispatched:'设备已接受',succeeded:'执行成功',failed:'执行失败',unknown:'结果未知 · 请核对设备',expired:'已过期，未执行'};
let nextCursor = null, photoIds = new Set(), dirtyParameters = false, refreshing = false;
let canControl = false, sending = false, pendingRequest = null, latestId = null;
const date = value => value == null ? '拍摄时间未校准' : new Date(value*1000).toLocaleString('zh-CN',{hour12:false});
function notice(text) { $('notice').textContent=text; $('notice').hidden=!text; }
function controls() { document.querySelectorAll('[data-control]').forEach(el=>el.disabled=!canControl||sending); }
async function api(path, options={}) {
  const response=await fetch(path,{...options,cache:'no-store',headers:{'Content-Type':'application/json','X-Camera-UI':'1',...options.headers}});
  const data=await response.json();
  if(!response.ok) {const error=new Error(data.error?.message || `请求失败 (${response.status})`);error.status=response.status;throw error;}
  return data;
}
function showStatus(data) {
  $('connection').textContent=data.online?'Wi-Fi 设备在线':'Wi-Fi 设备离线';
  $('connection').className='pill'+(data.online?' online':'');
  $('mcu').textContent=data.mcu_online?'MCU 已连接':'MCU 未连接';
  $('mcu').className='pill'+(data.mcu_online?' online':'');
  const heartbeat=data.status, mcu=heartbeat?.mcu;
  $('seen').textContent=data.last_seen?new Date(data.last_seen*1000).toLocaleTimeString('zh-CN',{hour12:false}):'尚未连接';
  $('device-detail').textContent=heartbeat?`固件 ${heartbeat.firmware} · Wi-Fi ${heartbeat.wifi_rssi_dbm} dBm`:'等待设备首次连接';
  const stale=!data.mcu_online || !mcu || mcu.age_ms+(Date.now()/1000-data.last_seen)*1000>90000;
  $('sd').textContent=stale?'未知':mcu.sd_ready==null?'未知':mcu.sd_ready?'可用':'未就绪';
  $('recording').textContent=stale?'未知':mcu.recording==null?'未知':mcu.recording?'延时摄影中':'未录制';
  $('rtc').textContent=stale?'未知':mcu.rtc_valid==null?'未知':mcu.rtc_valid?'已校准':'未校准';
  $('status-age').textContent=stale?'设备参数未知或已过期。':`设备上报：曝光 ${mcu.exposure??'未知'} / 增益 ${mcu.gain??'未知'}`;
  if(!stale&&!dirtyParameters) {
    $('exposure').value=mcu.exposure??''; $('gain').value=mcu.gain??'';
  }
  canControl=data.mcu_online && !data.commands.some(c=>['queued','claimed','dispatched'].includes(c.state)); controls();
  $('storage').textContent=`存储 ${(data.storage_bytes/1048576).toFixed(1)} / ${(data.storage_limit/1048576).toFixed(0)} MB`;
  $('commands').replaceChildren();
  if(!data.commands.length) { const p=document.createElement('p');p.className='muted';p.textContent='暂无远程操作。';$('commands').append(p); }
  for(const cmd of data.commands) {
    const row=document.createElement('div');row.className='command';
    const label=document.createElement('span');label.textContent=names[cmd.kind]||cmd.kind;
    const state=document.createElement('span');state.className='state '+cmd.state;state.textContent=states[cmd.state]||cmd.state;
    const time=document.createElement('small');time.textContent=date(cmd.created_at)+(cmd.code?` · ${cmd.code}`:'');
    row.append(label,state,time);$('commands').append(row);
  }
}
async function loadPhotos(append=false) {
  const data=await api('/api/ui/photos'+(append&&nextCursor?`?before=${nextCursor}`:''));
  if(!append){$('gallery').replaceChildren();photoIds=new Set();}
  for(const p of data.photos) {
    if(photoIds.has(p.photo_id)) continue; photoIds.add(p.photo_id);
    const a=document.createElement('a');a.href=p.url+'?attachment=true';a.className='photo';a.title='下载原图';
    const img=document.createElement('img');img.src=p.url;img.alt=p.name;img.loading='lazy';
    const text=document.createElement('div'),title=document.createElement('strong'),when=document.createElement('span');
    title.textContent=p.name;when.textContent=date(p.captured_at);text.append(title,when);a.append(img,text);$('gallery').append(a);
  }
  nextCursor=data.next_cursor;$('more').hidden=!nextCursor;$('empty-gallery').hidden=photoIds.size>0;
  if(!append&&data.photos.length){
    const p=data.photos[0]; if(p.photo_id!==latestId){$('latest').src=p.url;latestId=p.photo_id;}
    $('latest').hidden=false;$('empty-latest').hidden=true;$('photo-date').textContent=date(p.captured_at);
    $('photo-info').textContent=`${p.name} · ${(p.size/1024).toFixed(1)} KB`;
    $('download').href=p.url+'?attachment=true';$('download').hidden=false;
  }
}
async function refresh(withPhotos=false) {
  if(refreshing) return; refreshing=true;
  try {showStatus(await api('/api/ui/status'));if(withPhotos)await loadPhotos();}
  catch(e){canControl=false;controls();$('connection').textContent='服务器连接失败';$('connection').className='pill';notice(e.message);}
  finally{refreshing=false;}
}
async function command(kind,args={}) {
  if(sending||!canControl)return;
  const fingerprint=JSON.stringify({kind,args});
  if(pendingRequest&&pendingRequest.fingerprint!==fingerprint){
    notice('上一条请求的接收结果未知，请刷新操作记录确认后重新加载页面。');return;
  }
  if(!pendingRequest)pendingRequest={fingerprint,id:crypto.randomUUID().replaceAll('-','')};
  sending=true;controls();
  try {
    await api('/api/ui/commands',{method:'POST',body:JSON.stringify({request_id:pendingRequest.id,kind,args})});
    pendingRequest=null;notice('命令已排队，请在操作记录中查看设备执行结果。');
    if(kind==='set_parameters')dirtyParameters=false;
  }catch(e){
    if(e.status>=400&&e.status<500){pendingRequest=null;notice(`请求未接受：${e.message}`);}
    else notice(`请求未确认：${e.message}。相同操作重试会沿用请求编号。`);
  }
  finally{sending=false;await refresh();controls();}
}
$('capture').onclick=()=>command('capture');
$('start').onclick=()=>command('timelapse_start',{interval_s:Number($('interval').value)});
$('stop').onclick=()=>command('timelapse_stop');
$('parameters').onsubmit=e=>{e.preventDefault();command('set_parameters',{exposure:Number($('exposure').value),gain:Number($('gain').value)});};
for(const id of ['gain','exposure'])$(id).addEventListener('input',()=>dirtyParameters=true);
$('refresh').onclick=()=>{notice('');refresh(true);};
$('more').onclick=()=>loadPhotos(true).catch(e=>notice(e.message));
refresh(true);setInterval(()=>refresh(),5000);setInterval(()=>{if(document.visibilityState==='visible'&&!nextCursor)loadPhotos().catch(e=>notice(e.message));},30000);
