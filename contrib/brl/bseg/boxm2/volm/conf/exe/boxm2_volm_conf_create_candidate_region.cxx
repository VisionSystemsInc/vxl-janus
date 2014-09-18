// This is contrib/brl/bseg/boxm2/volm/conf/exe/boxm2_volm_create_candidate_region.cxx
//:
// \file
// \brief executable to create candidate region kml file from configurational matcher output.  Each candidate region is a
//        circular region and contains a heading direction pointing to some representative query annotation
//
// \author Yi Dong
// \date September 12, 2014
// \verbatim
//   Modifications
//    <none yet>
// \endverbatim
//

#include <vul/vul_arg.h>
#include <vul/vul_file.h>
#include <vul/vul_file_iterator.h>
#include <vcl_map.h>
#include <vnl/vnl_math.h>
#include <vpgl/vpgl_lvcs.h>
#include <vpgl/vpgl_lvcs_sptr.h>
#include <bkml/bkml_write.h>
#include <bkml/bkml_parser.h>
#include <volm/volm_tile.h>
#include <volm/volm_io.h>
#include <volm/volm_geo_index.h>
#include <volm/volm_geo_index_sptr.h>
#include <volm/volm_loc_hyp.h>
#include <volm/volm_loc_hyp_sptr.h>
#include <volm/volm_candidate_list.h>
#include <volm/conf/volm_conf_buffer.h>
#include <volm/conf/volm_conf_score.h>


//: check whether the given point is inside the polygon
static bool is_contained(vcl_vector<vgl_polygon<double> > const& poly_vec, vgl_point_2d<double> const& pt);

