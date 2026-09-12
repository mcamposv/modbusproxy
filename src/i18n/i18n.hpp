// ====================================================================
// i18n — Selector de idioma activo
//
// Uso: llamar a i18nInit(cfg.lang) después de cargar la configuración.
// Después usar L->campo en los handlers web.
// ====================================================================
#pragma once
#include "es.hpp"
#include "en.hpp"
#include "de.hpp"

static const LangStrings* L = &LANG_ES;

inline void i18nInit(const char* lang) {
    if (strncmp(lang, "en", 2) == 0)      L = &LANG_EN;
    else if (strncmp(lang, "de", 2) == 0) L = &LANG_DE;
    else                                   L = &LANG_ES;
}
