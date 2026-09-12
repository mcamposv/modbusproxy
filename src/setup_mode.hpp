// ====================================================================
// MODO SETUP — Portal cautivo WiFi para configuracion inicial
//
// Se activa cuando:
//   - NVS no tiene "cfgOk"=true  (dispositivo nunca configurado)
//   - "runSetup"=true en NVS     (factory reset solicitado)
//   - FORCE_SETUP = true en main.cpp (forzado en compilacion)
//
// Comportamiento:
//   - Levanta un AP WiFi abierto: "modbusproxy-<version>"
//   - IP del ESP32: 192.168.1.1
//   - DNS server redirige cualquier dominio a 192.168.1.1 (portal cautivo)
//   - Pagina web de setup con escaneo de redes WiFi y entrada manual
//   - Al guardar: escribe config en NVS, marca cfgOk, reinicia en modo proxy
// ====================================================================

#pragma once

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

// --- Configuracion del AP de setup ---
static const IPAddress SETUP_AP_IP   (192, 168, 1, 1);
static const IPAddress SETUP_AP_GW   (192, 168, 1, 1);
static const IPAddress SETUP_AP_MASK (255, 255, 255, 0);

static DNSServer  setupDnsServer;
static WebServer  setupWebServer(80);

// Declaraciones adelantadas
static void handleSetupRoot();
static void handleSetupScan();
static void handleSetupSave();