int main(int argc, char** argv)
{
  vul_arg<vcl_string>      out_kml("-out", "output kml file", "");
  vul_arg<unsigned>       world_id("-world", "ROI world id of the query", 9999);
  vul_arg<vcl_string>   geo_folder("-geo", "geo location database", "");
  vul_arg<vcl_string>    cand_file("-cand", "candidate list used during matching for search space reduction, if existed","");
  vul_arg<vcl_string>   index_name("-idx-name", "name of the loaded index", "");
  vul_arg<vcl_string> score_folder("-score", "folder to read matcher socre","");
  vul_arg<float>   buffer_capacity("-buffer",   "buffer capacity for loading score binary (in GByte)", 2.0f);
  vul_arg<double>           radius("-radius", "radius of pin-pointed circle (in meter)", 200.0);
  vul_arg<unsigned >  num_top_locs("-num-locs", "number of desired pinning points", 100);
  vul_arg_parse(argc, argv);
  vcl_cout << "argc: " << argc << vcl_endl;

  // input check
  if (world_id() == 9999 || geo_folder().compare("") == 0 || score_folder().compare("") == 0 || out_kml().compare("") == 0) {
    vul_arg_display_usage_and_exit();
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  vcl_string log_file;
  vcl_stringstream log;
  vcl_string kml_folder = vul_file::dirname(out_kml());
  log_file = kml_folder + "/log_cand_region.xml";
  // check score folder
  vcl_string score_bins = score_folder() + "/*" + index_name() + "*.bin";
  unsigned ns = 0;
  for (vul_file_iterator fn = score_bins.c_str(); fn; ++fn) {
    ns++;
  }
  if (ns == 0) {
    log << "ERROR: there is no score binaries in the input score folder: " << score_folder() << "!\n";
    volm_io::write_error_log(log_file, log.str());
    return volm_io::EXE_ARGUMENT_ERROR;
  }

  // create volm tiles for given world_id
  vcl_vector<volm_tile> tiles;
  if (!volm_tile::generate_tiles(world_id(), tiles)) {
    log << "ERROR: unknown world id: " << world_id() << "!\n";
    volm_io::write_error_log(log_file, log.str());
    return volm_io::EXE_ARGUMENT_ERROR;
  }
  // load candidate region from previous matcher if exists
  vgl_polygon<double> cand_poly;
  bool is_cand = false;
  cand_poly.clear();
  if (vul_file::exists(cand_file())) {
    cand_poly = bkml_parser::parse_polygon(cand_file());
    vcl_cout << "candidate regions (" << cand_poly.num_sheets() << " sheet)are loaded from file: " << cand_file() << "!!!!!!!!!!" << vcl_endl;
    is_cand = (cand_poly.num_sheets() != 0);
  }

  // loop over each tile to load all scores and sort them
  vcl_cout << "------------------ Start to create candidate region ---------------------" << vcl_endl;
  vcl_cout << tiles.size() << " tiles are created for world: " << world_id() << vcl_endl;
  vcl_multimap<float, vcl_pair<volm_conf_score, vgl_point_2d<double> >, vcl_greater<float> > score_map;
  for (unsigned t_idx = 0; t_idx < tiles.size(); t_idx++)
  {
    volm_tile tile = tiles[t_idx];
    vcl_stringstream file_name_pre;
    file_name_pre << geo_folder() << "geo_index_tile_" << t_idx;
    // no geo location for current tile, skip
    if (!vul_file::exists(file_name_pre.str()+ ".txt"))
      continue;
    // load the location database for current tile
    float min_size;
    volm_geo_index_node_sptr root = volm_geo_index::read_and_construct(file_name_pre.str()+".txt", min_size);
    volm_geo_index::read_hyps(root, file_name_pre.str());
    vcl_vector<volm_geo_index_node_sptr> loc_leaves_all;
    loc_leaves_all.clear();
    volm_geo_index::get_leaves_with_hyps(root, loc_leaves_all);
    // obtain the desired leaf
    vcl_vector<volm_geo_index_node_sptr> loc_leaves;
    for (unsigned i = 0; i < loc_leaves_all.size(); i++)
      if (is_cand && vgl_intersection(loc_leaves_all[i]->extent_, cand_poly))
        loc_leaves.push_back(loc_leaves_all[i]);
      else
        loc_leaves.push_back(loc_leaves_all[i]);
    vcl_cout << "  loading and sorting scores for tile " << t_idx << " from " << loc_leaves.size() << " leaves" << vcl_endl;
    vcl_stringstream score_file_pre;
    score_file_pre << score_folder() << "/conf_score_tile_" << t_idx;
    for (unsigned i = 0; i < loc_leaves.size(); i++)
    {
      volm_geo_index_node_sptr leaf = loc_leaves[i];
      vcl_string score_bin_file = score_file_pre.str() + "_" + leaf->get_string() + "_" + index_name() + ".bin";
      if (!vul_file::exists(score_bin_file))
        continue;
      volm_conf_buffer<volm_conf_score> score_idx(buffer_capacity());
      score_idx.initialize_read(score_bin_file);
      vgl_point_3d<double> h_pt;
      while (leaf->hyps_->get_next(0, 1, h_pt))
      {
        if (is_cand && !volm_candidate_list::inside_candidate_region(cand_poly, h_pt.x(), h_pt.y()))
          continue;
        volm_conf_score score_in;
        score_idx.get_next(score_in);
        vgl_point_2d<double> h_pt_2d(h_pt.x(), h_pt.y());
        vcl_pair<float, vcl_pair<volm_conf_score, vgl_point_2d<double> > > tmp_pair(score_in.score(), vcl_pair<volm_conf_score, vgl_point_2d<double> >(score_in, h_pt_2d));
        score_map.insert(tmp_pair);
      }
    }
  }  // end of loop over tiles

  vcl_cout << "Start to generate " << num_top_locs() << " top regions from " << score_map.size() << " matched locations..." << vcl_flush << vcl_endl;
  // containers to store the circle region and heading direction
  vcl_vector<vgl_polygon<double> > pin_pt_poly;
  vcl_vector<float> pin_pt_heading;
  vcl_vector<vgl_point_2d<double> > pin_pt_center;
  vcl_vector<float> likelihood;
  vcl_multimap<float, vcl_pair<volm_conf_score, vgl_point_2d<double> > >::iterator mit = score_map.begin();
  while (pin_pt_center.size() < num_top_locs() && mit != score_map.end())
  {
    // check whether the location has been in the pin-pointed region
    if (is_contained(pin_pt_poly, mit->second.second)) {
      ++mit;
      continue;
    }
    // generate a pin-point region for current location
    likelihood.push_back(mit->first);
    pin_pt_center.push_back(mit->second.second);
    pin_pt_heading.push_back(mit->second.first.theta());
    vcl_vector<vgl_point_2d<double> > circle;
    if (!volm_candidate_list::generate_pin_point_circle(mit->second.second, radius(), circle))
    {
      log << "ERROR: generating pin point circle for location " << mit->second.second << " failed!\n";
      volm_io::write_error_log(log_file, log.str());
      return volm_io::EXE_ARGUMENT_ERROR;
    }
    pin_pt_poly.push_back(vgl_polygon<double>(circle));
    ++mit;
  }

  vcl_cout << pin_pt_center.size() << " pin points are created out of " << score_map.size() <<  " matched locations" << vcl_endl;

  // write the kml file
  vcl_ofstream ofs_kml(out_kml().c_str());
  vcl_string kml_name = vul_file::strip_extension(vul_file::strip_directory(out_kml()));
  volm_candidate_list::open_kml_document(ofs_kml, kml_name, (float)num_top_locs());
  unsigned rank = 0;
  for (unsigned i = 0; i < pin_pt_center.size(); i++)
  {
    // create a line to represent heading direction
    vcl_vector<vgl_point_2d<double> > heading_line;
    if (!volm_candidate_list::generate_heading_direction(pin_pt_center[i], pin_pt_heading[i], radius()*2, heading_line)) {
      log << "ERROR: generate heading directional line for rank " << rank << " pin point failed!\n";
      volm_io::write_error_log(log_file, log.str());
      return volm_io::EXE_ARGUMENT_ERROR;
    }
    // put current candidate region into kml
    volm_candidate_list::write_kml_regions(ofs_kml, pin_pt_poly[i][0], pin_pt_center[i], heading_line, likelihood[i], rank++);
  }
  volm_candidate_list::close_kml_document(ofs_kml);
  ofs_kml.close();

  vcl_cout << "FINISH!  candidate region is stored at: " << out_kml() << vcl_endl;
  return volm_io::SUCCESS;
}

bool is_contained(vcl_vector<vgl_polygon<double> > const& poly_vec, vgl_point_2d<double> const& pt)
{
  unsigned n_poly = poly_vec.size();
  for (unsigned i = 0; i < n_poly; i++)
  {
    for (unsigned k = 0; k < poly_vec[i].num_sheets(); k++)
    {
      vgl_polygon<double> single_sheet(poly_vec[i][k]);
      if (single_sheet.contains(pt))
        return true;
    }
  }
  return false;
}