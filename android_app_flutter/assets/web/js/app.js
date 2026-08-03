<script>
let apiBase = localStorage.getItem('esp32ApiBase') || 'http://192.168.4.1';
let scenarios = [];
let settingsLoading = false;
let settingsLoadGeneration = 0;
let settingsEverLoaded = false;
let scenarioEditMode = false;
let currentStatus = null;
let pingTimeoutId = null;
let lastActivity = Date.now();
let currentInterval = 1000;
let lastStaState = null;
let lastWifiAssistAt = 0;
let connectionFailCount = 0;
let initialConnectionPhase = true;
let everConnected = false;
let wifiSignalText = "";
let scenariosDirty = false;
let settingsDirty = false;
let wasDisconnected = false;
let lastSettingsLoadTime = null;
let _confirmCallback = null;
const TABS=['scenarios','home','wifi']; const RTL=1; let currentTabIndex=1;
let _swipeStartX=0,_swipeStartY=0,_swipeDelta=0,_isSwiping=false,_isScrolling=false;
let _tpState = null; // {idx, isStart, h, m}
const _WITEM = 40, _WHALF = 80, _wheelInited = {};
function openTimePicker(idx, isStart){
  const sc = scenarios[idx];
  _tpState = {idx, isStart, h: isStart?sc.sh:sc.eh, m: isStart?sc.sm:sc.em};
  document.getElementById('tp-title').textContent = isStart ? 'تنظیم ساعت روشن' : 'تنظیم ساعت خاموش';
  buildWheel('tp-h-inner', 24, _tpState.h, 'tp-h-col');
  buildWheel('tp-m-inner', 60, _tpState.m, 'tp-m-col');
  document.getElementById('custom-alert-overlay').style.display = 'block';
  document.getElementById('time-picker-modal').style.display = 'block';
}
function closeTimePicker(){
  document.getElementById('time-picker-modal').style.display='none';
  document.getElementById('custom-alert-overlay').style.display='none';
  _tpState=null;
}
function confirmTimePicker(){
  if(!_tpState) return;
  const {idx, isStart, h, m} = _tpState;
  if(isStart){scenarios[idx].sh=h;scenarios[idx].sm=m;}else{scenarios[idx].eh=h;scenarios[idx].em=m;}
  const row=document.getElementById('row_'+idx);
  if(row){
    const d=row.querySelectorAll('.time-display');
    if(isStart&&d[0]) d[0].textContent=pad2(h)+':'+pad2(m);
    if(!isStart&&d[1]) d[1].textContent=pad2(h)+':'+pad2(m);
  }
  scenarios[idx]._dirty=true;
  markDirty();
  if(row) row.classList.toggle('scenario-dirty',scenarios[idx]._dirty&&!scenarios[idx]._isNew);
  closeTimePicker();
}
function buildWheel(innerId, count, selected, colId){
  const inner=document.getElementById(innerId);
  inner.innerHTML='';
  for(let i=0;i<count;i++){
    const d=document.createElement('div');
    d.className='wheel-item'+(i===selected?' active':'');
    d.textContent=String(i).padStart(2,'0');
    d.dataset.v=i;
    d.addEventListener('click',()=>snapWheel(colId,innerId,count,i));
    inner.appendChild(d);
  }
  inner.style.transform='translateY('+(_WHALF - selected*_WITEM)+'px)';
  inner.style.transition='';
  if(!_wheelInited[colId]){
    _wheelInited[colId]=true;
    initWheelGestures(document.getElementById(colId),innerId,count);
  }
}
function snapWheel(colId,innerId,count,val){
  val=Math.max(0,Math.min(count-1,val));
  const inner=document.getElementById(innerId);
  inner.style.transition='transform .28s cubic-bezier(.22,.9,.36,1.04)';
  inner.style.transform='translateY('+(_WHALF - val*_WITEM)+'px)';
  inner.querySelectorAll('.wheel-item').forEach(d=>{
    d.classList.toggle('active',parseInt(d.dataset.v)===val);
  });
  if(_tpState){if(colId==='tp-h-col') _tpState.h=val; else _tpState.m=val;}
  setTimeout(()=>inner.style.transition='',320);
}
function initWheelGestures(col,innerId,count){
  let sy=0,so=0,co=0,moved=false;
  function getOff(){const m=document.getElementById(innerId).style.transform.match(/translateY\((.+?)px\)/);return m?parseFloat(m[1]):0;}
  function onDown(y){sy=y;so=getOff();moved=false;document.getElementById(innerId).style.transition='';}
  function onMove(y){const dy=y-sy;co=so+dy;if(Math.abs(dy)>4)moved=true;document.getElementById(innerId).style.transform='translateY('+co+'px)';}
  function onUp(){
    let v=Math.round((_WHALF-co)/_WITEM);
    snapWheel(col.id,innerId,count,v);
  }
  col.addEventListener('touchstart',e=>{e.preventDefault();onDown(e.touches[0].clientY);},{passive:false});
  col.addEventListener('touchmove',e=>{e.preventDefault();onMove(e.touches[0].clientY);},{passive:false});
  col.addEventListener('touchend',e=>{e.preventDefault();onUp();},{passive:false});
  col.addEventListener('mousedown',e=>{e.preventDefault();onDown(e.clientY);
    function mm(e){onMove(e.clientY);}function mu(){onUp();document.removeEventListener('mousemove',mm);document.removeEventListener('mouseup',mu);}
    document.addEventListener('mousemove',mm);document.addEventListener('mouseup',mu);
  });
}
const CONNECTION_FAIL_THRESHOLD = 10;
let activeOperation = null;
let activeOperationTimer = null;
let statusFetchInFlight = false;
let _statusMutex = false; // Mutex ساده برای جلوگیری از race condition در fetchStatus
let activeDialog = null; // فقط یک دیالوگ سفارشی هم‌زمان مجاز است
let lastSignalRequestAt = 0;
let waitingWifiReboot = false, wifiRebootSeenDisconnect = false;

