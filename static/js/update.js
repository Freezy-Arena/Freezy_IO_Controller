const token=document.querySelector('meta[name="update-token"]').content;
const form=document.getElementById('form'),file=document.getElementById('firmware'),button=document.getElementById('upload'),
status=document.getElementById('status'),progress=document.getElementById('progress');
form.onsubmit=e=>{
 e.preventDefault(); const bin=file.files[0];
 if(!bin||!bin.size||!bin.name.toLowerCase().endsWith('.bin')){status.textContent='Select a nonempty .bin file.';return;}
 button.disabled=file.disabled=true; progress.value=0; status.textContent='Uploading firmware...';
 const xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.timeout=180000;
 xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-Update-Token',token);
 xhr.upload.onprogress=e=>{if(e.lengthComputable){progress.value=Math.round(e.loaded/e.total*100);status.textContent=progress.value===100?'Upload sent. Validating firmware...':'Uploading: '+progress.value+'%';}};
 const retry=message=>{status.textContent=message;button.disabled=file.disabled=false;};
 xhr.onload=()=>{if(xhr.status===200){status.textContent=xhr.responseText;progress.value=100;}else retry(xhr.responseText||'Update failed. Reload this page and retry.');};
 xhr.onerror=xhr.ontimeout=()=>retry('Connection lost or timed out. Update result is unknown. Wait for the controller, then reload this page before retrying.');
 xhr.send(bin);
};
