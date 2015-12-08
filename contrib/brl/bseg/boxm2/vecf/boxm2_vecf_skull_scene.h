#ifndef boxm2_vecf_skull_scene_h_
#define boxm2_vecf_skull_scene_h_

#include <vcl_string.h>

#include <boxm2/boxm2_scene.h>
#include "boxm2_vecf_mandible_scene.h"
#include "boxm2_vecf_cranium_scene.h"
#include "boxm2_vecf_head_model.h"
#include "boxm2_vecf_articulated_params.h"
#include "boxm2_vecf_skull_params.h"
#include "boxm2_vecf_articulated_scene.h"

class boxm2_vecf_skull_scene : public boxm2_vecf_articulated_scene{

public:
  boxm2_vecf_skull_scene(vcl_string const& scene_path, vcl_string const& geo_path);
  ~boxm2_vecf_skull_scene(){delete mandible_scene_; delete cranium_scene_;}
  void map_to_target(boxm2_scene_sptr target);

  bool set_params(boxm2_vecf_articulated_params const& params);
  boxm2_vecf_skull_params const& get_params() const {return params_;}
  //: refine target cells to match the refinement level of the source block
  virtual int prerefine_target_sub_block(vgl_point_3d<int> const& sub_block_index){return -1;}//FIXME
  //: compute inverse vector field for unrefined sub_block centers
  virtual void inverse_vector_field_unrefined(boxm2_scene_sptr target_scene){}//FIXME
private:
  bool target_data_extracted_;
  boxm2_vecf_skull_params params_;
  boxm2_vecf_mandible_scene* mandible_scene_;
  boxm2_vecf_cranium_scene* cranium_scene_;
  vcl_string scene_path;
};

#endif