function normalizeBaseUrl(v){ v=(v||'').trim(); if(!/^https?:\/\//i.test(v)) v='http://'+v; return v.replace(/\/+$/,''); }
function apiUrl(path){ return normalizeBaseUrl(apiBase)+path; }
function apiFetch(path, opts){ return fetch(apiUrl(path), opts); }
function utf8Bytes(str){ if(window.TextEncoder){ return new TextEncoder().encode(str); } const enc=unescape(encodeURIComponent(str)); const arr=new Uint8Array(enc.length); for(let i=0;i<enc.length;i++) arr[i]=enc.charCodeAt(i); return arr; }
function fnv1aChecksum(str){ let h=0x811c9dc5; const bytes=utf8Bytes(str); for(let i=0;i<bytes.length;i++){ h^=bytes[i]; h=Math.imul(h,0x01000193)>>>0; } return h.toString(16).padStart(8,'0'); }
function newRequestId(){
  try{ if(window.crypto&&typeof window.crypto.randomUUID==='function') return window.crypto.randomUUID(); }catch(e){}
  return Date.now().toString(36)+'-'+Math.random().toString(36).slice(2)+'-'+Math.random().toString(36).slice(2);
}
function postJson(path, body, requestId){
  const payload=(body==null?{}:body), serialized=JSON.stringify(payload);
  const envelope={payload:payload,checksum:fnv1aChecksum(serialized),requestId:requestId||newRequestId()};
  return apiFetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(envelope)});
}
function showOperationModal(title,text,state){
  const modal=document.getElementById('progress-modal'), overlay=document.getElementById('custom-alert-overlay'), fill=document.getElementById('progress-bar-fill'), pct=document.getElementById('progress-percent'), titleEl=document.getElementById('progress-title'), textEl=document.getElementById('progress-text'), actionRow=document.getElementById('operation-action-row'), retryBtn=document.getElementById('operation-retry-btn'), cancelBtn=document.getElementById('operation-cancel-btn'), closeBtn=document.getElementById('operation-close-btn');
  titleEl.innerText=title||'در حال انجام عملیات...';
  textEl.innerText=text||'';
  overlay.style.display='block'; modal.style.display='block';
  actionRow.style.display=(state==='error'||state==='timeout')?'flex':'none';
  retryBtn.style.display=(state==='timeout')?'inline-block':'none';
  cancelBtn.style.display=(state==='timeout')?'inline-block':'none';
  closeBtn.style.display=(state==='error')?'inline-block':'none';
  let percent=20;
  if(state==='queued') percent=25; else if(state==='sending') percent=65; else if(state==='success') percent=100; else if(state==='error'||state==='timeout') percent=100;
  fill.style.width=percent+'%'; pct.innerText=percent+'%';
  fill.style.background=(state==='error'||state==='timeout')?'linear-gradient(90deg,var(--off),var(--warn))':'linear-gradient(90deg,var(--accent),var(--cyan))';
  titleEl.style.color=(state==='error'||state==='timeout')?'var(--off)':(state==='success'?'var(--accent)':'var(--accent)');
}
function clearOperationTimer(){ if(activeOperationTimer){ clearTimeout(activeOperationTimer); activeOperationTimer=null; } }
function startOperationTimer(){
  clearOperationTimer();
  activeOperationTimer=setTimeout(()=>{
    if(!activeOperation) return;
    activeOperation.timedOut=true;
    activeOperation.inFlight=false;
    // پاسخ یک درخواست قدیمی، بعد از timeout یا retry نباید روی عملیات فعلی اثر بگذارد.
    activeOperation.sendGeneration++;
    showOperationModal(activeOperation.title,'ارتباط با برد تا ۶۰ ثانیه برقرار نشد. درخواست هنوز انجام نشده است. می‌توانید دوباره تلاش کنید یا درخواست را لغو کنید.','timeout');
  },60000);
}
function updateOperationModal(text,state,title){ if(!activeOperation && state!=='error') return; showOperationModal(title||(activeOperation?activeOperation.title:'عملیات'),text,state); }
function closeOperationModal(force){
  if(activeOperation && !force) return;
  clearOperationTimer();
  document.getElementById('progress-modal').style.display='none';
  document.getElementById('custom-alert-overlay').style.display='none';
  const actionRow=document.getElementById('operation-action-row'); if(actionRow) actionRow.style.display='none';
  if(force) activeOperation=null;
}
function cancelActiveOperation(){
  if(!activeOperation) { closeOperationModal(true); return; }
  clearOperationTimer();
  // fetch قبلی را نمی‌توان روی برد لغو کرد؛ فقط پاسخ دیررس آن را بی‌اعتبار می‌کنیم.
  activeOperation.sendGeneration++;
  activeOperation.timedOut=true;
  activeOperation.inFlight=false;
  activeOperation=null;
  showOperationModal('درخواست لغو شد','درخواست دیگر از طرف اپ پیگیری نمی‌شود. اگر برد آن را قبلاً دریافت کرده باشد، پاسخ دیررس نادیده گرفته خواهد شد.','error');
  setTimeout(()=>closeOperationModal(true),1800);
}
function retryActiveOperation(){
  if(!activeOperation) return;
  activeOperation.sendGeneration++;
  activeOperation.timedOut=false;
  activeOperation.inFlight=false;
  showOperationModal(activeOperation.title,'در حال تلاش دوباره برای ارتباط با برد...','queued');
  startOperationTimer();
  trySendActiveOperation();
  fetchStatus();
}
function runBoardOperation(op){
  if(activeOperation){
    showOperationModal(activeOperation.title,'یک عملیات دیگر در حال انجام است. لطفاً تا پایان آن صبر کنید.','queued');
    return false;
  }
  activeOperation=Object.assign({inFlight:false,timedOut:false,requestId:newRequestId(),sendGeneration:0},op);
  showOperationModal(op.title,'در حال ارسال درخواست به برد...','sending');
  startOperationTimer();
  trySendActiveOperation();
  return true;
}
function trySendActiveOperation(){
  if(!activeOperation || activeOperation.inFlight || activeOperation.timedOut) return;
  const op=activeOperation, generation=++op.sendGeneration;
  op.inFlight=true;
  showOperationModal(op.title,'در حال انجام عملیات روی برد...','sending');
  let requestPromise;
  try { requestPromise = postJson(op.path,op.payload||{},op.requestId); }
  catch(e) { requestPromise = Promise.reject(e); }
  requestPromise.then(async r=>{
    if(!r.ok){ let t=''; try{t=await r.text();}catch(e){} throw {server:true,message:t||('HTTP '+r.status)}; }
    let data;
    try{ data=await r.json(); }catch(e){ throw {server:true,message:'Invalid JSON response'}; }
    if(data && data.status && data.status!=='success') throw {server:true,message:data.message||'Operation failed'};
    return data;
  }).then(data=>{
    if(activeOperation!==op || op.sendGeneration!==generation || op.timedOut) return;
    op.inFlight=false;
    clearOperationTimer();
    showOperationModal(op.title,op.successMessage||'عملیات با موفقیت انجام شد.','success');
    try{ if(op.onSuccess) op.onSuccess(data); }catch(e){}
    activeOperation=null;
    setTimeout(()=>closeOperationModal(true), op.closeDelay||1800);
  }).catch(err=>{
    if(activeOperation!==op || op.sendGeneration!==generation || op.timedOut) return;
    op.inFlight=false;
    if(err && err.server){
      clearOperationTimer();
      showOperationModal(op.title,op.errorMessage||'عملیات انجام نشد. برد پاسخ خطا داد.','error');
      activeOperation=null;
    } else {
      showOperationModal(op.title,'ارتباط با برد برقرار نیست. درخواست نگه داشته شد و پس از اتصال دوباره ارسال می‌شود.','queued');
      maybeAskWifiAssist(false);
    }
  });
}
function showModal(msg){ if(activeDialog && activeDialog!=='alert') return; activeDialog='alert'; document.getElementById('custom-alert-text').innerText=msg; document.getElementById('custom-alert-overlay').style.display='block'; document.getElementById('custom-alert').style.display='block'; }
function closeAlert(){ document.getElementById('custom-alert-overlay').style.display='none'; document.getElementById('custom-alert').style.display='none'; if(activeDialog==='alert') activeDialog=null; }
function openApiModal(){ document.getElementById('api-base-input').value=apiBase; document.getElementById('wifi-assist-ssid').value=localStorage.getItem('esp32ApSsid')||'ESP32_Timer_Hub'; document.getElementById('wifi-assist-pass').value=localStorage.getItem('esp32ApPass')||'12345678'; document.getElementById('custom-alert-overlay').style.display='block'; document.getElementById('api-modal').style.display='block'; }
function closeApiModal(){ document.getElementById('api-modal').style.display='none'; document.getElementById('custom-alert-overlay').style.display='none'; }
function saveApiBaseFromModal(){ apiBase=normalizeBaseUrl(document.getElementById('api-base-input').value); localStorage.setItem('esp32ApiBase',apiBase); closeApiModal(); resetActivity(); if(scenariosDirty){showToast('سناریوهای محلی حفظ شدند. برای دریافت سناریوهای برد جدید، دکمه ↻ را بزنید.','warn');} loadSettings(); fetchStatus(); showToast('آدرس اتصال ذخیره شد: '+apiBase,'good'); }
function saveWifiAssistFromModal(){ const ssid=document.getElementById('wifi-assist-ssid').value.trim()||'ESP32_Timer_Hub'; const pass=document.getElementById('wifi-assist-pass').value; localStorage.setItem('esp32ApSsid',ssid); localStorage.setItem('esp32ApPass',pass); localStorage.setItem('esp32WifiAutoApproved','1'); maybeAskWifiAssist(true); showToast('تنظیم اتصال خودکار WiFi ذخیره شد.','good'); }
function openFactoryResetModal(){ document.getElementById('factory-reset-code').value=''; document.getElementById('custom-alert-overlay').style.display='block'; document.getElementById('factory-reset-modal').style.display='block'; setTimeout(()=>document.getElementById('factory-reset-code').focus(),150); }
function closeFactoryResetModal(){ document.getElementById('factory-reset-modal').style.display='none'; document.getElementById('custom-alert-overlay').style.display='none'; }
function submitFactoryReset(){ const code=document.getElementById('factory-reset-code').value.trim(); if(code!==String.fromCharCode(49,50,51,52,53,54)){ showToast('رمز ریست کامل اشتباه است.','bad'); return; } showConfirm('این عملیات همه حافظه برد را پاک می‌کند و برد ری‌استارت می‌شود. آیا مطمئن هستید؟',()=>{ runBoardOperation({title:'ریست کامل حافظه برد',path:'/factory-reset',payload:{code:code},successMessage:'حافظه برد پاک شد. برد در حال ری‌استارت است.',closeDelay:2500,onSuccess:()=>{ localStorage.removeItem('esp32ApSsid'); localStorage.removeItem('esp32ApPass'); localStorage.removeItem('esp32WifiAutoApproved'); closeFactoryResetModal(); setTimeout(()=>{fetchStatus(); loadSettings();},5000); }}); }); }
function togglePasswordVisibility(){ const passInput=document.getElementById('wifi-pass'); const eyeIcon=document.getElementById('eye-icon'); if(!passInput) return; if(passInput.type==='password'){ passInput.type='text'; if(eyeIcon) eyeIcon.innerHTML='<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/>'; } else { passInput.type='password'; if(eyeIcon) eyeIcon.innerHTML='<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>'; } }
function _doSwitchTab(tabId){
  const nextIndex=TABS.indexOf(tabId);
  const enteringDifferentTab=nextIndex!==currentTabIndex;
  currentTabIndex=nextIndex;
  document.querySelectorAll('.nav-btn').forEach(b=>b.classList.remove('active'));
  document.getElementById('tab-btn-'+tabId).classList.add('active');
  updatePagePositions(true);
  if(tabId==='scenarios'){
    scenarioEditMode=true;
    updateScenariosManualNote();
    // هر بار بعد از خروج و ورود، سناریوها باید از خود برد خوانده شوند.
    if(enteringDifferentTab) loadSettings(true);
    else if(!settingsEverLoaded) loadSettings();
    else renderScenarios();
    return;
  }
  if(scenarioEditMode) scenarioEditMode=false;
  if(tabId==='wifi' && enteringDifferentTab){
    // فرم تنظیمات نیز در هر ورود با آخرین وضعیت برد تازه می‌شود.
    loadSettings(true);
  }
}
function updatePagePositions(animate){ const pages=document.querySelectorAll('.page'); pages.forEach((p)=>{ const tabId=p.id.replace('tab-',''); const tabIndex=TABS.indexOf(tabId); const off=tabIndex-currentTabIndex; p.style.transition=animate?'transform .35s cubic-bezier(.25,.46,.45,.94)':'none'; p.style.transform='translateX('+(RTL*off*100)+'%)'; p.style.pointerEvents=tabIndex===currentTabIndex?'auto':'none'; }); }
function switchTab(tabId){ if(scenarioEditMode && scenariosDirty && tabId!=='scenarios'){ updatePagePositions(true); showConfirm('تغییرات ذخیره‌نشده‌ای در سناریوها دارید. آیا بدون ذخیره کردن تب را عوض می‌کنید؟',()=>{ scenariosDirty=false; _doSwitchTab(tabId); }); return; } if(settingsDirty && tabId!=='wifi'){ updatePagePositions(true); showConfirm('تغییرات ذخیره‌نشده‌ای در تنظیمات دارید. آیا بدون ذخیره کردن تب را عوض می‌کنید؟',()=>{ settingsDirty=false; _doSwitchTab(tabId); }); return; } _doSwitchTab(tabId); }
function updateScenariosManualNote(){ const note=document.getElementById('manual-disabled-note'); if(note) note.style.display=(currentStatus&&currentStatus.override===1)?'block':'none'; }
function updateDisconnectedDirtyNote(){ const isDisconnected=connectionFailCount>=CONNECTION_FAIL_THRESHOLD; const dirtyNote=document.getElementById('disconnected-dirty-note'); const note=document.getElementById('disconnected-note'); if(dirtyNote) dirtyNote.style.display=(isDisconnected&&scenariosDirty)?'block':'none'; if(note) note.style.display=(isDisconnected&&!scenariosDirty)?'block':'none'; }
function tryReconnect(){ maybeAskWifiAssist(true); fetchStatus(); if(!scenariosDirty) loadSettings(); showToast('در حال تلاش برای اتصال مجدد...','warn'); }
function pad2(n){ return String(n).padStart(2,'0'); }
function isInt(v,min,max){ return Number.isInteger(v)&&v>=min&&v<=max; }
function gregorianToJalali(gy, gm, gd){ const gdm=[0,31,59,90,120,151,181,212,243,273,304,334]; let gy2=(gm>2)?gy+1:gy; let days=355666+365*gy+Math.floor((gy2+3)/4)-Math.floor((gy2+99)/100)+Math.floor((gy2+399)/400)+gd+gdm[gm-1]; let jy=-1595+33*Math.floor(days/12053); days%=12053; jy+=4*Math.floor(days/1461); days%=1461; if(days>365){ jy+=Math.floor((days-1)/365); days=(days-1)%365; } let jm,jd; if(days<186){ jm=1+Math.floor(days/31); jd=1+(days%31); } else { jm=7+Math.floor((days-186)/30); jd=1+((days-186)%30); } return [jy,jm,jd]; }
function formatJalaliDate(gy,gm,gd){ const j=gregorianToJalali(gy,gm,gd); return j[0]+'/'+pad2(j[1])+'/'+pad2(j[2]); }
function formatDuration(totalSeconds){ if(totalSeconds==null||totalSeconds<0) return '--'; const days=Math.floor(totalSeconds/86400), hours=Math.floor((totalSeconds%86400)/3600), minutes=Math.floor((totalSeconds%3600)/60); if(days>0) return days+' روز و '+hours+' ساعت'; if(hours>0) return hours+' ساعت و '+minutes+' دقیقه'; if(minutes>0) return minutes+' دقیقه'; return totalSeconds+' ثانیه'; }
function formatNtpSuccessStamp(data){ if(!data||data.ntpLastValid!==1) return 'هنوز دریافت نشده'; const j=gregorianToJalali(data.ntpYear,data.ntpMonth,data.ntpDay); return j[0]+'/'+pad2(j[1])+'/'+pad2(j[2])+' - '+pad2(data.ntpHour)+':'+pad2(data.ntpMinute)+':'+pad2(data.ntpSecond); }
function buildPowerButtons(level){ const labels=['کم','متوسط','زیاد','حداکثر']; const row=document.getElementById('tx-power-row'); row.innerHTML=''; labels.forEach((label,i)=>{ const b=document.createElement('button'); b.type='button'; b.className='power-btn'+(i===level?' selected':''); b.dataset.power=i; b.onclick=()=>selectTxPower(i); b.textContent=label; row.appendChild(b); }); document.getElementById('tx-power-value').value=level; }
function selectTxPower(level){ document.getElementById('tx-power-value').value=level; document.querySelectorAll('.power-btn').forEach(btn=>btn.classList.toggle('selected',parseInt(btn.dataset.power,10)===level)); }
function toggleNet(sw){ const cb=document.getElementById('internet-enabled'); cb.checked=!cb.checked; sw.classList.toggle('on',cb.checked); }
function toggleApCycleSwitch(sw){ const cb=document.getElementById('ap-cycle-enabled'); cb.checked=!cb.checked; sw.classList.toggle('on',cb.checked); }
function setSwitch(id,on){ const sw=document.getElementById(id); if(sw) sw.classList.toggle('on',!!on); }
function scenarioListMessage(text, type){ const list=document.getElementById('scenarios-list'); if(!list) return; if(scenariosDirty && scenarios.length>0) return; const color=type==='error'?'var(--off)':(type==='empty'?'var(--text-dim)':'var(--warn)'); const border=type==='error'?'var(--off)':(type==='empty'?'var(--line-strong)':'var(--warn)'); const bg=type==='error'?'var(--off-dim)':(type==='empty'?'var(--panel-2)':'var(--warn-dim)'); list.innerHTML=`<div class='scenario-banner' style='background:${bg};border-color:${border};color:${color};'>${text}</div>`; }
function renderScenarios(sortList=true){ const list=document.getElementById('scenarios-list'); if(!list) return; if(!scenariosDirty && (settingsLoading || !settingsEverLoaded)){ scenarioListMessage('در حال دریافت سناریوها از برد... لطفاً چند لحظه صبر کنید.','loading'); return; } if(scenarios.length===0){ scenarioListMessage('هیچ سناریویی روی برد ذخیره نشده است. برای افزودن برنامه جدید، دکمه + را بزنید.','empty'); return; } list.innerHTML=''; if(sortList) scenarios.sort((a,b)=>(a.sh*60+a.sm)-(b.sh*60+b.sm)); scenarios.forEach((sc,i)=>{ const card=document.createElement('div'); card.className='scenario-card'+(sc.en===false?' scenario-disabled':'')+(sc._dirty&&!sc._isNew?' scenario-dirty':'')+(sc._isNew?' scenario-new':''); card.id='row_'+i; const switchClass=sc.en===false?'switch':'switch on'; const badgeHTML=sc._isNew?'<span class="badge-new">جدید</span>':(sc._dirty?'<span class="badge-dirty">تغییر یافته</span>':''); card.innerHTML=`<div class='scenario-top'><div class='scenario-name'>سناریو ${i+1} ${badgeHTML}</div><div class='scenario-actions'><div class='${switchClass}' onclick='toggleScenarioEnabled(${i})'><div class='knob'></div></div><div class='icon-btn danger' onclick='removeScenario(${i})' title='حذف سناریو'>✕</div></div></div><div class='time-row'><div class='time-box' onclick='openTimePicker(${i},true)'><div class='lbl'>روشن</div><div class='time-display'>${pad2(sc.sh)}:${pad2(sc.sm)}</div></div><div class='time-box' onclick='openTimePicker(${i},false)'><div class='lbl'>خاموش</div><div class='time-display'>${pad2(sc.eh)}:${pad2(sc.em)}</div></div></div><div class='days-label'>روزهای اجرا</div><div class='days-row'></div>`; const days=card.querySelector('.days-row'); ['ش','ی','د','س','چ','پ','ج'].forEach((name,d)=>{ const b=document.createElement('button'); b.type='button'; b.className='day-btn'+((sc.wd&(1<<d))?' selected':''); b.textContent=name; b.onclick=()=>toggleScenarioDay(i,d); days.appendChild(b); }); list.appendChild(card); }); highlightScenarios(currentStatus?currentStatus.time:null,currentStatus?currentStatus.weekday:0); }
function markDirty(){ if(scenariosDirty) return; scenariosDirty=true; document.querySelectorAll('.scenario-card.running,.scenario-card.next').forEach(c=>{c.classList.remove('running','next');const nm=c.querySelector('.scenario-name');if(nm){const b=nm.querySelector('.badge-live,.badge-next');if(b)b.remove();}}); const btn=document.querySelector('.save-btn'); if(btn){ btn.style.background='var(--warn)'; btn.style.color='#000'; } updateDisconnectedDirtyNote(); }
function clearDirty(){ scenariosDirty=false; scenarios.forEach(s=>delete s._dirty); const btn=document.querySelector('.save-btn'); if(btn){ btn.style.background=''; btn.style.color=''; } renderScenarios(); updateDisconnectedDirtyNote(); if(currentStatus&&currentStatus.sync===1&&currentStatus.override===0) highlightScenarios(currentStatus.time,currentStatus.weekday); }
function markSettingsDirty(){ settingsDirty=true; }
function clearSettingsDirty(){ settingsDirty=false; }
function showConfirm(msg,onYes){ if(activeDialog) return; activeDialog='confirm'; _confirmCallback=onYes; document.getElementById('custom-confirm-text').innerText=msg; document.getElementById('custom-alert-overlay').style.display='block'; document.getElementById('custom-confirm').style.display='block'; }
function closeConfirm(yes){ document.getElementById('custom-confirm').style.display='none'; document.getElementById('custom-alert-overlay').style.display='none'; const callback=_confirmCallback; _confirmCallback=null; activeDialog=null; if(yes && callback) callback(); }
function refreshScenariosFromBoard(){ if(!scenariosDirty){ loadSettings(); return; } showConfirm('تغییرات ذخیره‌نشده‌ای دارید. با دریافت مجدد از برد، تغییرات فعلی پاک می‌شود. ادامه می‌دهید؟',()=>{ scenariosDirty=false; loadSettings(); }); }
function addScenario(){ if(settingsLoading && !scenariosDirty){ showModal('در حال دریافت آخرین سناریوها از برد هستیم؛ چند لحظه صبر کنید.'); return; } if(scenarios.length>=20){ showModal('حداکثر ظرفیت مجاز (۲۰ سناریو) را اضافه کرده‌اید.'); return; } scenarios.unshift({sh:0,sm:0,eh:1,em:0,en:true,wd:127,_isNew:true,_dirty:true}); renderScenarios(false); markDirty(); setTimeout(()=>{ const newCard=document.querySelector('.scenario-card.scenario-new'); if(newCard) newCard.scrollIntoView({behavior:'smooth',block:'center'}); },120); setTimeout(()=>{ scenarios.forEach(s=>s._isNew=false); document.querySelectorAll('.scenario-card.scenario-new').forEach(card=>{ card.classList.remove('scenario-new'); const badge=card.querySelector('.badge-new'); if(badge) badge.remove(); }); },5000); }
function removeScenario(i){ showConfirm('آیا از حذف سناریو '+(i+1)+' مطمئن هستید؟',()=>{ scenarios.splice(i,1); renderScenarios(false); markDirty(); }); }
function formatTimeTyping(input){ let v=input.value.replace(/[^0-9]/g,'').slice(0,4); if(v.length>=3) v=v.slice(0,2)+':'+v.slice(2); input.value=v; }
function normalizeManualTime(val){ if(!val) return null; let v=String(val).trim(); if(/^\d{1,2}:\d{1,2}$/.test(v)){ const p=v.split(':').map(Number); if(p[0]>=0&&p[0]<=23&&p[1]>=0&&p[1]<=59) return {h:p[0],m:p[1]}; return null; } v=v.replace(/[^0-9]/g,''); if(v.length===3) v='0'+v; if(v.length!==4) return null; const h=Number(v.slice(0,2)), m=Number(v.slice(2)); if(h<0||h>23||m<0||m>59) return null; return {h:h,m:m}; }
function updateScenarioTime(i,start,val){ const parsed=normalizeManualTime(val); if(!parsed){showModal('زمان باید با فرمت ۲۴ ساعته وارد شود، مثل 08:30 یا 23:45');return;} if(start){scenarios[i].sh=parsed.h;scenarios[i].sm=parsed.m;}else{scenarios[i].eh=parsed.h;scenarios[i].em=parsed.m;} scenarios[i]._dirty=true; markDirty(); }
function toggleScenarioEnabled(i){ scenarios[i]._dirty=true; scenarios[i].en=!(scenarios[i].en!==false); const row=document.getElementById('row_'+i); if(row){ row.classList.toggle('scenario-disabled',scenarios[i].en===false); row.classList.toggle('scenario-dirty',scenarios[i]._dirty&&!scenarios[i]._isNew); const sw=row.querySelector('.switch'); if(sw) sw.classList.toggle('on',scenarios[i].en!==false); } markDirty(); if(currentStatus&&currentStatus.sync===1&&currentStatus.override===0) highlightScenarios(currentStatus.time,currentStatus.weekday); }
function toggleScenarioDay(i,day){ scenarios[i]._dirty=true; let mask=scenarios[i].wd||127; const bit=1<<day; if(mask&bit){ if((mask&127)===bit){ showModal('حداقل یک روز برای اجرای سناریو انتخاب کنید.'); return; } mask&=~bit; } else mask|=bit; scenarios[i].wd=mask; const row=document.getElementById('row_'+i); if(row){ const btn=row.querySelectorAll('.day-btn')[day]; if(btn) btn.classList.toggle('selected',(mask&bit)!==0); row.classList.toggle('scenario-dirty',scenarios[i]._dirty&&!scenarios[i]._isNew); } markDirty(); if(currentStatus&&currentStatus.sync===1&&currentStatus.override===0) highlightScenarios(currentStatus.time,currentStatus.weekday); }
function scenarioRowsToData(){ return scenarios.map(x=>({sh:x.sh,sm:x.sm,eh:x.eh,em:x.em,en:x.en!==false,wd:x.wd||127})); }
function validateScenarios(){ document.querySelectorAll('.scenario-card').forEach(r=>r.classList.remove('error-conflict')); let active=[]; for(let i=0;i<scenarios.length;i++){ const s=scenarios[i]; if(!isInt(s.sh,0,23)||!isInt(s.sm,0,59)||!isInt(s.eh,0,23)||!isInt(s.em,0,59)){ showModal('لطفاً زمان‌های سناریوها را به‌درستی وارد کنید.'); return false; } const st=s.sh*60+s.sm,en=s.eh*60+s.em; if(st===en){ markErr(i); showModal('در یکی از سناریوها زمان روشن و خاموش شدن یکسان است. این کار مجاز نیست.'); return false; } if(s.en!==false) active.push({start:st,end:en,id:i}); } let timeMap=new Array(1440).fill(null), conflicted=new Set(); active.forEach(s=>{ let mins=[]; if(s.start<s.end){for(let m=s.start;m<s.end;m++) mins.push(m);} else {for(let m=s.start;m<1440;m++) mins.push(m); for(let m=0;m<s.end;m++) mins.push(m);} mins.forEach(m=>{ if(timeMap[m]!==null){ conflicted.add(s.id); conflicted.add(timeMap[m]); } else timeMap[m]=s.id; }); }); if(conflicted.size){ conflicted.forEach(markErr); showModal('تداخل زمانی! بازه‌های سناریوها روی هم افتاده‌اند. لطفا زمان سناریوهای مشخص شده با کادر قرمز رنگ را اصلاح کنید.'); return false; } return true; }
function markErr(i){ const r=document.getElementById('row_'+i); if(r) r.classList.add('error-conflict'); }
function exportScenarios(){ const backup={version:1,type:'esp32-cooler-scenarios',exportedAt:new Date().toISOString(),scenarios:scenarioRowsToData()}; const txt=JSON.stringify(backup,null,2); if(window.FlutterExport){ FlutterExport.postMessage(txt); } else { const blob=new Blob([txt],{type:'application/json'}); const url=URL.createObjectURL(blob), a=document.createElement('a'); a.href=url; a.download='cooler-scenarios.json'; document.body.appendChild(a); a.click(); a.remove(); URL.revokeObjectURL(url); } }
function importScenarios(){ if(window.FlutterImport){ FlutterImport.postMessage('pick'); } else { showModal('انتخاب فایل فقط داخل اپ اندروید فعال است.'); } }
window.receiveScenarioImport=function(text){ try{ const parsed=JSON.parse(text); const data=Array.isArray(parsed)?parsed:parsed.scenarios; if(!Array.isArray(data)||data.length>20) throw new Error('format'); data.forEach(x=>{ if(!Number.isInteger(x.sh)||!Number.isInteger(x.sm)||!Number.isInteger(x.eh)||!Number.isInteger(x.em)||x.sh<0||x.sh>23||x.eh<0||x.eh>23||x.sm<0||x.sm>59||x.em<0||x.em>59||((x.sh*60+x.sm)===(x.eh*60+x.em))) throw new Error('data'); }); scenarios=data.map(x=>({sh:x.sh,sm:x.sm,eh:x.eh,em:x.em,en:x.en!==false,wd:Number.isInteger(x.wd)&&x.wd>=1&&x.wd<=127?x.wd:127,_dirty:true})); renderScenarios(); markDirty(); showModal('سناریوها از فایل خوانده شدند. برای اعمال روی برد، دکمه «ذخیره و پیاده‌سازی برنامه‌ها» را بزنید.'); }catch(e){ showModal('فایل پشتیبان معتبر نیست یا با این برنامه سازگار نیست.'); } }
function loadSettings(force=false){
  // refresh خودکار نباید فرم در حال ویرایش کاربر را overwrite کند.
  if(settingsDirty && !force) return Promise.resolve(false);
  const generation=++settingsLoadGeneration;
  settingsLoading=true;
  if(!scenariosDirty) scenarioListMessage('در حال دریافت تنظیمات و سناریوها از برد...','loading');
  const ctrl=new AbortController();
  const tid=setTimeout(()=>ctrl.abort(),5000);
  return apiFetch('/settings?t='+Date.now(),{signal:ctrl.signal}).then(r=>{
    clearTimeout(tid);
    if(!r.ok) throw new Error('HTTP '+r.status);
    return r.json();
  }).then(s=>{
    if(generation!==settingsLoadGeneration) return false;
    const preserveForms=settingsDirty;
    document.getElementById('brand-name').textContent=s.programName||'کولر هوشمند ESP32';
    document.getElementById('brand-sub').textContent=s.programTagline||'ESP32 · TIMER HUB';
    const ap=s.ap||{}, sta=s.sta||{}, pr=s.protection||{};

    if(!preserveForms){
      if(ap.ssid) localStorage.setItem('esp32ApSsid',ap.ssid);
      if(ap.password) localStorage.setItem('esp32ApPass',ap.password);
      document.getElementById('ap-ssid').value=ap.ssid||'';
      document.getElementById('wifi-pass').value=ap.password||'';
      document.getElementById('ap-cycle-enabled').checked=!!ap.cycleEnabled;
      setSwitch('ap-cycle-switch',!!ap.cycleEnabled);
      document.getElementById('ap-on-minutes').value=ap.onMinutes??10;
      document.getElementById('ap-off-minutes').value=ap.offMinutes??5;
      buildPowerButtons(Number.isInteger(ap.txPowerLevel)?ap.txPowerLevel:3);
      document.getElementById('internet-enabled').checked=!!sta.internetEnabled;
      setSwitch('internet-switch',!!sta.internetEnabled);
      document.getElementById('sta-ssid').value=sta.ssid||'';
      document.getElementById('sta-pass').value=sta.password||'';
      document.getElementById('sta-on-minutes').value=sta.onMinutes??10;
      document.getElementById('sta-off-minutes').value=sta.offMinutes??0;
      document.getElementById('min-off').value=pr.minOffMinutes??3;
      clearSettingsDirty();
    }

    if(scenariosDirty){
      settingsLoading=false;
      settingsEverLoaded=true;
      lastSettingsLoadTime=new Date();
      updateRefreshTimeLabel();
      if(preserveForms) showToast('تنظیمات برد دریافت شد؛ تغییرات فرم شما حفظ شدند.','good');
      else showToast('تنظیمات به‌روز شد، اما سناریوهای محلی حفظ شدند','good');
    }else{
      scenarios=(s.scenarios||[]).map(x=>({sh:x.sh,sm:x.sm,eh:x.eh,em:x.em,en:x.en!==false,wd:x.wd||127}));
      settingsLoading=false;
      settingsEverLoaded=true;
      if(!scenariosDirty) clearDirty();
      lastSettingsLoadTime=new Date();
      updateRefreshTimeLabel();
    }
    return true;
  }).catch(e=>{
    clearTimeout(tid);
    if(generation!==settingsLoadGeneration) return false;
    settingsLoading=false;
    if(!settingsEverLoaded && scenarios.length===0){
      scenarioListMessage('دریافت سناریوها از برد ناموفق بود. اتصال به برد یا آدرس API را بررسی کنید.','error');
    }else if(scenarios.length>0){
      renderScenarios();
    }
    showToast('خطا در دریافت تنظیمات/سناریوها از برد','bad');
    return false;
  });
}
function syncTime(){ let now=new Date(); const wd=(now.getDay()+1)%7; runBoardOperation({title:'همگام‌سازی ساعت',path:'/sync',payload:{h:now.getHours(),m:now.getMinutes(),s:now.getSeconds(),y:now.getFullYear(),mon:now.getMonth()+1,d:now.getDate(),wd},successMessage:'ساعت داخلی دستگاه با موفقیت همگام‌سازی شد.',onSuccess:()=>fetchStatus()}); }
function toggleManual(){
  const currentlyManual=currentStatus && (currentStatus.override===1 || currentStatus.override===true);
  const targetOverride=currentlyManual?0:1;
  runBoardOperation({title:'فرمان دستی کولر',path:'/toggle-manual',payload:{override:targetOverride},successMessage:'فرمان دستی با موفقیت روی برد اعمال شد.',onSuccess:()=>fetchStatus()});
}
function saveScenariosForm(event){ event.preventDefault(); if(!validateScenarios()) return false; const valid=scenarioRowsToData().sort((a,b)=>(a.sh*60+a.sm)-(b.sh*60+b.sm)); runBoardOperation({title:'ذخیره سناریوها',path:'/save',payload:valid,successMessage:'سناریوها با موفقیت روی برد ذخیره شدند.',closeDelay:1800,onSuccess:()=>{clearDirty(); loadSettings(); fetchStatus();}}); return false; }
function saveApForm(e){ e.preventDefault(); const ssid=document.getElementById('ap-ssid').value.trim(), pass=document.getElementById('wifi-pass').value; if(!ssid||pass.length<8){ showModal('رمز شبکه (AP) خود برد باید حداقل ۸ کاراکتر باشد.'); return false; } runBoardOperation({title:'ذخیره تنظیمات فرستنده',path:'/save-ap',payload:{ssid,pass},successMessage:'تنظیمات فرستنده ذخیره شد. برد در حال راه‌اندازی مجدد است.',closeDelay:2500,onSuccess:()=>{ localStorage.setItem('esp32ApSsid',ssid); localStorage.setItem('esp32ApPass',pass); waitingWifiReboot=true; wifiRebootSeenDisconnect=false; clearSettingsDirty(); showToast('مشخصات جدید AP ذخیره شد؛ در حال اتصال دوباره به برد...','warn'); setTimeout(()=>{ if(waitingWifiReboot) maybeAskWifiAssist(true); },3500); }}); return false; }
function saveStaForm(e){ e.preventDefault(); const pass=document.getElementById('sta-pass').value; const on=Number(document.getElementById('sta-on-minutes').value), off=Number(document.getElementById('sta-off-minutes').value); if(pass.length>0&&pass.length<8){ showModal('رمز وای‌فای مودم اگر وارد شود باید حداقل ۸ کاراکتر باشد.'); return false; } if(!isInt(on,1,1440)||!isInt(off,0,1440)){ showModal('زمان روشن بودن STA باید ۱ تا ۱۴۴۰ و زمان خاموش بودن ۰ تا ۱۴۴۰ دقیقه باشد.'); return false; } runBoardOperation({title:'ذخیره تنظیمات مودم / اینترنت',path:'/save-sta',payload:{internet:document.getElementById('internet-enabled').checked,sta_ssid:document.getElementById('sta-ssid').value.trim(),sta_pass:pass,sta_on_minutes:on,sta_off_minutes:off},successMessage:'تنظیمات مودم / اینترنت با موفقیت ذخیره شد.',onSuccess:()=>{clearSettingsDirty(); fetchStatus();}}); return false; }
function saveProtectionForm(e){ e.preventDefault(); const value=Number(document.getElementById('min-off').value); if(!isInt(value,0,1440)){ showModal('زمان محافظت باید یک عدد صحیح بین ۰ تا ۱۴۴۰ دقیقه باشد.'); return false; } runBoardOperation({title:'ذخیره محافظت کمپرسور',path:'/save-protection',payload:{min_off:value},successMessage:'تنظیمات محافظت کمپرسور ذخیره شد.',onSuccess:()=>{clearSettingsDirty(); fetchStatus();}}); return false; }
function saveApCycleForm(e){ e.preventDefault(); const on=Number(document.getElementById('ap-on-minutes').value), off=Number(document.getElementById('ap-off-minutes').value), power=Number(document.getElementById('tx-power-value').value); if(!isInt(on,1,1440)||!isInt(off,1,1440)||!isInt(power,0,3)){ showModal('مدت روشن/خاموش یا قدرت سیگنال معتبر نیست.'); return false; } runBoardOperation({title:'ذخیره چرخه AP',path:'/save-ap-cycle',payload:{cycle_enabled:document.getElementById('ap-cycle-enabled').checked,on_minutes:on,off_minutes:off,tx_power:power},successMessage:'تنظیمات چرخه AP ذخیره شد.',onSuccess:()=>{clearSettingsDirty(); fetchStatus();}}); return false; }
function highlightScenarios(boardTimeStr,currentWeekday){ if(!boardTimeStr||scenariosDirty) return; const parts=boardTimeStr.split(':'); if(parts.length<2) return; const cur=parseInt(parts[0])*60+parseInt(parts[1]); document.querySelectorAll('.scenario-card').forEach(c=>{ const isRunning=c.classList.contains('running'), isNext=c.classList.contains('next'); if(!isRunning && !isNext) return; c.classList.remove('running','next'); const nm=c.querySelector('.scenario-name'); if(nm){ const old=nm.querySelector('.badge-live,.badge-next'); if(old) old.remove(); }}); let any=false,next=null,minDiff=Infinity; scenarios.forEach((d,i)=>{ if(d.en===false) return; const s=d.sh*60+d.sm,e=d.eh*60+d.em; let running=s<e?(cur>=s&&cur<e):(cur>=s||cur<e); let scenarioDay=(s>e&&cur<e)?((currentWeekday+6)%7):currentWeekday; running=running&&((d.wd&(1<<scenarioDay))!==0); const card=document.getElementById('row_'+i); if(!card) return; if(running){ card.classList.add('running'); any=true; const b=document.createElement('span'); b.className='badge-live'; b.textContent='در حال اجرا'; card.querySelector('.scenario-name').appendChild(b); } else { let diff=s-cur; if(diff<0) diff+=1440; if(diff<minDiff){minDiff=diff; next=card;} }}); if(!any&&next){ next.classList.add('next'); const b=document.createElement('span'); b.className='badge-next'; b.textContent='بعدی'; next.querySelector('.scenario-name').appendChild(b); } }
function requestWifiSignal(){ const now=Date.now(); if(now-lastSignalRequestAt<5000) return; lastSignalRequestAt=now; if(window.FlutterWifi){ FlutterWifi.postMessage(JSON.stringify({action:'signal'})); } }
window.updateWifiSignal=function(text){ wifiSignalText=text||''; const badge=document.getElementById('connection-status'); if(badge && badge.classList.contains('connected')){ const ping=badge.dataset.ping||''; badge.innerHTML='<span class="dot"></span> متصل'+(ping?' · '+ping+'ms':'')+(wifiSignalText?' · '+wifiSignalText:''); } }
function maybeAskWifiAssist(force){ const now=Date.now(); const autoApproved=localStorage.getItem('esp32WifiAutoApproved')==='1'; if(!force && !autoApproved) return; if(!force && now-lastWifiAssistAt<15000) return; lastWifiAssistAt=now; const ssid=localStorage.getItem('esp32ApSsid')||'ESP32_Timer_Hub'; const pass=localStorage.getItem('esp32ApPass')||'12345678'; if(window.FlutterWifi){ FlutterWifi.postMessage(JSON.stringify({action:'connect',ssid:ssid,pass:pass,apiBase:apiBase,force:!!force,autoApproved:autoApproved})); } else if(force){ showModal('قابلیت اتصال خودکار WiFi فقط داخل اپ اندروید فعال است.'); } }
function fetchStatus(){ if(document.hidden){ queueNextPing(); return; } if(statusFetchInFlight || _statusMutex) return; _statusMutex = true; statusFetchInFlight=true; const reqStart=Date.now(); const controller=new AbortController(); const timeoutId=setTimeout(()=>controller.abort(),2500); apiFetch('/status?t='+reqStart,{signal:controller.signal}).then(r=>{ clearTimeout(timeoutId); if(!r.ok) throw new Error(); return r.json(); }).then(data=>{ currentStatus=data; if(waitingWifiReboot && wifiRebootSeenDisconnect){ waitingWifiReboot=false; wifiRebootSeenDisconnect=false; } const ping=Date.now()-reqStart; connectionFailCount=0; initialConnectionPhase=false; everConnected=true; if(wasDisconnected){wasDisconnected=false;showToast('✅ اتصال مجدد برقرار شد','good');loadSettings();} updateDisconnectedDirtyNote(); const badge=document.getElementById('connection-status'); badge.className='conn-badge connected'; badge.dataset.ping=String(ping); badge.innerHTML='<span class="dot"></span> متصل · '+ping+'ms'+(wifiSignalText?' · '+wifiSignalText:''); requestWifiSignal(); if(activeOperation && !activeOperation.inFlight) trySendActiveOperation(); document.getElementById('board-time').innerText=data.time; const dayNames=['شنبه','یکشنبه','دوشنبه','سه‌شنبه','چهارشنبه','پنجشنبه','جمعه']; document.getElementById('board-date').innerText='امروز: '+dayNames[data.weekday]+' — '+formatJalaliDate(data.year,data.month,data.day); const sta=staTexts(data); setTextClass('internet-wifi-status',sta.text,sta.cls); setTextClass('sta-status-inline',sta.text,sta.cls); const stacycle=staCycleText(data); setTextClass('sta-cycle-status',stacycle.text,stacycle.cls); setTextClass('sta-cycle-status-inline',stacycle.text,stacycle.cls); const im=internetModeText(data); setTextClass('internet-mode-status',im.text,im.cls); setTextClass('internet-mode-inline',im.text,im.cls); const ntp=formatNtpSuccessStamp(data); document.getElementById('ntp-last-update').innerText=ntp; document.getElementById('ntp-last-update-inline').innerText=ntp; document.getElementById('ntp-last-update').style.color=data.ntpLastValid===1?'var(--scenario-live)':'var(--warn)'; document.getElementById('ntp-last-update-inline').style.color=data.ntpLastValid===1?'var(--scenario-live)':'var(--warn)'; const apEl=document.getElementById('ap-cycle-status'); if(apEl){ if(!data.apCycleEnabled){ apEl.textContent='غیرفعال — فرستنده همیشه روشن است'; apEl.style.color='var(--text-dim)'; } else if(data.apOn&&data.apClientConnected){ apEl.textContent='روشن — دستگاهی متصل است، چرخه متوقف مانده'; apEl.style.color='var(--scenario-live)'; } else if(data.apOn){ apEl.textContent='روشن — '+Math.ceil(data.apRemaining/60)+' دقیقه تا خاموش‌شدن'; apEl.style.color='var(--scenario-live)'; } else { apEl.textContent='در حال خاموش بودن طبق چرخه'; apEl.style.color='var(--warn)'; } } const pr=document.getElementById('protection-status'); if(pr){ if(data.protectionRemaining>0){pr.textContent='فعال — '+Math.ceil(data.protectionRemaining/60)+' دقیقه تا اجازه روشن‌شدن'; pr.style.color='var(--warn)';} else if(data.protectionMinutes>0){pr.textContent='فعال — فاصله تنظیم‌شده: '+data.protectionMinutes+' دقیقه'; pr.style.color='var(--scenario-live)';} else {pr.textContent='غیرفعال'; pr.style.color='var(--text-dim)';} } document.getElementById('relay-switch-count').innerText=data.switchCount+' بار'; document.getElementById('relay-on-duration').innerText=formatDuration(data.onSeconds); const syncWarning=document.getElementById('sync-warning'); if(data.sync===1){syncWarning.innerText='ساعت همگام‌سازی شده و سناریوها فعال هستند.'; syncWarning.style.color='var(--on)';} else {syncWarning.innerText='⚠️ ساعت برد همگام نیست! سناریوها تا زمان همگام‌سازی اجرا نخواهند شد.'; syncWarning.style.color='var(--off)';} const hero=document.getElementById('cooler-display-status'), statusText=document.getElementById('cooler-status-text'), statusSub=document.getElementById('cooler-status-sub'), manualBtn=document.getElementById('manual-btn'); if(data.relay===1){ hero.className='panel hero rivets on'; statusText.innerText='کولر روشن است'; } else { hero.className='panel hero rivets off'; statusText.innerText='کولر خاموش است'; } statusSub.innerText=data.protectionRemaining>0?'محافظت کمپرسور: '+Math.ceil(data.protectionRemaining/60)+' دقیقه تا روشن‌شدن':(data.override===1?'حالت: دستی (روشن)':'حالت: خودکار (سناریو)'); if(data.override===1){manualBtn.classList.remove('isoff'); manualBtn.innerText='خاموش کردن دستی کولر';} else {manualBtn.classList.add('isoff'); manualBtn.innerText='روشن کردن دستی کولر';} updateScenariosManualNote(); const scenariosVisible=TABS[currentTabIndex]==='scenarios'; if(scenariosVisible&&data.sync===1&&data.override===0) highlightScenarios(data.time,data.weekday); else if(scenariosVisible) document.querySelectorAll('.scenario-card').forEach(r=>r.classList.remove('running','next')); if(lastStaState!==null&&lastStaState!==data.staState&&data.internetEnabled===1&&data.staConfigured===1){ if(data.staState===3) showToast('⚠️ اتصال STA به مودم اینترنت قطع شد','bad'); else if(data.staState===2) showToast('✅ اتصال STA به مودم برقرار شد','good'); } lastStaState=data.staState; statusFetchInFlight=false; _statusMutex=false; queueNextPing(); }).catch(()=>{ statusFetchInFlight=false; _statusMutex=false; if(waitingWifiReboot) wifiRebootSeenDisconnect=true; connectionFailCount++; const badge=document.getElementById('connection-status'); if(connectionFailCount<CONNECTION_FAIL_THRESHOLD){ if(initialConnectionPhase && !everConnected){ badge.className='conn-badge connecting'; badge.innerHTML='<span class="dot"></span> تلاش برای اتصال '+connectionFailCount+'/'+CONNECTION_FAIL_THRESHOLD; } maybeAskWifiAssist(false); queueNextPing(); return; } initialConnectionPhase=false; badge.className='conn-badge disconnected'; badge.innerHTML='<span class="dot"></span> قطع ارتباط!'; wasDisconnected=true; if(scenariosDirty) showToast('⚠️ ارتباط با برد قطع شد، اما تغییرات شما حفظ شده‌اند. پس از اتصال مجدد ذخیره کنید.','warn'); const hero=document.getElementById('cooler-display-status'); hero.className='panel hero rivets disconnected'; document.getElementById('cooler-status-text').innerText='ارتباط با برد قطع شده است'; const sub=document.getElementById('cooler-status-sub'); if(sub){ sub.innerText=waitingWifiReboot?'برد در حال راه‌اندازی مجدد با مشخصات WiFi جدید است؛ اپ برای اتصال دوباره تلاش می‌کند.':(localStorage.getItem('esp32WifiAutoApproved')==='1'?'اتصال خودکار فعال است؛ اپ همچنان برای اتصال دوباره تلاش می‌کند. برای تغییر تنظیمات، روی نشانگر اتصال بالای صفحه بزنید.':'برای تنظیم اتصال خودکار، روی نشانگر اتصال بالای صفحه بزنید.'); } updateDisconnectedDirtyNote(); maybeAskWifiAssist(false); queueNextPing(); }); }
function setTextClass(id,text,cls){ const el=document.getElementById(id); if(el){el.textContent=text; el.className=cls;} }
function staTexts(data){ if(data.internetEnabled!==1) return {text:'غیرفعال چون اینترنت خاموش است',cls:'status-connecting'}; if(data.staConfigured!==1) return {text:'تنظیم نشده',cls:'status-connecting'}; if(data.staState===4) return {text:'طبق زمان‌بندی موقتاً قطع است',cls:'status-connecting'}; if(data.staState===2) return {text:'متصل - '+data.staIp,cls:'status-connected'}; if(data.staState===3) return {text:'⚠️ قطع شده - در حال تلاش مجدد...',cls:'status-disconnected'}; return {text:'در حال اتصال...',cls:'status-connecting'}; }
function staCycleText(data){ if(data.internetEnabled!==1) return {text:'غیرفعال',cls:'status-connecting'}; if(data.staConfigured!==1) return {text:'تا قبل از وارد کردن SSID مودم فعال نمی‌شود',cls:'status-connecting'}; if(data.staOffMinutes===0) return {text:'دائم روشن (زمان خاموشی = ۰)',cls:'status-connected'}; if(data.staPhaseOn===1) return {text:'روشن — '+Math.ceil(data.staRemaining/60)+' دقیقه تا قطع دوره‌ای',cls:'status-connected'}; return {text:'خاموش — '+Math.ceil(data.staRemaining/60)+' دقیقه تا وصل مجدد',cls:'status-connecting'}; }
function internetModeText(data){ if(data.internetEnabled===1){ if(data.staConfigured!==1) return {text:'فعال است، اما مودم تنظیم نشده',cls:'status-connecting'}; if(data.staOffMinutes===0) return {text:'فعال — اتصال مودم دائماً روشن است',cls:'status-connected'}; return {text:'فعال — طبق زمان‌بندی STA',cls:'status-connected'}; } return {text:'غیرفعال',cls:'status-connecting'}; }
function queueNextPing(){ if(pingTimeoutId) clearTimeout(pingTimeoutId); currentInterval=(Date.now()-lastActivity)>30000?3000:1000; pingTimeoutId=setTimeout(fetchStatus,currentInterval); }
function resetActivity(){ lastActivity=Date.now(); if(currentInterval!==1000){ currentInterval=1000; if(pingTimeoutId) clearTimeout(pingTimeoutId); fetchStatus(); } }
['click','touchstart','input','change','scroll'].forEach(evt=>document.addEventListener(evt,resetActivity,{passive:true}));
document.addEventListener('visibilitychange',()=>{ if(!document.hidden) resetActivity(); });
function updateRefreshTimeLabel(){ const el=document.getElementById('last-refresh-time'); if(!el) return; if(!lastSettingsLoadTime){ el.textContent='دریافت نشده'; return; } const h=String(lastSettingsLoadTime.getHours()).padStart(2,'0'), m=String(lastSettingsLoadTime.getMinutes()).padStart(2,'0'), s=String(lastSettingsLoadTime.getSeconds()).padStart(2,'0'); el.textContent='آخرین دریافت: '+h+':'+m+':'+s; }
buildPowerButtons(3); if(localStorage.getItem('esp32WifiAutoApproved')!=='1'){ setTimeout(()=>maybeAskWifiAssist(true),800); } document.getElementById('tab-wifi').addEventListener('input',()=>{markSettingsDirty();}); document.getElementById('tab-wifi').addEventListener('change',()=>{markSettingsDirty();}); document.getElementById('custom-confirm-yes').addEventListener('click',()=>closeConfirm(true)); (function(){ const sl=document.getElementById('scenarios-list'); if(!sl) return; sl.addEventListener('click',function(e){ const card=e.target.closest('.scenario-card'); if(!card) return; document.querySelectorAll('.scenario-card.scenario-active').forEach(c=>{ if(c!==card){ c.classList.remove('scenario-active'); const b=c.querySelector('.badge-editing'); if(b) b.remove(); } }); card.classList.add('scenario-active'); const name=card.querySelector('.scenario-name'); if(name&&!name.querySelector('.badge-editing')){ const b=document.createElement('span'); b.className='badge-editing'; b.textContent='ویرایش'; name.appendChild(b); } }); sl.addEventListener('focusin',function(e){ const card=e.target.closest('.scenario-card'); if(!card) return; document.querySelectorAll('.scenario-card.scenario-active').forEach(c=>{ if(c!==card){ c.classList.remove('scenario-active'); const b=c.querySelector('.badge-editing'); if(b) b.remove(); } }); card.classList.add('scenario-active'); const name=card.querySelector('.scenario-name'); if(name&&!name.querySelector('.badge-editing')){ const b=document.createElement('span'); b.className='badge-editing'; b.textContent='ویرایش'; name.appendChild(b); } }); sl.addEventListener('focusout',function(e){ const card=e.target.closest('.scenario-card'); if(!card) return; setTimeout(()=>{ if(!card.contains(document.activeElement)){ card.classList.remove('scenario-active'); const b=card.querySelector('.badge-editing'); if(b) b.remove(); } },120); }); })();
/* ===== Swipe / Drag بین تب‌ها ===== */
(function(){
  const pagesEl=document.querySelector('.pages');
  if(!pagesEl) return;
  function onTS(e){
    if(e.target.closest('#wheel-modal,#custom-alert,#custom-confirm,#api-modal,#progress-modal,#factory-reset-modal,#time-picker-modal')) return;
    _swipeStartX=e.touches[0].clientX; _swipeStartY=e.touches[0].clientY;
    _swipeDelta=0; _isSwiping=false; _isScrolling=false;
  }
  function onTM(e){
    if(_isScrolling) return;
    const dx=e.touches[0].clientX-_swipeStartX, dy=e.touches[0].clientY-_swipeStartY;
    if(!_isSwiping&&!_isScrolling){
      if(Math.abs(dy)>Math.abs(dx)+5){_isScrolling=true;return;}
      if(Math.abs(dx)>10) _isSwiping=true;
    }
    if(!_isSwiping) return;
    e.preventDefault();
    _swipeDelta=dx;
    // صفحه بعدی در سمت راست باید با کشیدن انگشت به راست به مرکز نزدیک شود.
    const cw=pagesEl.offsetWidth, pct=-RTL*(_swipeDelta/cw)*100;
    document.querySelectorAll('.page').forEach((p)=>{
      const tabId=p.id.replace('tab-','');
      const tabIndex=TABS.indexOf(tabId);
      const off=tabIndex-currentTabIndex;
      p.style.transition='none';
      p.style.transform='translateX('+(RTL*off*100+pct)+'%)';
    });
  }
  function onTE(){
    if(!_isSwiping) return;
    _isSwiping=false;
    const cw=pagesEl.offsetWidth, thr=cw*0.25;
    let ni=currentTabIndex;
    if(_swipeDelta>thr&&currentTabIndex<TABS.length-1) ni=currentTabIndex+1;
    else if(_swipeDelta<-thr&&currentTabIndex>0) ni=currentTabIndex-1;
    if(ni!==currentTabIndex) switchTab(TABS[ni]);
    else updatePagePositions(true);
  }
  pagesEl.addEventListener('touchstart',onTS,{passive:true});
  pagesEl.addEventListener('touchmove',onTM,{passive:false});
  pagesEl.addEventListener('touchend',onTE,{passive:true});
})();
updatePagePositions(false);
loadSettings(); fetchStatus();
</script>
</body>
