let updateInterval;const SERIAL_CONFIG_MAP = {
'0': 'SERIAL_8N1','2': 'SERIAL_8E1','3': 'SERIAL_8O1'
};document.addEventListener('DOMContentLoaded',() => {
initializeTabs();initializeForms();loadSystemInfo();loadNetworkConfig();loadGatewayConfig();loadModbusTCPStatus();startAutoUpdate()});function initializeTabs() {
const tabBtns = document.querySelectorAll('.tab-btn');const tabContents = document.querySelectorAll('.tab-content');tabBtns.forEach(btn => {
btn.addEventListener('click',() => {
const targetTab = btn.dataset.tab;tabBtns.forEach(b => b.classList.remove('active'));tabContents.forEach(c => c.classList.remove('active'));btn.classList.add('active');document.getElementById(targetTab).classList.add('active');if(targetTab === 'files') {
loadFileList()}
})})}
function initializeForms() {
document.getElementById('rs485-form').addEventListener('submit',async (e) => {
e.preventDefault();await saveRS485Config()});document.getElementById('save-ports-btn').addEventListener('click',async () => {
await savePortConfig()});document.getElementById('network-form').addEventListener('submit',async (e) => {
e.preventDefault();await saveNetworkConfig()});document.getElementById('network-mode').addEventListener('change',(e) => {
const staticFields = document.querySelector('.static-ip-fields');staticFields.style.display = e.target.value === 'static' ? 'grid' : 'none'});document.getElementById('reboot-btn').addEventListener('click',async () => {
if(confirm('Are you sure you want to reboot the system?')) {
await rebootSystem()}
});document.getElementById('refresh-files-btn').addEventListener('click',() => {
loadFileList()})}
async function loadSystemInfo() {
try {
const response = await fetch('/api/system/version');const data = await response.json();document.getElementById('system-version').textContent = `v${data.version}`} catch (error) {
console.error('Failed to load system info:',error)}
}
async function updateDashboard() {
try {
const statusResponse = await fetch('/api/system/status');if(!statusResponse.ok) {
console.warn('System status request failed:',statusResponse.status);return;// Keep existing UI state on error
}
const statusData = await statusResponse.json();if(statusData.busy) {
const uptime = statusData.uptime;const hours = Math.floor(uptime / 3600);const minutes = Math.floor((uptime % 3600) / 60);document.getElementById('system-uptime').textContent = `Uptime: ${hours}h ${minutes}m`;return;// Skip other updates to preserve current UI state
}
const uptime = statusData.uptime;const hours = Math.floor(uptime / 3600);const minutes = Math.floor((uptime % 3600) / 60);document.getElementById('system-uptime').textContent = `Uptime: ${hours}h ${minutes}m`;if(statusData.ethernet) {
if(statusData.ethernet.connected) {
document.getElementById('eth-status').textContent = statusData.ethernet.dhcp ? 'DHCP' : 'Static';document.getElementById('eth-status').className = 'badge success';document.getElementById('eth-ip').textContent = statusData.ethernet.ip} else {
document.getElementById('eth-status').textContent = 'Disconnected';document.getElementById('eth-status').className = 'badge error';document.getElementById('eth-ip').textContent = '--'}
}
if(statusData.sd) {
if(statusData.sd.ready) {
const inserted = statusData.sd.inserted ? 'Inserted' : 'Not Inserted';document.getElementById('sd-status').textContent = inserted;document.getElementById('sd-status').className = 'badge success';document.getElementById('sd-size').textContent =
`${statusData.sd.capacityGB.toFixed(1)} GB (${statusData.sd.freeSpaceGB.toFixed(1)} GB free)`} else if(statusData.sd.inserted) {
document.getElementById('sd-status').textContent = 'Error';document.getElementById('sd-status').className = 'badge error';document.getElementById('sd-size').textContent = '--'} else {
document.getElementById('sd-status').textContent = 'Not Inserted';document.getElementById('sd-status').className = 'badge info';document.getElementById('sd-size').textContent = '--'}
}
if(statusData.modbus) {
if(statusData.modbus.hasError) {
document.getElementById('rs485-status').textContent = 'Comm Error';document.getElementById('rs485-status').className = 'badge error'} else if(statusData.modbus.activeDevices > 0) {
document.getElementById('rs485-status').textContent = 'OK';document.getElementById('rs485-status').className = 'badge success'} else {
document.getElementById('rs485-status').textContent = 'No Devices';document.getElementById('rs485-status').className = 'badge info'}
const deviceText = statusData.modbus.errorDevices > 0
? `${statusData.modbus.activeDevices} (${statusData.modbus.errorDevices} errors)`
: `${statusData.modbus.activeDevices}`;document.getElementById('rs485-devices').textContent = deviceText}
if(statusData.modbusTcp) {
document.getElementById('modbus-port').textContent = statusData.modbusTcp.port;document.getElementById('modbus-clients').textContent = statusData.modbusTcp.connectedClients;const clientList = document.getElementById('modbus-client-list');if(statusData.modbusTcp.clients && statusData.modbusTcp.clients.length > 0) {
clientList.innerHTML = statusData.modbusTcp.clients.map(client =>
`<div class="client-info"><i class="mdi mdi-lan-connect"></i> ${client}</div>`
).join('')} else {
clientList.innerHTML = '<div class="client-info no-clients">No connected clients</div>'}
}
const gatewayResponse = await fetch('/api/gateway/data');const gatewayData = await gatewayResponse.json();updateFlowCounterGrid(gatewayData.flow_counters,gatewayData.current_millis,gatewayData.millis_rollover_count)} catch (error) {
console.error('Failed to update dashboard:',error)}
}
function formatTimestamp(unixTimestamp) {
if(!unixTimestamp || unixTimestamp === 0) return 'N/A';const date = new Date(unixTimestamp * 1000);const year = date.getUTCFullYear();const month = String(date.getUTCMonth() + 1).padStart(2,'0');const day = String(date.getUTCDate()).padStart(2,'0');const hours = String(date.getUTCHours()).padStart(2,'0');const minutes = String(date.getUTCMinutes()).padStart(2,'0');const seconds = String(date.getUTCSeconds()).padStart(2,'0');return `${day}/${month}/${year},${hours}:${minutes}:${seconds}`}
function formatTimeSince(lastUpdateMillis,currentMillis,rolloverCount) {
if(!lastUpdateMillis || lastUpdateMillis === 0) return 'Never';let elapsed;if(currentMillis >= lastUpdateMillis) {
elapsed = currentMillis - lastUpdateMillis} else {
elapsed = (0xFFFFFFFF - lastUpdateMillis) + currentMillis}
const seconds = Math.floor(elapsed / 1000);const minutes = Math.floor(seconds / 60);const hours = Math.floor(minutes / 60);const days = Math.floor(hours / 24);if(days > 0) {
return `${days}d ${hours % 24}h ago`} else if(hours > 0) {
return `${hours}h ${minutes % 60}m ago`} else if(minutes > 0) {
return `${minutes}m ${seconds % 60}s ago`} else {
return `${seconds}s ago`}
}
function updateFlowCounterGrid(flowCounters,currentMillis,rolloverCount) {
const grid = document.getElementById('flow-counter-grid');if(!flowCounters || flowCounters.length === 0) {
grid.innerHTML = '<p style="color: var(--text-secondary);">No flow counters configured</p>';return}
const enabledCounters = flowCounters.filter(fc => fc.enabled);if(enabledCounters.length === 0) {
grid.innerHTML = '<p style="color: var(--text-secondary);">No flow counters enabled</p>';return}
grid.innerHTML = enabledCounters.map(fc => {
let statusClass = '';if(fc.comm_error) statusClass = 'error';else if(fc.trigger_count > 0 && fc.data_valid) statusClass = 'ok';let dataHtml = '<p style="color: var(--text-secondary);font-size: 0.85rem;">No data</p>';if(fc.data_valid && fc.data) {
const lastTriggerTime = formatTimestamp(fc.data.timestamp);const timeSince = formatTimeSince(fc.data.last_update,currentMillis,rolloverCount);dataHtml = `
<div class="flow-counter-data">
<div class="data-section">
<div class="data-section-title">Flow Data</div>
<div><span class="label">Unit ID:</span><span class="value">${fc.data.unit_id || 'N/A'}</span></div>
<div><span class="label">Volume:</span><span class="value">${fc.data.volume?.toFixed(2) || '0.00'} mL</span></div>
<div><span class="label">Volume (Norm):</span><span class="value">${fc.data.volume_normalised?.toFixed(2) || '0.00'} mL</span></div>
<div><span class="label">Flow:</span><span class="value">${fc.data.flow?.toFixed(2) || '0.00'} mL/min</span></div>
<div><span class="label">Flow (Norm):</span><span class="value">${fc.data.flow_normalised?.toFixed(2) || '0.00'} mL/min</span></div>
</div>
<div class="data-section">
<div class="data-section-title">Environmental</div>
<div><span class="label">Temperature:</span><span class="value">${fc.data.temperature?.toFixed(1) || '0.0'} °C</span></div>
<div><span class="label">Pressure:</span><span class="value">${fc.data.pressure?.toFixed(1) || '0.0'} hPa</span></div>
<div><span class="label">PSU Voltage:</span><span class="value">${fc.data.psu_volts?.toFixed(2) || '0.00'} V</span></div>
<div><span class="label">Battery:</span><span class="value">${fc.data.batt_volts?.toFixed(2) || '0.00'} V</span></div>
</div>
<div class="data-section">
<div class="data-section-title">Status</div>
<div><span class="label">Last Trigger:</span><span class="value">${lastTriggerTime}</span></div>
<div><span class="label">Last Read:</span><span class="value">${timeSince}</span></div>
<div><span class="label">Temp (Current):</span><span class="value">${fc.data.current_temperature?.toFixed(1) || '0.0'} °C</span></div>
<div><span class="label">Press (Current):</span><span class="value">${fc.data.current_pressure?.toFixed(1) || '0.0'} kPa</span></div>
<div><span class="label">Trigger Count:</span><span class="value">${fc.trigger_count}</span></div>
</div>
</div>
`}
return `
<div class="flow-counter-card">
<div class="flow-counter-header">
<span class="flow-counter-title">${fc.name}</span>
<span class="flow-counter-status ${statusClass}"></span>
</div>
<div style="margin-bottom: 8px;font-size: 0.85rem;color: var(--text-secondary);">
Slave ID: ${fc.slave_id}
</div>
${dataHtml}
<div class="flow-counter-actions">
<button class="btn btn-secondary btn-sm" onclick="manualRead(${fc.port})">
<i class="mdi mdi-refresh"></i> Manual Read
</button>
</div>
</div>
`}).join('')}
async function loadGatewayConfig() {
try {
const response = await fetch('/api/gateway/config');const data = await response.json();document.getElementById('baud-rate').value = data.rs485.baud_rate;const { parity,stopBits } = parseSerialConfig(data.rs485.serial_config);document.getElementById('parity').value = parity;document.getElementById('stop-bits').value = stopBits;document.getElementById('timeout').value = data.rs485.response_timeout;renderPortConfig(data.ports)} catch (error) {
console.error('Failed to load gateway config:',error)}
}
function parseSerialConfig(serialConfig) {
let parity = 'none';let stopBits = '1';const parityBits = serialConfig & 0xF;if(parityBits === 0x1) parity = 'even';else if(parityBits === 0x2) parity = 'odd';else if(parityBits === 0x3) parity = 'none';const stopBitField = serialConfig & 0xF0;if(stopBitField === 0x30) stopBits = '2';else if(stopBitField === 0x10) stopBits = '1';return { parity,stopBits }}
function renderPortConfig(ports) {
const container = document.getElementById('port-config-container');container.innerHTML = ports.map(port => `
<div class="port-config" data-port="${port.port}">
<div class="port-header">
<span class="port-title">Port ${port.port}</span>
</div>
<div class="port-fields">
<div class="form-group inline-field">
<label>Slave ID:</label>
<input type="number" class="port-slave-id slave-id-input" min="1" max="247" value="${port.slave_id}">
</div>
<div class="form-group inline-field">
<label>Name:</label>
<input type="text" class="port-name" value="${port.name}" maxlength="15">
</div>
<div class="form-group">
<label>
<input type="checkbox" class="port-enabled" ${port.enabled ? 'checked' : ''}>
Enabled
</label>
</div>
<div class="form-group">
<label>
<input type="checkbox" class="port-log-sd" ${port.log_to_sd ? 'checked' : ''}>
Log to SD
</label>
</div>
</div>
</div>
`).join('')}
async function saveRS485Config() {
const baudRate = parseInt(document.getElementById('baud-rate').value);const parity = document.getElementById('parity').value;const stopBits = document.getElementById('stop-bits').value;const timeout = parseInt(document.getElementById('timeout').value);const SERIAL_DATA_8 = 0x400;const SERIAL_STOP_BIT_1 = 0x10;const SERIAL_STOP_BIT_2 = 0x30;const SERIAL_PARITY_NONE = 0x3;const SERIAL_PARITY_EVEN = 0x1;const SERIAL_PARITY_ODD = 0x2;let parityBits = SERIAL_PARITY_NONE;if(parity === 'even') parityBits = SERIAL_PARITY_EVEN;else if(parity === 'odd') parityBits = SERIAL_PARITY_ODD;let stopBitField = (stopBits === '2') ? SERIAL_STOP_BIT_2 : SERIAL_STOP_BIT_1;let serialConfig = SERIAL_DATA_8 | stopBitField | parityBits;try {
const response = await fetch('/api/gateway/config',{
method: 'POST',headers: { 'Content-Type': 'application/json' },body: JSON.stringify({
rs485: {
baud_rate: baudRate,serial_config: serialConfig,response_timeout: timeout
}
})
});const result = await response.json();showToast(result.message || 'RS485 configuration saved','success');if(result.message && result.message.includes('restart')) {
setTimeout(() => {
window.location.reload()},3000)}
} catch (error) {
showToast('Failed to save RS485 configuration','error');console.error(error)}
}
async function savePortConfig() {
const portConfigs = [];const portElements = document.querySelectorAll('.port-config');portElements.forEach(el => {
const port = parseInt(el.dataset.port);const enabled = el.querySelector('.port-enabled').checked;const slaveId = parseInt(el.querySelector('.port-slave-id').value);const name = el.querySelector('.port-name').value;const logToSd = el.querySelector('.port-log-sd').checked;portConfigs.push({
port: port,enabled: enabled,slave_id: slaveId,name: name,log_to_sd: logToSd
})});try {
const response = await fetch('/api/gateway/config',{
method: 'POST',headers: { 'Content-Type': 'application/json' },body: JSON.stringify({ ports: portConfigs })
});const result = await response.json();showToast(result.message || 'Port configuration saved','success')} catch (error) {
showToast('Failed to save port configuration','error');console.error(error)}
}
async function loadNetworkConfig() {
try {
const response = await fetch('/api/network');const data = await response.json();document.getElementById('network-mode').value = data.mode;document.getElementById('ip-address').value = data.ip;document.getElementById('subnet').value = data.subnet;document.getElementById('gateway').value = data.gateway;document.getElementById('dns').value = data.dns;document.getElementById('hostname').value = data.hostname;document.getElementById('modbus-tcp-port').value = data.modbusTcpPort;const staticFields = document.querySelector('.static-ip-fields');staticFields.style.display = data.mode === 'static' ? 'grid' : 'none'} catch (error) {
console.error('Failed to load network config:',error)}
}
async function saveNetworkConfig() {
const mode = document.getElementById('network-mode').value;const config = {
mode: mode,hostname: document.getElementById('hostname').value,modbusTcpPort: parseInt(document.getElementById('modbus-tcp-port').value)
};if(mode === 'static') {
config.ip = document.getElementById('ip-address').value;config.subnet = document.getElementById('subnet').value;config.gateway = document.getElementById('gateway').value;config.dns = document.getElementById('dns').value}
try {
const response = await fetch('/api/network',{
method: 'POST',headers: { 'Content-Type': 'application/json' },body: JSON.stringify(config)
});const result = await response.json();showToast(result.message || 'Network configuration saved. System will reboot.','success');setTimeout(() => {
window.location.reload()},3000)} catch (error) {
showToast('Failed to save network configuration','error');console.error(error)}
}
async function loadModbusTCPStatus() {
try {
const response = await fetch('/api/modbus-tcp/status');const data = await response.json();document.getElementById('modbus-status').textContent = data.running ? 'Running' : 'Stopped';document.getElementById('modbus-status').className = data.running ? 'badge success' : 'badge error';document.getElementById('modbus-port-detail').textContent = data.port;document.getElementById('modbus-connections').textContent = data.connectedClients;const clientsList = document.getElementById('modbus-clients-list');if(data.clients && data.clients.length > 0) {
clientsList.innerHTML = '<h4 style="margin-bottom: 10px;">Connected Clients:</h4>' +
data.clients.map(client => `<div class="client-item">${client}</div>`).join('')} else {
clientsList.innerHTML = '<p style="color: var(--text-secondary);">No connected clients</p>'}
} catch (error) {
console.error('Failed to load Modbus TCP status:',error)}
}
async function loadFileList() {
try {
const response = await fetch('/api/sd/list?path=/');const data = await response.json();const fileList = document.getElementById('file-list');const sdInfo = document.getElementById('sd-info');if(data.error) {
fileList.innerHTML = `<p style="color: var(--accent-red);">${data.error}</p>`;return}
if(data.free_space_mb && data.total_space_mb) {
sdInfo.textContent = `${data.free_space_mb.toFixed(1)} MB free of ${data.total_space_mb.toFixed(1)} MB`}
let html = '<div class="file-section"><h4 style="color: var(--text-secondary);font-size: 0.9em;margin: 0 0 8px 0;">System Log</h4>';const logSize = data.system_log_size ? formatFileSize(data.system_log_size) : 'N/A';html += `
<div class="file-item">
<div class="file-info">
<a href="#" class="file-name" onclick="previewFile('/logs/system.txt','system.txt');return false;">
<i class="mdi mdi-file-document"></i> system.txt
</a>
<span class="file-size">${logSize}</span>
</div>
<div class="file-actions">
<button class="btn btn-secondary btn-icon" onclick="downloadFile('/logs/system.txt','system.txt')" title="Download">
<i class="mdi mdi-download-box"></i>
</button>
<button class="btn btn-secondary btn-icon" onclick="deleteFile('/logs/system.txt','system.txt')" title="Delete">
<i class="mdi mdi-trash-can"></i>
</button>
</div>
</div>
</div>`;if(data.files && data.files.length > 0) {
html += '<div class="file-section" style="margin-top: 16px;"><h4 style="color: var(--text-secondary);font-size: 0.9em;margin: 0 0 8px 0;">Recording Files</h4>';html += data.files.map(file => `
<div class="file-item">
<div class="file-info">
<a href="#" class="file-name" onclick="previewFile('${file.path}','${file.name}');return false;">
<i class="mdi mdi-file-document-outline"></i> ${file.name}
</a>
<span class="file-size">${formatFileSize(file.size)}</span>
</div>
<div class="file-actions">
<button class="btn btn-secondary btn-icon" onclick="downloadFile('${file.path}','${file.name}')" title="Download">
<i class="mdi mdi-download-box"></i>
</button>
<button class="btn btn-secondary btn-icon" onclick="deleteFile('${file.path}','${file.name}')" title="Delete">
<i class="mdi mdi-trash-can"></i>
</button>
</div>
</div>
`).join('');html += '</div>'}
fileList.innerHTML = html} catch (error) {
console.error('Failed to load file list:',error);document.getElementById('file-list').innerHTML = '<p style="color: var(--accent-red);">Failed to load files</p>'}
}
function downloadFile(path,name) {
const url = `/api/sd/download?path=${encodeURIComponent(path)}`;const a = document.createElement('a');a.href = url;a.download = name;document.body.appendChild(a);a.click();document.body.removeChild(a)}
function previewFile(path,name) {
const url = `/api/sd/view?path=${encodeURIComponent(path)}`;window.open(url,'_blank')}
async function deleteFile(path,name) {
if(!confirm(`Are you sure you want to delete "${name}"?`)) {
return}
try {
const response = await fetch(`/api/sd/delete?path=${encodeURIComponent(path)}`,{
method: 'DELETE'
});const result = await response.json();if(response.ok) {
showToast('File deleted successfully','success');loadFileList();// Refresh the file list
} else {
showToast(result.error || 'Failed to delete file','error')}
} catch (error) {
console.error('Failed to delete file:',error);showToast('Failed to delete file','error')}
}
function formatFileSize(bytes) {
if(bytes < 1024) return bytes + ' B';if(bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';return (bytes / (1024 * 1024)).toFixed(1) + ' MB'}
async function rebootSystem() {
try {
await fetch('/api/system/reboot',{ method: 'POST' });showToast('System is rebooting...','info');document.body.style.opacity = '0.5';document.body.style.pointerEvents = 'none';setTimeout(() => {
window.location.reload()},10000)} catch (error) {
showToast('Failed to reboot system','error')}
}
async function manualRead(portNumber) {
try {
showToast(`Triggering manual read for Port ${portNumber}...`,'info');const response = await fetch(`/api/gateway/manual-read?port=${portNumber}`,{
method: 'POST'
});if(response.ok) {
showToast(`Manual read triggered for Port ${portNumber}`,'success');setTimeout(() => updateDashboard(),500)} else {
const error = await response.json();showToast(`Failed to read Port ${portNumber}: ${error.error || 'Unknown error'}`,'error')}
} catch (error) {
showToast(`Error triggering manual read: ${error.message}`,'error')}
}
function showToast(message,type = 'info') {
const toast = document.getElementById('toast');toast.textContent = message;toast.className = `toast ${type} show`;setTimeout(() => {
toast.classList.remove('show')},4000)}
function startAutoUpdate() {
updateDashboard();updateInterval = setInterval(() => {
updateDashboard()},2000);// Update every 2 seconds
}
document.addEventListener('visibilitychange',() => {
if(document.hidden) {
clearInterval(updateInterval)} else {
startAutoUpdate()}
});