// ====================================================================
// Pagina HTML del modo setup
// ====================================================================
static void handleSetupRoot() {
    String apName = "modbusproxy-" + FIRMWARE_VERSION;

    String html = "<!DOCTYPE html><html lang='";
    html += L->html_lang;
    html += "'><head><meta charset='UTF-8'>";
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1.0'>");
    html += "<title>";
    html += L->setup_page_title;
    html += " v";
    html += FIRMWARE_VERSION;
    html += F("</title><style>");
    html += F("*{box-sizing:border-box;}");
    html += F("body{background:#121212;color:#e0e0e0;font-family:'Segoe UI',sans-serif;margin:0;padding:0;}");
    html += F(".wrap{max-width:660px;margin:20px auto;padding:0 16px 50px;}");
    html += F("h1{color:#4da6ff;border-bottom:1px solid #333;padding-bottom:8px;margin-top:0;}");
    html += F("h3{color:#a0c4ff;border-bottom:1px solid #222;padding-bottom:6px;margin-top:28px;}");
    html += F(".form-group{margin-bottom:16px;}");
    html += F("label.lbl{display:block;font-size:13px;color:#aaa;margin-bottom:5px;font-weight:600;}");
    html += F("input[type=text],input[type=password],input[type=number]");
    html += F("{width:100%;padding:9px 12px;background:#1e1e1e;border:1px solid #333;");
    html += F("color:#e0e0e0;border-radius:5px;font-size:14px;}");
    html += F("input:focus{outline:none;border-color:#4da6ff;box-shadow:0 0 0 2px rgba(77,166,255,.2);}");
    html += F(".radio-group{display:flex;gap:20px;margin-top:4px;flex-wrap:wrap;}");
    html += F(".radio-group label{display:flex;align-items:center;gap:6px;font-size:14px;");
    html += F("color:#e0e0e0;font-weight:400;cursor:pointer;}");
    html += F(".warn{background:#3d2f00;border:1px solid #f39c12;color:#ffe082;");
    html += F("padding:12px 16px;border-radius:6px;margin-bottom:20px;font-size:14px;}");
    html += F(".info{background:#0a2a3d;border:1px solid #17a2b8;color:#80dfff;");
    html += F("padding:12px 16px;border-radius:6px;margin-bottom:20px;font-size:14px;}");
    html += F(".btn-save{display:block;width:100%;padding:12px;background:#28a745;color:#fff;");
    html += F("border:none;border-radius:6px;font-size:16px;font-weight:700;cursor:pointer;margin-top:28px;}");
    html += F(".btn-save:hover{background:#1e8035;}");
    html += F(".btn-scan{padding:9px 18px;background:#17a2b8;color:#fff;border:none;");
    html += F("border-radius:5px;font-size:14px;font-weight:600;cursor:pointer;}");
    html += F(".btn-scan:disabled{opacity:0.6;cursor:wait;}");
    html += F(".btn-scan:hover:not(:disabled){background:#138496;}");
    html += F(".section-note{font-size:12px;color:#666;margin-top:4px;}");
    // Lista de redes escaneadas
    html += F("#netList{margin-top:12px;border-radius:5px;overflow:hidden;border:1px solid #2a2a2a;}");
    html += F("#netList:empty{display:none;}");
    html += F(".net-item{padding:10px 14px;background:#1a1a1a;border-bottom:1px solid #2a2a2a;");
    html += F("cursor:pointer;display:flex;align-items:center;gap:10px;transition:background .15s;}");
    html += F(".net-item:last-child{border-bottom:none;}");
    html += F(".net-item:hover,.net-item.selected{background:#1e3a5a;}");
    html += F(".net-signal{font-size:18px;line-height:1;min-width:52px;letter-spacing:1px;}");
    html += F(".net-ssid{flex:1;font-size:14px;color:#e0e0e0;word-break:break-all;}");
    html += F(".net-lock{font-size:14px;}");
    html += F(".net-dbm{font-size:11px;color:#666;white-space:nowrap;}");
    html += F(".scan-msg{padding:10px 14px;color:#888;font-size:13px;font-style:italic;}");
    html += F("</style></head><body>");

    // Header
    html += F("<div style='background:#1a1a2e;padding:10px 20px;border-bottom:2px solid #4da6ff;'>");
    html += "<span style='color:#4da6ff;font-weight:700;font-size:15px;'>";
    html += L->setup_header;
    html += " v";
    html += FIRMWARE_VERSION;
    html += F("</span></div>");

    html += F("<div class='wrap'><h1>");
    html += L->setup_h1;
    html += F("</h1>");

    html += "<div class='info'>";
    html += L->setup_info_1;
    html += apName;
    html += L->setup_info_2;
    html += "</div>";

    html += "<div class='warn'>";
    html += L->setup_warn;
    html += "</div>";

    html += F("<form method='POST' action='/save' id='frm'>");

    // ---- Interfaz de red ----
    html += "<h3>";
    html += L->setup_h3_net;
    html += "</h3><div class='form-group'><label class='lbl'>";
    html += L->setup_conn_type;
    html += F("</label><div class='radio-group'>");
    html += F("<label><input type='radio' name='useEth' value='0' checked id='r_wifi'");
    html += F(" onchange='toggleIface()'> ");
    html += L->setup_wifi;
    html += F("</label><label><input type='radio' name='useEth' value='1' id='r_eth'");
    html += F(" onchange='toggleIface()'> ");
    html += L->setup_ethernet;
    html += F("</label></div></div>");

    // ---- Credenciales WiFi ----
    html += F("<div id='wifiSection'><h3>");
    html += L->setup_h3_wifi_creds;
    html += F("</h3>");

    html += F("<div class='form-group'><label class='lbl'>");
    html += L->setup_available_nets;
    html += "</label><button type='button' class='btn-scan' id='scanBtn' onclick='doScan()'>";
    html += L->setup_scan_btn;
    html += F("</button><div id='netList'></div></div>");

    html += F("<div class='form-group'><label class='lbl' for='wifiSSID'>");
    html += L->setup_ssid_label;
    html += F("</label><input type='text' id='wifiSSID' name='wifiSSID' maxlength='63' placeholder='");
    html += L->setup_ssid_placeholder;
    html += F("'></div>");

    html += F("<div class='form-group'><label class='lbl' for='wifiPass'>");
    html += L->setup_pass_label;
    html += F("</label><input type='password' id='wifiPass' name='wifiPass' maxlength='63' placeholder='");
    html += L->setup_pass_placeholder;
    html += F("'></div></div>"); // fin wifiSection

    // ---- Asignacion de IP ----
    html += "<h3>";
    html += L->setup_h3_ip;
    html += "</h3><div class='form-group'><label class='lbl'>";
    html += L->setup_ip_method;
    html += F("</label><div class='radio-group'>");
    html += F("<label><input type='radio' name='useDHCP' value='1' checked id='r_dhcp'");
    html += F(" onchange='toggleDHCP()'> ");
    html += L->setup_dhcp;
    html += F("</label><label><input type='radio' name='useDHCP' value='0' id='r_static'");
    html += F(" onchange='toggleDHCP()'> ");
    html += L->setup_static;
    html += F("</label></div></div>");

    // ---- IP Estatica ----
    html += F("<div id='staticSection' style='display:none'>");
    html += F("<div class='form-group'><label class='lbl' for='localIP'>");
    html += L->setup_proxy_ip;
    html += "</label><input type='text' id='localIP' name='localIP' maxlength='15' value='";
    html += cfg.localIP;
    html += F("'></div><div class='form-group'><label class='lbl' for='gw'>");
    html += L->setup_gateway;
    html += "</label><input type='text' id='gw' name='gw' maxlength='15' value='";
    html += cfg.gateway;
    html += F("'></div><div class='form-group'><label class='lbl' for='sn'>");
    html += L->setup_subnet;
    html += "</label><input type='text' id='sn' name='sn' maxlength='15' value='";
    html += cfg.subnet;
    html += F("'></div><div class='form-group'><label class='lbl' for='dns'>");
    html += L->setup_dns;
    html += "</label><input type='text' id='dns' name='dns' maxlength='15' value='";
    html += cfg.dns;
    html += F("'></div></div>"); // fin staticSection

    // ---- Destino Modbus ----
    html += "<h3>";
    html += L->setup_h3_modbus;
    html += F("</h3><div class='form-group'><label class='lbl' for='modbusIP'>");
    html += L->setup_modbus_ip;
    html += "</label><input type='text' id='modbusIP' name='modbusIP' maxlength='15' value='";
    html += cfg.modbusIP;
    html += F("'></div><div class='form-group'><label class='lbl' for='modbusPort'>");
    html += L->setup_modbus_port;
    html += "</label><input type='number' id='modbusPort' name='modbusPort' min='1' max='65535' value='";
    html += String(cfg.modbusPort);
    html += F("'></div>");

    html += "<button type='submit' class='btn-save'>";
    html += L->setup_save_btn;
    html += "</button>";
    html += F("</form>");
    html += F("</div>"); // fin wrap

    // ---- JavaScript ----
    html += F("<script>");

    // Toggle WiFi/Ethernet section
    html += F("function toggleIface(){");
    html += F("document.getElementById('wifiSection').style.display=");
    html += F("document.getElementById('r_wifi').checked?'':'none';}");

    // Toggle static IP section
    html += F("function toggleDHCP(){");
    html += F("document.getElementById('staticSection').style.display=");
    html += F("document.getElementById('r_static').checked?'':'none';}");

    // Convierte RSSI a barras de senal con colores (4 barras)
    html += F("function sigBars(rssi){");
    html += F("var bars=rssi>=-50?4:rssi>=-65?3:rssi>=-75?2:1;");
    html += F("var col=bars>=3?'#28a745':bars==2?'#f39c12':'#dc3545';");
    html += F("var s='';");
    html += F("for(var i=0;i<4;i++){");
    html += F("s+='<span style=\"color:'+(i<bars?col:'#333')+';font-size:18px\">&#9646;</span>';}");
    html += F("return s;}");

    // Escaneo AJAX
    html += F("function doScan(){");
    html += F("var btn=document.getElementById('scanBtn');");
    html += F("var lst=document.getElementById('netList');");
    html += "btn.disabled=true;btn.textContent='";
    html += L->setup_js_scanning;
    html += "';lst.innerHTML='<div class=\"scan-msg\">";
    html += L->setup_js_searching;
    html += "</div>';";
    html += F("fetch('/scan')");
    html += F(".then(function(r){");
    html += F("if(!r.ok)throw new Error('HTTP '+r.status);");
    html += F("return r.json();})");
    html += F(".then(function(nets){");
    html += F("lst.innerHTML='';");
    html += F("if(!nets||nets.length===0){");
    html += "lst.innerHTML='<div class=\"scan-msg\">";
    html += L->setup_js_no_nets;
    html += F("</div>';return;}");
    html += F("nets.forEach(function(n){");
    html += F("var d=document.createElement('div');");
    html += F("d.className='net-item';");
    html += F("var lock=n.enc?'&#128274;':'&#128275;';");
    html += F("d.innerHTML=");
    html += F("'<span class=\"net-signal\">'+sigBars(n.rssi)+'</span>'");
    html += F("+'<span class=\"net-ssid\">'+n.ssid+'</span>'");
    html += F("+'<span class=\"net-lock\">'+lock+'</span>'");
    html += F("+'<span class=\"net-dbm\">'+n.rssi+'&nbsp;dBm</span>';");
    html += F("(function(ssid){");
    html += F("d.onclick=function(){");
    html += F("document.querySelectorAll('.net-item').forEach(function(x){x.classList.remove('selected');});");
    html += F("d.classList.add('selected');");
    html += F("document.getElementById('wifiSSID').value=ssid;");
    html += F("document.getElementById('wifiPass').focus();};");
    html += F("})(n.ssid);");
    html += F("lst.appendChild(d);});})");
    html += F(".catch(function(e){");
    html += "lst.innerHTML='<div class=\"scan-msg\">";
    html += L->setup_js_error;
    html += "'+e.message+'</div>';})";
    html += F(".finally(function(){");
    html += "btn.disabled=false;btn.textContent='";
    html += L->setup_scan_btn;
    html += F("';});");
    html += F("}");

    html += F("</script></body></html>");

    setupWebServer.send(200, "text/html", html);
}

