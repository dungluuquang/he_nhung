#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <time.h> 
#include <Stepper.h>

#define SS_PIN  5
#define RST_PIN 4     
#define IN1 15
#define IN2 2
#define IN3 16
#define IN4 17

const char* ssid = "iPhone 4s";
const char* password = "12345678";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Stepper myStepper(2048, IN1, IN3, IN2, IN4);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

// --- HỆ THỐNG ĐA TỦ ---
#define NUM_LOCKERS 4
bool lockerOcc[NUM_LOCKERS] = {false, false, false, false};
String lockerOwner[NUM_LOCKERS] = {"", "", "", ""};

// --- QUẢN LÝ TÀI KHOẢN & THẺ ---
String userAccounts = ""; 
String authorizedCards = ""; 
String reqChangeUsers = ""; 
bool isEnrollMode = false;
String pendingName = "";

bool isOpen = false;
unsigned long doorOpenedAt = 0; 
int currentOpenLocker = -1;

#define MAX_LOGS 10 
String historyLogs[MAX_LOGS];
int logCount = 0;

// --- GIAO DIỆN WEB ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <title>Hệ Thống Tủ Đồ Thông Minh </title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #eef2f5; margin:0; padding:15px; }
    .container { max-width: 500px; margin: auto; background: white; padding: 20px; border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }
    h2 { text-align: center; color: #2c3e50; }
    .pwd-wrapper { position: relative; margin-bottom: 12px; width: 100%; }
    .pwd-wrapper input { width: calc(100% - 24px); padding: 12px; padding-right: 40px; margin-bottom: 0; border: 1px solid #ccc; border-radius: 6px; font-size: 16px; box-sizing: border-box; }
    .pwd-toggle { position: absolute; right: 12px; top: 50%; transform: translateY(-50%); cursor: pointer; user-select: none; font-size: 18px; color: #7f8c8d; }
    input[type="text"]:not(.pwd-input) { width: calc(100% - 24px); padding: 12px; margin-bottom: 12px; border: 1px solid #ccc; border-radius: 6px; font-size: 16px; box-sizing: border-box; }
    .btn { padding: 12px; font-weight: bold; border: none; border-radius: 6px; cursor: pointer; color: white; width: 100%; margin: 5px 0; font-size: 16px; }
    .btn-blue { background: #3498db; } .btn-green { background: #27ae60; } .btn-red { background: #e74c3c; } .btn-gray { background: #95a5a6; } .btn-orange { background: #f39c12; color: white; }
    .btn-small { padding: 6px 10px; font-size: 12px; border: none; border-radius: 4px; cursor: pointer; margin-left: 5px; color: white;}
    #login-view, #force-change-view { text-align: center; padding: 10px 0; }
    #dashboard-view { display: none; }
    .header-bar { display: flex; justify-content: space-between; align-items: center; background: #34495e; color: white; padding: 10px 15px; border-radius: 8px; margin-bottom: 15px; }
    .locker-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 20px; }
    .locker-card { border: 2px solid #bdc3c7; border-radius: 8px; padding: 15px; text-align: center; transition: all 0.3s; }
    .locker-empty { background: #e8f5e9; border-color: #2ecc71; } .locker-full { background: #fbeee6; border-color: #e67e22; }
    .locker-title { font-size: 1.2rem; font-weight: bold; margin-bottom: 10px; }
    .admin-panel { background: #f8f9fa; padding: 15px; border: 1px solid #bdc3c7; border-radius: 8px; margin-bottom: 15px; }
    .user-item { display: flex; justify-content: space-between; align-items: center; background: white; padding: 10px; border-radius: 4px; margin-bottom: 5px; border-left: 3px solid #3498db;}
    .history-box { height: 150px; overflow-y: auto; font-size: 13px; color: #333; background: #fff3e0; padding: 10px; border-radius: 6px; border-left: 4px solid #f39c12; margin-top: 15px; }
    .modal-overlay { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.6); z-index: 1000; align-items: center; justify-content: center; }
    .modal-box { background: white; padding: 20px; border-radius: 10px; width: 90%; max-width: 400px; box-shadow: 0 5px 15px rgba(0,0,0,0.3); }
  </style>
</head>
<body>
  <div class="container">
    <h2>Hệ Thống Tủ Đồ Thông Minh</h2>
    
    <div id="login-view">
      <h3>Đăng nhập hệ thống</h3>
      <input type="text" id="logUser" placeholder="Tài khoản">
      <div class="pwd-wrapper">
        <input type="password" id="logPass" class="pwd-input" placeholder="Mật khẩu...">
        <span class="pwd-toggle" onclick="togglePwd('logPass', this)">👁️</span>
      </div>
      <button class="btn btn-blue" onclick="doLogin()">ĐĂNG NHẬP</button>
      <p id="login-msg" style="color:red;"></p>
    </div>

    <div id="force-change-view" style="display:none;">
      <h3 style="color:#e74c3c;">YÊU CẦU ĐỔI MẬT KHẨU</h3>
      <p style="font-size:14px;">Bạn đang dùng mật khẩu tạm thời. Vui lòng đổi mật khẩu mới để tiếp tục!</p>
      <div class="pwd-wrapper">
        <input type="password" id="fcPass" class="pwd-input" placeholder="Mật khẩu mới (>= 8 ký tự)...">
        <span class="pwd-toggle" onclick="togglePwd('fcPass', this)">👁️</span>
      </div>
      <div class="pwd-wrapper">
        <input type="password" id="fcPassConfirm" class="pwd-input" placeholder="Xác nhận mật khẩu mới...">
        <span class="pwd-toggle" onclick="togglePwd('fcPassConfirm', this)">👁️</span>
      </div>
      <button class="btn btn-orange" onclick="doForceChange()">CẬP NHẬT & ĐĂNG NHẬP</button>
      <button class="btn btn-gray" onclick="logout()">THOÁT</button>
      <p id="fc-msg" style="color:red;"></p>
    </div>

    <div id="dashboard-view">
      <div class="header-bar">
        <span id="welcome-text">Xin chào, User</span>
        <button class="btn-small btn-red" onclick="logout()">Thoát</button>
      </div>

      <div id="admin-panel" class="admin-panel" style="display: none;">
        <h3 style="margin-top:0;">Quản lý Cư dân</h3>
        <div style="display:flex; flex-direction:column; gap:5px; margin-bottom: 10px;">
          <input type="text" id="newUser" placeholder="Tên Cư dân mới (VD: P101)">
          <div class="pwd-wrapper">
            <input type="password" id="newPass" class="pwd-input" placeholder="Mật khẩu (Không để ngày sinh)">
            <span class="pwd-toggle" onclick="togglePwd('newPass', this)">👁️</span>
          </div>
          <div class="pwd-wrapper">
            <input type="password" id="newPassConfirm" class="pwd-input" placeholder="Xác nhận lại mật khẩu">
            <span class="pwd-toggle" onclick="togglePwd('newPassConfirm', this)">👁️</span>
          </div>
        </div>
        <button class="btn btn-green" onclick="createAccount()">+ CẤP TÀI KHOẢN MỚI</button>
        <div id="enroll-status" style="color:#d35400; font-weight:bold; margin-top:15px; margin-bottom:10px; text-align:center;"></div>
        <h4 style="margin-bottom:5px;">Danh sách Cư dân:</h4>
        <div id="user-list"></div>
      </div>

      <h3 style="margin-bottom: 5px;">Chọn Tủ Của Bạn:</h3>
      <div class="locker-grid" id="locker-container"></div>
      
      <div id="door-status" style="text-align:center; padding:10px; font-weight:bold; border-radius:6px; margin-bottom:15px; transition: 0.3s;">
        TRẠNG THÁI: ĐANG TẢI...
      </div>

      <div id="history-section" class="history-box" style="display: none;">
        <strong>Nhật ký hệ thống:</strong>
        <div id="historyList">Đang tải...</div>
      </div>
    </div>
  </div>

  <div id="resetModal" class="modal-overlay">
    <div class="modal-box">
      <h3 style="margin-top:0; color:#d35400;">Đổi Mật Khẩu</h3>
      <p style="margin-bottom:15px;">Đang cấp lại mật khẩu cho: <b id="resetTargetName"></b></p>
      <div class="pwd-wrapper">
        <input type="password" id="resetPass" class="pwd-input" placeholder="Mật khẩu mới...">
        <span class="pwd-toggle" onclick="togglePwd('resetPass', this)">👁️</span>
      </div>
      <div class="pwd-wrapper">
        <input type="password" id="resetPassConfirm" class="pwd-input" placeholder="Nhập lại mật khẩu mới...">
        <span class="pwd-toggle" onclick="togglePwd('resetPassConfirm', this)">👁️</span>
      </div>
      <div style="display:flex; gap:10px; margin-top:20px;">
        <button class="btn btn-gray" onclick="closeResetModal()">HỦY BỎ</button>
        <button class="btn btn-orange" onclick="confirmResetPassword()">XÁC NHẬN ĐỔI</button>
      </div>
    </div>
  </div>

<script>
  var currentUser = ""; var currentPass = ""; var ws; var targetResetUser = "";

  function initWebSocket() {
    ws = new WebSocket(`ws://${window.location.hostname}/ws`);
    ws.onmessage = (event) => { try { updateDashboard(JSON.parse(event.data)); } catch(e) {} };
    ws.onclose = () => { setTimeout(initWebSocket, 2000); };
  }
  window.onload = initWebSocket;

  function togglePwd(inputId, iconEl) {
    let input = document.getElementById(inputId);
    if (input.type === "password") { input.type = "text"; iconEl.innerText = "🙈"; } 
    else { input.type = "password"; iconEl.innerText = "👁️"; }
  }

  function checkPasswordStrength(pwd) {
    if(pwd.length < 8) return "Mật khẩu phải dài ít nhất 8 ký tự!";
    if (/^(.)\1+$/.test(pwd)) return "Cảnh báo: Không được đặt các ký tự giống hệt nhau liên tiếp!";
    
    let isAscending = true; let isDescending = true;
    for (let i = 0; i < pwd.length - 1; i++) {
        let diff = pwd.charCodeAt(i + 1) - pwd.charCodeAt(i);
        if (diff !== 1) isAscending = false;
        if (diff !== -1) isDescending = false;
    }
    if (isAscending || isDescending) return "Cảnh báo: Không đặt mật khẩu là chuỗi ký tự tiến/lùi liên tiếp!";

    if (/^\d{8}$/.test(pwd)) {
        let dd1 = parseInt(pwd.substring(0,2)), mm1 = parseInt(pwd.substring(2,4)), yy1 = parseInt(pwd.substring(4,8));
        let isDDMMYYYY = (dd1 >= 1 && dd1 <= 31) && (mm1 >= 1 && mm1 <= 12) && (yy1 >= 1900 && yy1 <= 2100);
        let yy2 = parseInt(pwd.substring(0,4)), mm2 = parseInt(pwd.substring(4,6)), dd2 = parseInt(pwd.substring(6,8));
        let isYYYYMMDD = (yy2 >= 1900 && yy2 <= 2100) && (mm2 >= 1 && mm2 <= 12) && (dd2 >= 1 && dd2 <= 31);
        if (isDDMMYYYY || isYYYYMMDD) return "Bảo mật kém: Hệ thống chặn đặt mật khẩu là Ngày tháng năm sinh!";
    }
    return "OK";
  }

  function doLogin() {
    let u = document.getElementById('logUser').value.trim();
    let p = document.getElementById('logPass').value.trim();
    if(u === "" || p === "") return;
    
    fetch(`/api/login?u=${u}&p=${p}`).then(res => res.text()).then(text => {
      if(text === "OK") {
        currentUser = u; currentPass = p;
        document.getElementById('login-view').style.display = "none";
        document.getElementById('dashboard-view').style.display = "block";
        document.getElementById('welcome-text').innerText = "Xin chào, " + u;
        document.getElementById('admin-panel').style.display = (u === "admin") ? "block" : "none";
        document.getElementById('history-section').style.display = (u === "admin") ? "block" : "none";
        ws.send("request_update");
      } else if (text === "REQUIRE_CHANGE") {
        currentUser = u; currentPass = p;
        document.getElementById('login-view').style.display = "none";
        document.getElementById('force-change-view').style.display = "block";
      } else { 
        document.getElementById('login-msg').innerText = "Sai tài khoản hoặc mật khẩu!"; 
      }
    });
  }

  function doForceChange() {
    let newP = document.getElementById('fcPass').value.trim();
    let confP = document.getElementById('fcPassConfirm').value.trim();

    if(newP === "" || confP === "") { document.getElementById('fc-msg').innerText = "Vui lòng nhập đầy đủ!"; return; }
    if(newP !== confP) { document.getElementById('fc-msg').innerText = "Mật khẩu xác nhận không khớp!"; return; }

    let strengthMsg = checkPasswordStrength(newP);
    if(strengthMsg !== "OK") { document.getElementById('fc-msg').innerText = strengthMsg; return; }

    fetch(`/api/force_change?u=${currentUser}&p=${currentPass}&newP=${encodeURIComponent(newP)}`)
      .then(res => res.text()).then(txt => { 
        if(txt === "OK") {
          alert("Đổi mật khẩu thành công! Chào mừng bạn.");
          currentPass = newP;
          document.getElementById('force-change-view').style.display = "none";
          document.getElementById('dashboard-view').style.display = "block";
          document.getElementById('welcome-text').innerText = "Xin chào, " + currentUser;
          document.getElementById('admin-panel').style.display = (currentUser === "admin") ? "block" : "none";
          document.getElementById('history-section').style.display = (currentUser === "admin") ? "block" : "none";
          ws.send("request_update");
        } else { document.getElementById('fc-msg').innerText = txt; }
    });
  }

  function createAccount() {
    let u = document.getElementById('newUser').value.trim();
    let p = document.getElementById('newPass').value.trim();
    let pConf = document.getElementById('newPassConfirm').value.trim();
    
    if(u === "" || p === "" || pConf === "") { alert("Vui lòng nhập đầy đủ thông tin!"); return; }
    if(p !== pConf) { alert("Lỗi: Mật khẩu xác nhận không khớp!"); return; }
    
    let strengthMsg = checkPasswordStrength(p);
    if(strengthMsg !== "OK") { alert(strengthMsg); return; }

    fetch(`/api/create?u=${currentUser}&p=${currentPass}&newU=${u}&newP=${p}`).then(res=>res.text()).then(txt => { 
        if(txt === "OK") {
            alert("Đã cấp tài khoản thành công cho: " + u);
            document.getElementById('newUser').value = "";
            document.getElementById('newPass').value = ""; 
            document.getElementById('newPassConfirm').value = ""; 
        } else { alert(txt); }
    });
  }

  function openResetModal(target) {
    targetResetUser = target;
    document.getElementById('resetTargetName').innerText = target;
    document.getElementById('resetPass').value = "";
    document.getElementById('resetPassConfirm').value = "";
    document.getElementById('resetModal').style.display = "flex";
  }
  function closeResetModal() { document.getElementById('resetModal').style.display = "none"; targetResetUser = ""; }

  function confirmResetPassword() {
    let newP = document.getElementById('resetPass').value.trim();
    let confirmP = document.getElementById('resetPassConfirm').value.trim();

    if(newP === "" || confirmP === "") { alert("Vui lòng nhập đầy đủ 2 ô mật khẩu!"); return; }
    if(newP !== confirmP) { alert("Lỗi: Hai ô mật khẩu không khớp nhau!"); return; }

    let strengthMsg = checkPasswordStrength(newP);
    if(strengthMsg !== "OK") { alert(strengthMsg); return; }

    fetch(`/api/reset_pass?u=${currentUser}&p=${currentPass}&target=${targetResetUser}&newP=${encodeURIComponent(newP)}`)
      .then(res => res.text()).then(txt => { alert(txt); closeResetModal(); });
  }

  function deleteUser(target) {
    let confirmName = prompt(`CẢNH BÁO NGUY HIỂM \nBạn đang chuẩn bị xóa toàn bộ tài khoản, thẻ từ và quyền mở tủ của cư dân [${target}].\n\nĐể đảm bảo không xóa nhầm, vui lòng GÕ LẠI TÊN (${target}) vào ô bên dưới:`);
    if (confirmName === null) return; 
    
    if (confirmName.trim() === target) {
        fetch(`/api/delete_user?u=${currentUser}&p=${currentPass}&target=${target}`)
            .then(res => res.text())
            .then(txt => { 
                if(txt === "OK") alert(`Đã xóa thành công toàn bộ dữ liệu của ${target}!`);
                else alert(txt);
            });
    } else {
        alert("Lỗi: Tên xác nhận không khớp! Lệnh xóa đã bị hủy để đảm bảo an toàn.");
    }
  }

  function enrollCard(target) { fetch(`/api/enroll?u=${currentUser}&p=${currentPass}&target=${target}`); }
  function actionLocker(id) { fetch(`/api/locker?id=${id}&u=${currentUser}&p=${currentPass}`).then(res=>res.text()).then(txt => { if(txt !== "OK") alert(txt); }); }

  function logout() {
    currentUser = ""; currentPass = "";
    document.getElementById('dashboard-view').style.display = "none";
    document.getElementById('force-change-view').style.display = "none";
    document.getElementById('login-view').style.display = "block";
    document.getElementById('logPass').value = "";
    document.getElementById('fcPass').value = ""; document.getElementById('fcPassConfirm').value = "";
  }

  function updateDashboard(data) {
    if(currentUser === "") return;
    
    if(data.isEnroll) document.getElementById('enroll-status').innerText = "ĐANG ĐỢI QUẸT THẺ CHO: " + data.pending;
    else document.getElementById('enroll-status').innerText = "";

    let doorSt = document.getElementById('door-status');
    if (data.isOpen) {
        doorSt.innerText = "TRẠNG THÁI: TỦ SỐ " + (data.openId + 1) + " ĐANG MỞ (Khóa sau 10s)";
        doorSt.style.background = "#e74c3c"; 
        doorSt.style.color = "white";
    } else {
        doorSt.innerText = "TRẠNG THÁI: TỦ ĐÃ KHÓA AN TOÀN";
        doorSt.style.background = "#2ecc71"; 
        doorSt.style.color = "white";
    }

    if(currentUser === "admin") {
      let uHTML = "";
      data.users.forEach(u => {
        uHTML += `<div class="user-item"><b>${u}</b> <div>`;
        uHTML += `<button class="btn-small btn-blue" onclick="enrollCard('${u}')">Cấp Thẻ</button>`;
        uHTML += `<button class="btn-small btn-orange" onclick="openResetModal('${u}')">Đổi Pass</button>`;
        uHTML += `<button class="btn-small btn-red" onclick="deleteUser('${u}')">Xóa</button></div></div>`;
      });
      document.getElementById('user-list').innerHTML = uHTML;
    }

    let gridHTML = "";
    for(let i=0; i<4; i++) {
      let isOcc = data.lockers[i].occ; let owner = data.lockers[i].owner;
      gridHTML += `<div class="locker-card ${isOcc ? 'locker-full' : 'locker-empty'}">`;
      gridHTML += `<div class="locker-title">TỦ SỐ ${i+1}</div>`;
      
      // ADMIN BỊ TƯỚC QUYỀN MỞ TỦ
      if(!isOcc) {
        gridHTML += `<div style="color:#27ae60; margin-bottom:10px;">Trạng thái: Trống</div>`;
        if (currentUser === "admin") {
            gridHTML += `<button class="btn btn-gray" disabled>QUYỀN CƯ DÂN</button>`;
        } else {
            gridHTML += `<button class="btn btn-blue" onclick="actionLocker(${i})">MỞ CHO SHIPPER</button>`;
        }
      } else {
        gridHTML += `<div style="color:#d35400; margin-bottom:10px;">Chứa đồ của:<br><b>${owner}</b></div>`;
        if(currentUser === owner) {
          gridHTML += `<button class="btn btn-green" onclick="actionLocker(${i})">MỞ ĐỂ LẤY ĐỒ</button>`;
        } else {
          gridHTML += `<button class="btn btn-gray" disabled>BẢO MẬT: KHÔNG CÓ QUYỀN</button>`;
        }
      }
      gridHTML += `</div>`;
    }
    document.getElementById('locker-container').innerHTML = gridHTML;
    
    let histHTML = "";
    data.history.forEach(h => { histHTML += `<div style='padding:4px 0; border-bottom:1px dashed #ccc'>• ${h}</div>`; });
    document.getElementById('historyList').innerHTML = histHTML;
  }
</script>
</body>
</html>
)rawliteral";


String getTimeString() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo, 0)) return "--:--"; 
    char timeStringBuff[10];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
    return String(timeStringBuff);
}

void addLog(String message) {
    for(int i = MAX_LOGS - 1; i > 0; i--) historyLogs[i] = historyLogs[i-1];
    historyLogs[0] = message + " (" + getTimeString() + ")"; 
    if(logCount < MAX_LOGS) logCount++;
}

void updateLCD(String l1, String l2) {
    lcd.clear(); lcd.setCursor(0, 0); lcd.print(l1); lcd.setCursor(0, 1); lcd.print(l2);                      
}

void showIdleLCD() {
    String t = getTimeString();
    int freeLockers = 0;
    for(int i=0; i<NUM_LOCKERS; i++) { if(!lockerOcc[i]) freeLockers++; }
    updateLCD("Gio: " + t, "Tu trong: " + String(freeLockers) + "/4");
}

void saveLockerState() {
    for(int i=0; i<NUM_LOCKERS; i++) {
        preferences.putBool(("occ" + String(i)).c_str(), lockerOcc[i]);
        preferences.putString(("own" + String(i)).c_str(), lockerOwner[i]);
    }
}

bool checkLogin(String u, String p) {
    if(u == "admin" && p == "123456") return true; 
    String searchStr = u + ":" + p + ",";
    return (userAccounts.indexOf(searchStr) != -1);
}

void sendStateToWeb() {
    String json = "{";
    json += "\"isOpen\":" + String(isOpen) + ",\"openId\":" + String(currentOpenLocker) + ",";
    json += "\"isEnroll\":" + String(isEnrollMode) + ",\"pending\":\"" + pendingName + "\",";
    
    json += "\"users\":[";
    int startIdx = 0; bool firstUser = true;
    while(startIdx < userAccounts.length()) {
        int commaIdx = userAccounts.indexOf(',', startIdx);
        if(commaIdx == -1) commaIdx = userAccounts.length();
        String entry = userAccounts.substring(startIdx, commaIdx);
        if(entry.length() > 0) {
            int colonIdx = entry.indexOf(':');
            String uName = entry.substring(0, colonIdx);
            if(!firstUser) json += ",";
            json += "\"" + uName + "\"";
            firstUser = false;
        }
        startIdx = commaIdx + 1;
    }
    json += "],\"lockers\":[";
    for(int i=0; i<NUM_LOCKERS; i++) {
        json += "{\"occ\":" + String(lockerOcc[i]) + ",\"owner\":\"" + lockerOwner[i] + "\"}";
        if(i < NUM_LOCKERS - 1) json += ",";
    }
    json += "],\"history\":[";
    for(int i=0; i<logCount; i++) {
        json += "\"" + historyLogs[i] + "\"";
        if(i < logCount - 1) json += ",";
    }
    json += "]}";
    ws.textAll(json);
}

void disableStepper() {
    digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

void openSpecificLocker(int id, String user, bool isRFID) {
    if (isOpen) return; 
    
    doorOpenedAt = millis(); 
    isOpen = true; 
    currentOpenLocker = id;
    
    if (!lockerOcc[id]) { 
        lockerOcc[id] = true; lockerOwner[id] = user;
        addLog(user + (isRFID ? " (THẺ)" : " (WEB)") + " mở Tủ " + String(id+1) + " để GỬI ĐỒ");
    } else { 
        lockerOcc[id] = false; lockerOwner[id] = "";
        addLog(user + (isRFID ? " (THẺ)" : " (WEB)") + " đã lấy đồ Tủ " + String(id+1));
    }
    
    updateLCD("TU " + String(id+1) + " DANG MO", "Tu khoa sau 10s");
    saveLockerState(); sendStateToWeb(); 
    
    myStepper.step(512); disableStepper(); 
    doorOpenedAt = millis(); 
}

void processDeleteUser(String targetUser) {
    String newAccounts = ""; int startIdx = 0;
    while(startIdx < userAccounts.length()) {
        int commaIdx = userAccounts.indexOf(',', startIdx);
        if (commaIdx == -1) commaIdx = userAccounts.length();
        String entry = userAccounts.substring(startIdx, commaIdx);
        if (entry.length() > 0 && !entry.startsWith(targetUser + ":")) { newAccounts += entry + ","; }
        startIdx = commaIdx + 1;
    }
    userAccounts = newAccounts; preferences.putString("users", userAccounts);

    String newCards = ""; startIdx = 0;
    while(startIdx < authorizedCards.length()) {
        int commaIdx = authorizedCards.indexOf(',', startIdx);
        if (commaIdx == -1) commaIdx = authorizedCards.length();
        String entry = authorizedCards.substring(startIdx, commaIdx);
        if (entry.length() > 0 && !entry.endsWith(":" + targetUser)) { newCards += entry + ","; }
        startIdx = commaIdx + 1;
    }
    authorizedCards = newCards; preferences.putString("uids", authorizedCards);

    String searchStr = targetUser + ",";
    int idx = reqChangeUsers.indexOf(searchStr);
    if(idx != -1) {
        reqChangeUsers = reqChangeUsers.substring(0, idx) + reqChangeUsers.substring(idx + searchStr.length());
        preferences.putString("reqChange", reqChangeUsers);
    }

    for(int i=0; i<NUM_LOCKERS; i++) {
        if(lockerOcc[i] && lockerOwner[i] == targetUser) {
            lockerOcc[i] = false; lockerOwner[i] = "";
            preferences.putBool(("occ" + String(i)).c_str(), false);
            preferences.putString(("own" + String(i)).c_str(), "");
        }
    }
    addLog("Đã xóa quyền và thu hồi tủ của: " + targetUser);
}

// --- SETUP ---
void setup() {
    Serial.begin(115200); SPI.begin(); rfid.PCD_Init();
    myStepper.setSpeed(15); disableStepper(); 
    
    lcd.init(); lcd.backlight(); lcd.setCursor(0,0); lcd.print("Khoi dong...");
    
    preferences.begin("locker", false);
    userAccounts = preferences.getString("users", "");
    authorizedCards = preferences.getString("uids", "");
    reqChangeUsers = preferences.getString("reqChange", ""); 
    
    for(int i=0; i<NUM_LOCKERS; i++) {
        lockerOcc[i] = preferences.getBool(("occ" + String(i)).c_str(), false);
        lockerOwner[i] = preferences.getString(("own" + String(i)).c_str(), "");
    }
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    Serial.println("\n========================================");
    Serial.println("KẾT NỐI WIFI THÀNH CÔNG!");
    Serial.print("Mời bạn dùng điện thoại truy cập Web tại: ");
    Serial.println(WiFi.localIP()); 
    Serial.println("========================================");
    
    showIdleLCD(); 
    addLog("Hệ thống khởi động");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/html", index_html); });
    
    server.on("/api/login", HTTP_GET, [](AsyncWebServerRequest *r){ 
        String u = r->getParam("u")->value(); String p = r->getParam("p")->value();
        if(checkLogin(u, p)) {
            if (u != "admin" && reqChangeUsers.indexOf(u + ",") != -1) {
                r->send(200, "text/plain", "REQUIRE_CHANGE");
            } else {
                r->send(200, "text/plain", "OK");
            }
        } else { r->send(200, "text/plain", "FAIL"); }
    });

    server.on("/api/force_change", HTTP_GET, [](AsyncWebServerRequest *r){
        String u = r->getParam("u")->value();
        String p = r->getParam("p")->value();
        String newP = r->getParam("newP")->value();

        if(!checkLogin(u, p)) { r->send(200, "text/plain", "Lỗi xác thực!"); return; }
        
        newP.replace(":", ""); newP.replace(",", ""); 
        String newAccounts = ""; int startIdx = 0; bool found = false;
        
        while(startIdx < userAccounts.length()) {
            int commaIdx = userAccounts.indexOf(',', startIdx);
            if (commaIdx == -1) commaIdx = userAccounts.length();
            String entry = userAccounts.substring(startIdx, commaIdx);
            if (entry.length() > 0) {
                if (entry.startsWith(u + ":")) {
                    newAccounts += u + ":" + newP + ",";
                    found = true;
                } else { newAccounts += entry + ","; }
            }
            startIdx = commaIdx + 1;
        }

        if(found) {
            userAccounts = newAccounts;
            preferences.putString("users", userAccounts);

            String searchStr = u + ",";
            int idx = reqChangeUsers.indexOf(searchStr);
            if(idx != -1) {
                reqChangeUsers = reqChangeUsers.substring(0, idx) + reqChangeUsers.substring(idx + searchStr.length());
                preferences.putString("reqChange", reqChangeUsers);
            }
            addLog("Cư dân " + u + " đã tự đổi mật khẩu bắt buộc");
            r->send(200, "text/plain", "OK");
        } else { r->send(200, "text/plain", "Lỗi cập nhật!"); }
    });

    server.on("/api/create", HTTP_GET, [](AsyncWebServerRequest *r){ 
        String au = r->getParam("u")->value(); 
        String ap = r->getParam("p")->value();
        if (au != "admin" || ap != "123456") { r->send(200, "text/plain", "Từ chối! Không có quyền Admin."); return; }
        
        String nu = r->getParam("newU")->value(); 
        String np = r->getParam("newP")->value();
        nu.replace(":", ""); nu.replace(",", ""); np.replace(":", ""); np.replace(",", "");
        
        String searchStr = nu + ":";
        if(userAccounts.indexOf(searchStr) == -1) {
            userAccounts += nu + ":" + np + ",";
            preferences.putString("users", userAccounts);
            
            if (reqChangeUsers.indexOf(nu + ",") == -1) {
                reqChangeUsers += nu + ",";
                preferences.putString("reqChange", reqChangeUsers);
            }

            addLog("Admin đã cấp tài khoản cho: " + nu);
            r->send(200, "text/plain", "OK");
            sendStateToWeb();
        } else { r->send(200, "text/plain", "Tên căn hộ này đã được cấp tài khoản!"); }
    });

    server.on("/api/reset_pass", HTTP_GET, [](AsyncWebServerRequest *r){
        if(r->getParam("u")->value() == "admin" && r->getParam("p")->value() == "123456") {
            String target = r->getParam("target")->value();
            String newP = r->getParam("newP")->value();

            newP.replace(":", ""); newP.replace(",", ""); 
            String newAccounts = ""; int startIdx = 0; bool found = false;
            
            while(startIdx < userAccounts.length()) {
                int commaIdx = userAccounts.indexOf(',', startIdx);
                if (commaIdx == -1) commaIdx = userAccounts.length();
                String entry = userAccounts.substring(startIdx, commaIdx);
                if (entry.length() > 0) {
                    if (entry.startsWith(target + ":")) {
                        newAccounts += target + ":" + newP + ",";
                        found = true;
                    } else { newAccounts += entry + ","; }
                }
                startIdx = commaIdx + 1;
            }

            if(found) {
                userAccounts = newAccounts;
                preferences.putString("users", userAccounts);
                
                if (reqChangeUsers.indexOf(target + ",") == -1) {
                    reqChangeUsers += target + ",";
                    preferences.putString("reqChange", reqChangeUsers);
                }

                addLog("Admin đã reset mật khẩu cho cư dân: " + target);
                r->send(200, "text/plain", "Đã đổi mật khẩu thành công!");
                sendStateToWeb();
            } else { r->send(200, "text/plain", "Lỗi: Không tìm thấy cư dân này!"); }
        } else { r->send(200, "text/plain", "Từ chối quyền truy cập!"); }
    });

    server.on("/api/delete_user", HTTP_GET, [](AsyncWebServerRequest *r){ 
        if(r->getParam("u")->value() == "admin" && r->getParam("p")->value() == "123456") {
            String target = r->getParam("target")->value();
            processDeleteUser(target);
            r->send(200, "text/plain", "OK");
            sendStateToWeb();
        }
    });

    server.on("/api/enroll", HTTP_GET, [](AsyncWebServerRequest *r){ 
        if(r->getParam("u")->value() == "admin" && r->getParam("p")->value() == "123456") {
            pendingName = r->getParam("target")->value();
            isEnrollMode = true;
            r->send(200, "text/plain", "OK");
            sendStateToWeb();
        }
    });

    server.on("/api/locker", HTTP_GET, [](AsyncWebServerRequest *r){ 
        String u = r->getParam("u")->value(); String p = r->getParam("p")->value();
        if(!checkLogin(u, p)) { r->send(200, "text/plain", "Lỗi xác thực!"); return; }
        
        // CHỐT CHẶN BẢO MẬT: TƯỚC QUYỀN MỞ TỦ CỦA ADMIN
        if(u == "admin" || u.equalsIgnoreCase("admin")) { 
            r->send(200, "text/plain", "Bảo mật: Admin không có quyền mở tủ của cư dân!"); return; 
        }

        int id = r->getParam("id")->value().toInt();
        if(isOpen) { r->send(200, "text/plain", "Hệ thống đang mở tủ khác!"); return; }
        if(lockerOcc[id] == true && lockerOwner[id] != u) {
            r->send(200, "text/plain", "Tủ này của người khác!"); return;
        }
        openSpecificLocker(id, u, false);
        r->send(200, "text/plain", "OK");
    });

    ws.onEvent([](AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
        if(type == WS_EVT_DATA) sendStateToWeb(); 
    });
    server.addHandler(&ws); server.begin();
}

unsigned long lastTimeUpdate = 0; 

void loop() {
    ws.cleanupClients(); 
    
    if (isOpen && (millis() - doorOpenedAt >= 10000)) {
        myStepper.step(-512); disableStepper(); 
        
        updateLCD("TU " + String(currentOpenLocker + 1) + " DA KHOA", "Khoa an toan!");
        
        isOpen = false; currentOpenLocker = -1;
        sendStateToWeb(); 
        
        lastTimeUpdate = millis() - 7000; 
    }
    
    if (millis() - lastTimeUpdate > 10000 && !isOpen) {
        if(isEnrollMode) updateLCD("DANG CHO THE...", pendingName);
        else showIdleLCD();
        lastTimeUpdate = millis();
    }
    
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
            uid += String(rfid.uid.uidByte[i], HEX);
        }
        uid.toUpperCase();

        if (isEnrollMode) { 
            String searchStr = uid + ":";
            if (authorizedCards.indexOf(searchStr) == -1) { 
                authorizedCards += uid + ":" + pendingName + ","; 
                preferences.putString("uids", authorizedCards); 
                updateLCD("CAP THE XONG!", pendingName);
                addLog("Đã gán thẻ vật lý cho: " + pendingName);
            } else { updateLCD("THE DA DUNG ROI!", ""); }
            isEnrollMode = false; pendingName = ""; doorOpenedAt = millis(); 
            sendStateToWeb();
        } 
        else if (!isOpen) { 
            String searchStr = uid + ":";
            int uidIndex = authorizedCards.indexOf(searchStr);
            if (uidIndex != -1) { 
                int nameStart = authorizedCards.indexOf(":", uidIndex) + 1;
                int nameEnd = authorizedCards.indexOf(",", uidIndex);
                if (nameEnd == -1) nameEnd = authorizedCards.length(); 
                String userName = authorizedCards.substring(nameStart, nameEnd);
                
                int targetLocker = -1;
                for(int i=0; i<NUM_LOCKERS; i++) {
                    if(lockerOcc[i] && lockerOwner[i] == userName) { targetLocker = i; break; }
                }
                
                if(targetLocker != -1) {
                    openSpecificLocker(targetLocker, userName, true);
                } else {
                    for(int i=0; i<NUM_LOCKERS; i++) {
                        if(!lockerOcc[i]) { targetLocker = i; break; }
                    }
                    if(targetLocker != -1) openSpecificLocker(targetLocker, userName, true);
                    else { updateLCD("HET TU TRONG!", ""); doorOpenedAt = millis() - 2000; }
                }
            } else {
                updateLCD("SAI THE!", "");
                addLog("<span style='color:#e74c3c;'>Cảnh báo: Thẻ lạ (" + uid + ") vừa quẹt</span>"); 
                doorOpenedAt = millis() - 2000; sendStateToWeb();
            }
        }
        rfid.PICC_HaltA(); 
    }
    delay(5); 
}
