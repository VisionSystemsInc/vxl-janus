// This is brl/bseg/betr/pro/processes/betr_execute_event_trigger_process.cxx
#include <iostream>
#include <fstream>
#include <bprb/bprb_func_process.h>
#include <vcl_string.h>
//:
// \file
// \brief  A process for executing an event_trigger (process change)
//


#include <vcl_compiler.h>
#include <betr/betr_event_trigger.h>
#include <vgl/vgl_point_3d.h>

namespace betr_execute_event_trigger_process_globals
{
  const unsigned n_inputs_  = 2;
  const unsigned n_outputs_ = 1;
}

bool betr_execute_event_trigger_process_cons(bprb_func_process& pro)
{
  using namespace betr_execute_event_trigger_process_globals;

  //process takes 2 inputs
  std::vector<std::string> input_types_(n_inputs_);
  input_types_[0]  = "betr_event_trigger_sptr"; //event_trigger
  input_types_[1]  = "vcl_string"; //algorithm name
  // process has 1 output
  std::vector<std::string> output_types_(n_outputs_);
  output_types_[0] = "float"; // change probability
  return pro.set_input_types(input_types_) && pro.set_output_types(output_types_);
}

bool betr_execute_event_trigger_process(bprb_func_process& pro)
{
  using namespace betr_execute_event_trigger_process_globals;

  if ( pro.n_inputs() < n_inputs_ ) {
    std::cout << pro.name() << ": The input number should be " << n_inputs_<< std::endl;
    return false;
  }
  //get the inputs
  unsigned i = 0;
  betr_event_trigger_sptr event_trigger = pro.get_input<betr_event_trigger_sptr>(i++);
  std::string algorithm_name = pro.get_input<std::string>(i);
  if(!event_trigger)
    return false;
  double prob_change = 0.0;
  bool good = event_trigger->process(algorithm_name, prob_change);
  pro.set_output_val<double>(0, prob_change);
  return good;
}