// ====================================================================
// Escaneo de redes WiFi — devuelve JSON ordenado por RSSI
// ====================================================================
static void handleSetupScan() {
    // Escaneo sincrono; bloquea ~2-4 segundos pero el browser espera
    int n = WiFi.scanNetworks(false, false);  // async=false, show_hidden=false

    String json = "[";
    bool first = true;

    if (n > 0) {
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue; // ignorar SSIDs vacios/ocultos

            if (!first) json += ",";
            first = false;

            // Escapar caracteres especiales en SSID
            ssid.replace("\\", "\\\\");
            ssid.replace("\"", "\\\"");

            json += "{\"ssid\":\"";
            json += ssid;
            json += "\",\"rssi\":";
            json += String(WiFi.RSSI(i));
            json += ",\"enc\":";
            json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "1" : "0";
            json += "}";
        }
    }
    json += "]";
    WiFi.scanDelete();

    setupWebServer.sendHeader("Cache-Control", "no-cache");
    setupWebServer.send(200, "application/json", json);
}

// ====================================================================
// Guardar configuracion y salir del modo setup
// ====================================================================
static void handleSetupSave() {
    Preferences p;
    p.begin("proxycfg", false);

    bool useEth  = (setupWebServer.arg("useEth")  == "1");
    bool useDHCP = (setupWebServer.arg("useDHCP") == "1");
    p.putBool("useEth",  useEth);
    p.putBool("useDHCP", useDHCP);

    String ssid = setupWebServer.arg("wifiSSID"); ssid.trim();
    if (ssid.length() > 0) p.putString("wifiSSID", ssid.substring(0, 63));

    String pass = setupWebServer.arg("wifiPass");  // puede estar vacia (red abierta)
    p.putString("wifiPass", pass.substring(0, 63));

    String localIP = setupWebServer.arg("localIP"); localIP.trim();
    if (localIP.length() > 0) p.putString("localIP", localIP.substring(0, 15));

    String gw = setupWebServer.arg("gw"); gw.trim();
    if (gw.length() > 0) p.putString("gateway", gw.substring(0, 15));

    String sn = setupWebServer.arg("sn"); sn.trim();
    if (sn.length() > 0) p.putString("subnet", sn.substring(0, 15));

    String dns = setupWebServer.arg("dns"); dns.trim();
    if (dns.length() > 0) p.putString("dns", dns.substring(0, 15));

    String modbusIP = setupWebServer.arg("modbusIP"); modbusIP.trim();
    if (modbusIP.length() > 0) p.putString("modbusIP", modbusIP.substring(0, 15));

    int port = setupWebServer.arg("modbusPort").toInt();
    if (port > 0 && port <= 65535) p.putUShort("modbusPort", (uint16_t)port);

    p.putBool("runSetup", false);
    p.end();

    Serial.println("[SETUP] Configuracion guardada. Reiniciando en modo proxy...");

    String html = "<!DOCTYPE html><html lang='";
    html += L->html_lang;
    html += F("'><head><meta charset='UTF-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1.0'>");
    html += "<title>";
    html += L->setup_saved_title;
    html += F("</title><style>body{background:#121212;color:#fff;font-family:sans-serif;");
    html += F("text-align:center;padding-top:12%;}h1{color:#28a745;}</style>");
    html += F("</head><body><h1>");
    html += L->setup_saved_h1;
    html += "</h1><p>";
    html += L->setup_saved_msg;
    html += "</p><p style='color:#888;font-size:13px;'>";
    html += L->setup_saved_hint;
    html += F("</p></body></html>");
    setupWebServer.send(200, "text/html", html);

    delay(1500);
    ESP.restart();
}

