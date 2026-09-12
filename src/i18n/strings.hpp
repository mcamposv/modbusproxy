// ====================================================================
// i18n — Definición del struct de cadenas traducibles
//
// Para añadir un idioma nuevo:
//   1. Copia src/i18n/en.hpp → src/i18n/xx.hpp
//   2. Renombra el struct a LANG_XX
//   3. Traduce los valores de cada campo
//   4. Añade #include y caso en i18n.hpp
// ====================================================================
#pragma once

struct LangStrings {
    const char* html_lang;   // atributo lang del <html> ("es", "en", "de")

    // --- Portal de setup ---
    const char* setup_page_title;
    const char* setup_header;
    const char* setup_h1;
    const char* setup_info_1;       // precede al nombre del AP
    const char* setup_info_2;       // sigue al nombre del AP
    const char* setup_warn;
    const char* setup_h3_net;
    const char* setup_conn_type;
    const char* setup_wifi;
    const char* setup_ethernet;
    const char* setup_h3_wifi_creds;
    const char* setup_available_nets;
    const char* setup_scan_btn;
    const char* setup_ssid_label;
    const char* setup_ssid_placeholder;
    const char* setup_pass_label;
    const char* setup_pass_placeholder;
    const char* setup_h3_ip;
    const char* setup_ip_method;
    const char* setup_dhcp;
    const char* setup_static;
    const char* setup_proxy_ip;
    const char* setup_gateway;
    const char* setup_subnet;
    const char* setup_dns;
    const char* setup_h3_modbus;
    const char* setup_modbus_ip;
    const char* setup_modbus_port;
    const char* setup_save_btn;
    const char* setup_js_scanning;
    const char* setup_js_searching;
    const char* setup_js_no_nets;
    const char* setup_js_error;       // prefijo antes de e.message
    const char* setup_saved_title;
    const char* setup_saved_h1;
    const char* setup_saved_msg;
    const char* setup_saved_hint;
    const char* setup_oled_no_pass;   // texto en pantalla OLED

    // --- Barra de navegación ---
    const char* nav_dashboard;
    const char* nav_log;
    const char* nav_config;

    // --- Página de reinicio ---
    const char* reboot_title;
    const char* reboot_h1;
    const char* reboot_msg;
    const char* reboot_redirect;

    // --- Página de factory reset ---
    const char* factory_h1;
    const char* factory_msg;
    const char* factory_hint_pre;    // precede al nombre del AP
    const char* factory_hint_post;   // sigue al nombre del AP

    // --- Página de configuración ---
    const char* cfg_title;
    const char* cfg_h1;
    const char* cfg_warn;
    const char* cfg_h3_net;
    const char* cfg_conn_type;
    const char* cfg_wifi;
    const char* cfg_ethernet;
    const char* cfg_ip_assign;
    const char* cfg_static;
    const char* cfg_dhcp;
    const char* cfg_rotate;
    const char* cfg_rotate_no;
    const char* cfg_rotate_yes;
    const char* cfg_h3_wifi;
    const char* cfg_wifi_note;
    const char* cfg_available_nets;
    const char* cfg_scan_btn;
    const char* cfg_ssid;
    const char* cfg_pass;
    const char* cfg_h3_modbus;
    const char* cfg_modbus_ip;
    const char* cfg_modbus_port;
    const char* cfg_h3_static;
    const char* cfg_static_note;
    const char* cfg_proxy_ip;
    const char* cfg_gateway;
    const char* cfg_subnet;
    const char* cfg_dns;
    const char* cfg_h3_ota;
    const char* cfg_ota_pass;
    const char* cfg_h3_lang;         // encabezado de sección de idioma
    const char* cfg_save_btn;
    const char* cfg_reboot_btn;
    const char* cfg_danger_h3;
    const char* cfg_danger_desc_pre;   // precede al nombre del AP
    const char* cfg_danger_desc_post;  // sigue al nombre del AP
    const char* cfg_factory_btn;
    const char* cfg_factory_confirm;   // texto del confirm() JS
    const char* cfg_js_scanning;
    const char* cfg_js_searching;
    const char* cfg_js_no_nets;
    const char* cfg_js_error;          // prefijo antes de e.message
    const char* cfg_saved_title;
    const char* cfg_saved_h1;
    const char* cfg_saved_msg;
    const char* cfg_saved_redirect;

    // --- Dashboard ---
    const char* dash_title;
    const char* dash_h1;
    const char* dash_state_shutdown;
    const char* dash_h3_status;
    const char* dash_tunnel;
    const char* dash_device;
    const char* dash_connections;
    const char* dash_h3_debug;
    const char* dash_attempts;
    const char* dash_phase;
    const char* dash_sent;
    const char* dash_recv;
    const char* dash_diag;
    const char* dash_h3_clients;
    const char* dash_col_ip;
    const char* dash_col_req;
    const char* dash_col_last;
    const char* dash_no_clients;
    const char* dash_time_ago_fmt;   // formato printf con %lu para segundos
    const char* dash_shutdown_btn;
    const char* dash_passive_msg;

    // --- Página de apagado ---
    const char* shutdown_title;
    const char* shutdown_h1;
    const char* shutdown_msg1;
    const char* shutdown_msg2;

    // --- Página de log Modbus ---
    const char* log_title;
    const char* log_h1;
    const char* log_entries;
    const char* log_autorefresh;
    const char* log_export_csv;
    const char* log_no_entries;
    const char* log_mutex_err;
    const char* log_col_num;
    const char* log_col_time;
    const char* log_col_src;
    const char* log_col_dst;
    const char* log_col_unit;
    const char* log_col_func;
    const char* log_col_reg;
    const char* log_col_qty;
    const char* log_col_req;
    const char* log_col_resp;
    const char* log_col_exc;
    const char* log_col_req_hex;
    const char* log_col_resp_hex;
};
