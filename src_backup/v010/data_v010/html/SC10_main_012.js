// SC10_main_012.js

(() => {
  "use strict";

  const $ = (sel, root=document)=>root.querySelector(sel);
  const text=(el,v)=>el&&(el.textContent=v);

  document.addEventListener("DOMContentLoaded",()=>{
    bindEvents();
    refreshVersion();
    refreshState(true);
  });

  // --------- 토스트 ---------
  function showToast(msg,type="ok"){
    const cont=$("#toastContainer");
    const div=document.createElement("div");
    div.className=`toast ${type}`;
    div.textContent=msg;
    cont.appendChild(div);
    setTimeout(()=>div.remove(),3000);
  }

  // --------- 로딩 ---------
  function setLoading(flag){
    const ov=$("#loadingOverlay");
    ov.style.display=flag?"flex":"none";
  }

  // --------- 프리셋 미리보기 ---------
  function previewPreset(){
    const preset=$('#preset').value;
    $('#presetPreview').textContent=`프리셋 미리보기: ${preset}`;
    showToast(`"${preset}" 프리셋 미리보기 적용`, "ok");
  }

  // --------- 이벤트 바인딩 ---------
  function bindEvents(){
    $('#btnRefresh').addEventListener('click',()=>refreshState(true));
    $('#lnkRefresh').addEventListener('click',(e)=>{e.preventDefault();refreshState(true)});
    $('#btnSaveConfig').addEventListener('click',saveConfig);
    $('#btnSaveWifi').addEventListener('click',saveConfig);
    $('#btnScan').addEventListener('click',scanNetworks);
    $('#btnPreviewPreset').addEventListener('click',previewPreset);
    $('#btnDiag').addEventListener('click',()=>fetchApi('/api/diag','GET',null,'진단 조회'));
    $('#btnLogs').addEventListener('click',()=>fetchApi('/api/logs','GET',null,'로그 조회'));
    $('#btnReboot').addEventListener('click',()=>fetchApi('/api/reboot','POST',null,'재부팅'));
    $('#btnReset').addEventListener('click',()=>fetchApi('/api/reset','POST',null,'공장 초기화'));
    $('#btnUpload').addEventListener('click',uploadStatic);
    $('#btnOTA').addEventListener('click',uploadOTA);
  }

  // --------- API 유틸 ---------
  async function fetchApi(url,method='GET',body=null,desc="작업"){
    setLoading(true);
    try{
      const opt={method,headers:{}};
      if(body){opt.body=JSON.stringify(body);opt.headers['Content-Type']='application/json';}
      const r=await fetch(url,opt);
      if(!r.ok) throw new Error(r.status);
      const txt=await r.text();
      showToast(`${desc} 성공`,"ok");
      if(url.includes("diag")) $('#diagOut').textContent=txt;
      if(url.includes("logs")) $('#logsOut').textContent=txt;
      return txt;
    }catch(e){
      showToast(`${desc} 실패: ${e.message}`,"err");
    }finally{setLoading(false);}
  }

  // --------- 상태 ---------
  async function refreshVersion(){
    try{
      const r=await fetch('/api/version'); const j=await r.json();
      text($('#fwVer'),j.fw_version);
    }catch{ text($('#fwVer'),'version?'); }
  }
  async function refreshState(){
    try{
      setLoading(true);
      const r=await fetch('/api/state'); const j=await r.json();
      text($('#simActive'),j.status.sim_active?'Active':'Idle');
      text($('#phase'),j.status.phase_name);
      text($('#wind'),j.status.wind_speed);
      text($('#pwm'),j.status.fan_pwm);
      text($('#wifiMode'),j.status.wifi_mode);
      text($('#curSsid'),j.status.ssid);
      text($('#ip'),j.status.ip_addr);
      // 프리셋 선택 채우기
      const sel=$('#preset'); sel.innerHTML='';
      j.presets.forEach(p=>{let o=document.createElement('option');o.value=p;o.textContent=p;sel.appendChild(o);});
    }catch{ showToast("상태 불러오기 실패","err"); }
    finally{ setLoading(false); }
  }

  // --------- 저장 ---------
  async function saveConfig(){
    const body={preset:$('#preset').value,intensity:Number($('#intensity').value)};
    await fetchApi('/api/config','POST',body,'설정 저장');
    refreshState();
  }

  // --------- 스캔 ---------
  async function scanNetworks(){
    const j=await fetchApi('/api/scan','GET',null,'Wi-Fi 스캔');
    if(j){
      try{const arr=JSON.parse(j); const sel=$('#scanList'); sel.innerHTML='';
        arr.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid;sel.appendChild(o);});
      }catch{}
    }
  }

  // --------- 업로드 ---------
  async function