// ====================================================================
// Funcion principal del modo setup — nunca retorna
// ====================================================================
static void runSetupMode() {
    Serial.println();
    Serial.println("============================================");
    Serial.println("  MODO SETUP ACTIVO");
    Serial.println("============================================");

    String apName = "modbusproxy-" + FIRMWARE_VERSION;

    // Mostrar info en OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("*** MODO SETUP ***");
    display.setCursor(0, 14);
    display.print("AP: ");
    display.println(apName.substring(0, 20));
    display.setCursor(0, 28);
    display.println("IP: 192.168.1.1");
    display.setCursor(0, 42);
    display.println(L->setup_oled_no_pass);
    display.display();

    // AP + STA para poder hacer scan de redes mientras el AP esta activo
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(SETUP_AP_IP, SETUP_AP_GW, SETUP_AP_MASK);
    WiFi.softAP(apName.c_str());

    Serial.printf("  AP SSID : %s\n", apName.c_str());
    Serial.printf("  AP IP   : 192.168.1.1\n");
    Serial.println("============================================");

    // DNS server: redirige cualquier dominio al ESP (activa popup de portal cautivo)
    setupDnsServer.start(53, "*", SETUP_AP_IP);

    setupWebServer.on("/",     HTTP_GET,  handleSetupRoot);
    setupWebServer.on("/scan", HTTP_GET,  handleSetupScan);
    setupWebServer.on("/save", HTTP_POST, handleSetupSave);

    // Captive portal: cualquier URL desconocida → pagina de setup
    setupWebServer.onNotFound([]() {
        setupWebServer.sendHeader("Location", "http://192.168.1.1/", true);
        setupWebServer.send(302, "text/plain", "");
    });

    setupWebServer.begin();

    // Bucle principal — no retorna nunca
    while (true) {
        setupDnsServer.processNextRequest();
        setupWebServer.handleClient();
        delay(10);
    }
}
