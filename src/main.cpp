// ====================================================================
// MASTER SWITCH DE COMPILACIÓN
// Cambia esto a 'true' para inyectar el código de test de botones y MAC
// Cambia esto a 'false' para inyectar el código del Proxy Modbus Real
// ====================================================================

#define MODO_TEST_HARDWARE false

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