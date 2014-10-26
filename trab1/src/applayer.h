#ifndef APP_LAYER_H
#define APP_LAYER_H

#include "useful.h"
#include "bundle.h"

/**
 * @desc Initializes appLayer struct and linkLayer struct
 * @arg Bundle* bundle: Parsed link and app layer settings
 * @return Retorna um número positivo em caso de sucesso e negativo em caso de erro
 */
int initAppLayer(Bundle *bundle);


#endif

