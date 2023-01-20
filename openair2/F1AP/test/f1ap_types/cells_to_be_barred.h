#ifndef F1AP_CELLS_TO_BE_BARRED_H
#define F1AP_CELLS_TO_BE_BARRED_H 

#include "nr_cgi.h"

#include <stdbool.h>

typedef enum{

  BARRED_CELL_BARRED,
  NOT_BARRED_CELL_BARRED,
  END_CELL_BARRED

} cell_barred_e;

bool eq_cell_barred(cell_barred_e const* m0, cell_barred_e const* m1);

typedef struct{
  // NR CGI 9.3.1.12
  // Mandatory
  nr_cgi_t nr_cgi;

  // Cell Barred 
  // Mandatory
  cell_barred_e cell_barred;

  // IAB Barred 
  // Optional
  cell_barred_e* iab_barred;

} cells_to_be_barred_t;

void free_cells_to_be_barred(cells_to_be_barred_t* src);

bool eq_cells_to_be_barred(cells_to_be_barred_t const* m0, cells_to_be_barred_t const* m1);

#endif
