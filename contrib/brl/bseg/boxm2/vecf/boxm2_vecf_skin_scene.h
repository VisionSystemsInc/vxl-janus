#ifndef boxm2_vecf_skin_scene_h_
#define boxm2_vecf_skin_scene_h_
//:
// \file
// \brief  boxm2_vecf_skin_scene models the surface of the face
//
// \author J.L. Mundy
// \date   3 Dec 2015
//
// For the time being, the skin map to target is only global transformations
//
#include <boxm2/boxm2_block.h>
#include <boxm2/vecf/boxm2_vecf_articulated_scene.h>
#include <boxm2/vecf/boxm2_vecf_articulated_params.h>
#include <boxm2/boxm2_scene.h>
#include <boxm2/boxm2_data.h>
#include <vcl_string.h>
#include <vcl_vector.h>
#include <vcl_iosfwd.h>
#include <vgl/algo/vgl_rotation_3d.h>
#include "boxm2_vecf_skin_params.h"
#include "boxm2_vecf_skin.h"
#include <vgl/vgl_point_3d.h>
#include <vcl_set.h>

class boxm2_vecf_skin_scene : public boxm2_vecf_articulated_scene
{
 public:
  enum anat_type { SKIN, NO_TYPE};
 boxm2_vecf_skin_scene(): source_model_exists_(false), alpha_data_(0), app_data_(0), nobs_data_(0), skin_data_(0),target_alpha_data_(0),target_app_data_(0), target_nobs_data_(0), extrinsic_only_(false),target_blk_(0),target_data_extracted_(false),boxm2_vecf_articulated_scene(),sigma_(0.5f){}

  //: set parameters
  bool set_params(boxm2_vecf_articulated_params const& params);

  //: construct from scene file specification, use exising database unless initialize == true
  // otherwise compute voxel contents from the skin parameters
  boxm2_vecf_skin_scene(vcl_string const& scene_file);

  boxm2_vecf_skin_scene(vcl_string const& scene_file, vcl_string const& geometry_file);

#if 0 //currently only a pre-built skin is used
  boxm2_vecf_skin_scene(vcl_string const& scene_file, vcl_string const& geometry_file, vcl_string const& params_file);
#endif
  //: refine target cells to match the refinement level of the source block
  virtual int prerefine_target_sub_block(vgl_point_3d<int> const& sub_block_index){return -1;}//FIX ME

  //: map skin data to the target scene
  void map_to_target(boxm2_scene_sptr target_scene);

  //: compute an inverse vector field for rotation of skin
  void inverse_vector_field(vcl_vector<vgl_vector_3d<double> >& vfield, vcl_vector<bool>& valid) const;

  //: test the anat_type (SKIN) of the voxel that contains a global point
  bool is_type_global(vgl_point_3d<double> const& global_pt, anat_type type) const;

 //: test the anat_type (SKIN) of a given data index
 bool is_type_data_index(unsigned data_index, anat_type type) const;

 //: tree subblock size in mm
 double subblock_len() const { if(blk_)return (blk_->sub_block_dim()).x(); return 0.0;}
  //: set up pointers to source block databases
 void extract_block_data();
  //: set up pointers to target block databases
 void extract_target_block_data(boxm2_scene_sptr target_scene);
 //: initialize the source block data
 void fill_block();
 //: initialize the full target block (not currently used )
 void fill_target_block();
 //: interpolate the alpha and appearance data around the vector field source location
 void interpolate_vector_field(vgl_point_3d<double> const& src, unsigned sindx, unsigned dindx, unsigned tindx,
                               vcl_vector<vgl_point_3d<double> > & cell_centers,
                               vcl_map<unsigned, vcl_vector<unsigned> >& cell_neighbor_cell_index,
                               vcl_map<unsigned, vcl_vector<unsigned> >&cell_neighbor_data_index);


 void apply_vector_field_to_target(vcl_vector<vgl_vector_3d<double> > const& vf, vcl_vector<bool> const& valid);

 // find nearest cell and return the data index of the nearest cell (found depth is for debug, remove at some point)
 bool find_nearest_data_index(boxm2_vecf_skin_scene::anat_type type, vgl_point_3d<double> const& probe, double cell_len, unsigned& data_indx, int& found_depth) const;

 
  //re-create geometry according to params_
  void rebuild();
  //check for intrinsic parameter change
  bool vfield_params_change_check(const boxm2_vecf_skin_params& params);
  // store the neigbors of each cell for each anatomical component in a vector;
  void cache_neighbors();
  // pre-refine the target scene
  void prerefine_target(boxm2_scene_sptr target_scene);

  void create_anatomy_labels();
  void export_point_cloud(vcl_ostream& ostr) const;

 // ============   skin methods ================
#if 0
 //: construct manidble from parameters
 void create_skin();
 //:read block eye data and reset indices
 void recreate_skin();
#endif
 //: cache index of neighboring cells
 void find_cell_neigborhoods();
 //: set manible anatomy flags
 void cache_cell_centers_from_anatomy_labels();

 //: scan dense set of points on the manbible
 void build_skin();
 //: assign appearance to parts of the skin
 void paint_skin();

#if 0
 void reset_buffers();
#endif
  //: members
 float alpha_init_;
  boxm2_block_sptr blk_;                     // the source block
  boxm2_block_sptr target_blk_;              // the target block
  // cached databases
  // source dbs
  boxm2_data_base* alpha_base_;
  boxm2_data_base* app_base_;
  boxm2_data_base* nobs_base_;
  boxm2_data_base* skin_base_;

  // target dbs
  boxm2_data_base* target_alpha_base_;
  boxm2_data_base* target_app_base_;
  boxm2_data_base* target_nobs_base_;

  boxm2_data<BOXM2_ALPHA>::datatype* alpha_data_;  // source alpha database
  boxm2_data<BOXM2_MOG3_GREY>::datatype* app_data_;// source appearance database
  boxm2_data<BOXM2_NUM_OBS>::datatype* nobs_data_;         // source nobs database
  boxm2_data<BOXM2_ALPHA>::datatype* target_alpha_data_;   //target alpha database
  boxm2_data<BOXM2_MOG3_GREY>::datatype* target_app_data_; //target appearance database
  boxm2_data<BOXM2_NUM_OBS>::datatype* target_nobs_data_;  //target nobs
  boxm2_data<BOXM2_PIXEL>::datatype* skin_data_;        // is voxel a skin point

  vcl_vector<cell_info> box_cell_centers_;       // cell centers in the target block

  boxm2_vecf_skin_params params_;               // parameter struct
  // =============  skin ===============
  
boxm2_vecf_skin skin_geo_;
  vcl_vector<vgl_point_3d<double> > skin_cell_centers_; // centers of skin cells
  vcl_vector<unsigned> skin_cell_data_index_;           // corresponding data indices
  //      cell_index          cell_index
  vcl_map<unsigned, vcl_vector<unsigned> > cell_neighbor_cell_index_; // neighbors of each skin voxel
  //     data_index cell_index
  vcl_map<unsigned, unsigned > data_index_to_cell_index_;             // data index to cell index
  //      data_index          data_index
  vcl_map<unsigned, vcl_vector<unsigned> > cell_neighbor_data_index_; // data index to neighbor data indices

private:
  void extract_scene_metadata();
  bool source_model_exists_;
  bool extrinsic_only_;
  bool target_data_extracted_;
  float sigma_;

 //: assign target cell centers that map to the source scene bounding box
  void determine_target_box_cell_centers();
};

#endif // boxm2_vecf_skin_scene_h_
