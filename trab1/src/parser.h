#ifndef PARSER_H
#define PARSER_H
#include "bundle.h"

#include <stdlib.h>
#include <stdio.h>

void print_usage(char **argv);
Bundle** parse_args(int argc, char **argv, size_t *NBundles);

#endif

