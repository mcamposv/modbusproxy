// ============================================================================
// 🔋 PROTOTIPO PROXY HUAWEI: PLANCHA DE SOPORTE (V3 - TETONES INTERIORES 2.5mm)
// ============================================================================
// - Frontal completamente liso y cerrado (letras en relieve hacia fuera).
// - Trasera con los cajeados para encastrar componentes por detrás.
// - Tetones de 2.5mm orientados hacia el INTERIOR del rebaje (dirección caja).
// ============================================================================

$fn = 64; // Calidad de renderizado para círculos

// --- VARIABLES GLOBALES DE CONFIGURACIÓN ---
ancho_plancha    = 135;  // Ancho total de la placa de pruebas
alto_plancha     = 65;   // Alto total de la placa de pruebas
espesor_plancha  = 3.5;  // Espesor total (permite rigidez y recesos estables)
prof_receso      = 1.8;  // Profundidad del cajeado trasero para empotrar componentes

// --- MEDIDAS COMPONENTES (HUAWEI PROXY) ---
// Pantalla OLED (Según especificaciones)
pantalla_x         = 95;    // Posición centro X de la pantalla
pantalla_y         = 32.5;  // Posición centro Y de la pantalla
pantalla_ext_w     = 33.7;  // Ancho exterior módulo
pantalla_ext_h     = 35.4;  // Alto exterior módulo
pantalla_vent_w    = 24.0;  // Ancho de la ventana visible del cristal
pantalla_vent_h    = 19.0;  // Alto de la ventana visible del cristal
pantalla_holes_w   = 30.6;  // Distancia horizontal entre agujeros
pantalla_holes_h   = 28.5;  // Distancia vertical entre agujeros
pantalla_pin_dia   = 2.5;   // ¡CORREGIDO! Diámetro del tetón a 2.5mm
pantalla_pin_h     = 2.0;   // Longitud del tetón (recorre los 1.8mm de profundidad y sobresale 1mm)

// Pulsadores 12x12x4mm (CRUCETA COMPACTA)
cruz_x             = 42;    // Centro X de la disposición en cruz
cruz_y             = 32.5;  // Centro Y de la disposición en cruz
dist_cruz          = 14.5;  // Distancia compacta desde el centro a cada pulsador
boton_cuerpo       = 12.3;  // Ancho cajeado (12mm + 0.3mm tolerancia impresión)
boton_vasto_dia    = 6.0;   // Agujero pasante para el capuchón/vástago redondo
prog_x             = 15;    // Posición X botón de Flasheo (PROG)
prog_y             = 51;    // Posición Y botón de Flasheo (PROG)

// ============================================================================
// MOTORES GRÁFICOS / MÓDULOS
// ============================================================================

module plancha_solida() {
    // La placa base nace en Z=0 y sube hasta Z=espesor_plancha (3.5mm)
    cube([ancho_plancha, alto_plancha, espesor_plancha]);
}

module rotulos_relieve() {
    // Letras y símbolos en relieve en la cara frontal exterior (Z = 3.5mm hacia arriba)
    color("DarkSlateGray") {
        
        // Botón MÁS (+) -> Arriba
        translate([cruz_x, cruz_y + dist_cruz + 7.5, espesor_plancha])
            linear_extrude(height = 0.8)
                text("+", size = 7, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
                
        // Botón MENOS (-) -> Abajo
        translate([cruz_x, cruz_y - dist_cruz - 7.5, espesor_plancha])
            linear_extrude(height = 0.8)
                text("-", size = 7, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
                
        // Botón OK -> Derecha
        translate([cruz_x + dist_cruz + 8.5, cruz_y - 1, espesor_plancha])
            linear_extrude(height = 0.8)
                text("OK", size = 4.5, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
                
        // Botón ATRÁS -> Izquierda
        translate([cruz_x - dist_cruz - 8.5, cruz_y - 1, espesor_plancha])
            linear_extrude(height = 0.8)
                text("<-", size = 4.5, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
                
        // Botón FLASH -> Esquina Superior Izquierda (PROG)
        translate([prog_x, prog_y - 8, espesor_plancha])
            linear_extrude(height = 0.8)
                text("PROG", size = 4, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
    }
}

module vaciados_y_muescas() {
    // Los rebajes se comen la placa por detrás (nacen en Z=-0.1 y suben hasta prof_receso)
    
    // Ventana pasante para el cristal de la pantalla
    translate([pantalla_x - pantalla_vent_w/2, pantalla_y - pantalla_vent_h/2, -1])
        cube([pantalla_vent_w, pantalla_vent_h, espesor_plancha + 2]);
        
    // Cajeado trasero para encastrar el contorno del PCB de la pantalla
    translate([pantalla_x - pantalla_ext_w/2, pantalla_y - pantalla_ext_h/2, -0.1])
        cube([pantalla_ext_w, pantalla_ext_h, prof_receso + 0.1]);

    // Cortes para los 5 pulsadores
    coordenadas_botones = [
        [cruz_x, cruz_y + dist_cruz], // Botón Superior (+)
        [cruz_x, cruz_y - dist_cruz], // Botón Inferior (-)
        [cruz_x - dist_cruz, cruz_y], // Botón Izquierdo (Atrás)
        [cruz_x + dist_cruz, cruz_y], // Botón Derecho (OK)
        [prog_x, prog_y]              // Botón Flash (PROG)
    ];
    
    for (pos = coordenadas_botones) {
        // Agujero pasante redondo para el botón físico
        translate([pos[0], pos[1], -1])
            cylinder(d = boton_vasto_dia, h = espesor_plancha + 2);
            
        // Receso cuadrado trasero para alojar el cuerpo de 12x12mm
        translate([pos[0] - boton_cuerpo/2, pos[1] - boton_cuerpo/2, -0.1])
            cube([boton_cuerpo, boton_cuerpo, prof_receso + 0.1]);
    }
}

module tetones_fijacion_interna() {
    // ¡CORREGIDO! Los pinchitos nacen en el techo del rebaje (Z = prof_receso)
    // y crecen hacia ABAJO (restando su altura), apuntando hacia el interior de la caja.
    color("CadetBlue") {
        for (mx = [-1, 1]) {
            for (my = [-1, 1]) {
                translate([pantalla_x + mx * pantalla_holes_w/2, pantalla_y + my * pantalla_holes_h/2, prof_receso - pantalla_pin_h])
                    cylinder(d = pantalla_pin_dia, h = pantalla_pin_h);
            }
        }
    }
}

// ============================================================================
// ENSAMBLAJE FINAL (OPERACIONES BOOLEANAS)
// ============================================================================
difference() {
    plancha_solida();
    vaciados_y_muescas();
}
tetones_fijacion_interna();
rotulos_relieve();