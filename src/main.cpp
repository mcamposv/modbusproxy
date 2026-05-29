// ====================================================================
// MASTER SWITCH DE COMPILACIÓN
// Cambia esto a 'true' para inyectar el código de test de botones y MAC
// Cambia esto a 'false' para inyectar el código del Proxy Modbus Real
// ====================================================================

#define MODO_TEST_HARDWARE false

// ====================================================================
// FORZAR MODO SETUP EN EL SIGUIENTE ARRANQUE (independiente de NVS)
// Cambia esto a 'true' para entrar siempre en modo Setup al arrancar.
// El modo Setup normal se activa via Factory Reset desde la web /config
// o automaticamente en el primer arranque (dispositivo sin configurar).
// ====================================================================

#define FORCE_SETUP false

// ====================================================================
// MOTOR DE INYECCIÓN DE CÓDIGO (Directivas de Preprocesador)
// No tocar a menos que cambies el nombre de los archivos
// ====================================================================

#if MODO_TEST_HARDWARE == true
    // El compilador solo pegará aquí el código del test
    #include "test_hardware.hpp"
#else
    // El compilador solo pegará aquí el código del proxy real
    #include "proxy_operativo.hpp"
#endif

// Nota: No necesitas añadir void setup() ni void loop() aquí,
// ya vienen incluidos dentro de los archivos .hpp correspondientes.