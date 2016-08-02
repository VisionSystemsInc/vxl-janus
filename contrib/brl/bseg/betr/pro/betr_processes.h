#ifndef betr_processes_h_
#define betr_processes_h_

#include <bprb/bprb_func_process.h>
#include <bprb/bprb_macros.h>

//the init functions
DECLARE_FUNC_CONS(betr_create_site_process);
DECLARE_FUNC_CONS(betr_create_event_trigger_process);
// add objects
DECLARE_FUNC_CONS(betr_add_site_object_process);
DECLARE_FUNC_CONS(betr_add_event_trigger_process);
DECLARE_FUNC_CONS(betr_add_event_trigger_object_process);
DECLARE_FUNC_CONS(betr_set_event_trigger_data_process);
DECLARE_FUNC_CONS(betr_execute_event_trigger_process);

#endif