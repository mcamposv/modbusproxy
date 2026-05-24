// ====================================================================
// CALIBRE DE PRUEBA PARA PULSADOR CUADRADO (v1.1 Corregida)
// ====================================================================

// Medidas de la plancha exterior
ancho_plancha = 30;
largo_plancha = 30;
espesor_plancha = 3;

// Medidas nominales del pulsador
hueco_boton = 13; 

// AJUSTE DE TOLERANCIA (Modifica esto según el desajuste de tu impresora)
// Ejemplos: 0.0 (exacto), 0.1 (un pelín más grande), 0.2 (holgado)
tolerancia = 0.0; 

// CÁLCULOS: Siempre fuera de los bloques de geometría
medida_final = hueco_boton + (tolerancia * 2);

// ====================================================================
// OBJETO 3D
// ====================================================================
difference() {
    // 1. Cuerpo principal de la plancha (Cubo base)
    cube([ancho_plancha, largo_plancha, espesor_plancha], center = true);
    
    // 2. Agujero pasante con la tolerancia aplicada (Cubo de recorte)
    cube([medida_final, medida_final, espesor_plancha + 2], center = true);
}