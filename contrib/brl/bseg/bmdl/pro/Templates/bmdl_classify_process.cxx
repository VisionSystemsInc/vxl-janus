#include <bprb/bprb_func_process.h>
//:
// \file

#include <vil/vil_image_view.h>
#include <vil/vil_pixel_format.h>
#include <bprb/bprb_parameters.h>
#include <bmdl/bmdl_classify.h>

#define PARAM_GROUND_THRESH "ground_threshold"
#define PARAM_VEG_THRESH    "vegetation_threshold"
#define PARAM_AREA_THRESH   "area_threshold"
#define PARAM_HEIGHT_RES    "height_resolution"

template vil_image_view_base_sptr bprb_func_process::get_input(unsigned);

template <class T>
bool classify(const vil_image_view<T>& lidar_first,
                                     const vil_image_view<T>& lidar_last,
                                     const vil_image_view<T>& ground,
                                     vil_image_view<unsigned int>& label_img,
                                     vil_image_view<T>& height_img,
                                     T gthresh, T vthresh, T athresh, T hres)
{
  bmdl_classify<T> classifier(athresh, hres, gthresh, vthresh);
  classifier.set_lidar_data(lidar_first,lidar_last);
  classifier.set_bare_earth(ground);
  classifier.label_lidar();
  label_img = classifier.labels();
  height_img = classifier.heights();
  return true;
}


bool classify(vil_image_view_base_sptr lidar_first,
                                     vil_image_view_base_sptr lidar_last,
                                     vil_image_view_base_sptr ground,
                                     vil_image_view_base_sptr& label_img,
                                     vil_image_view_base_sptr& height_img,
                                     float gthresh, float vthresh, float athresh, float hres)
{
  label_img = new vil_image_view<unsigned int>();

  // use the float version
  if (lidar_first->pixel_format() == VIL_PIXEL_FORMAT_FLOAT)
  {
    if (lidar_last->pixel_format() == VIL_PIXEL_FORMAT_FLOAT)
    {
      if (ground->pixel_format() == VIL_PIXEL_FORMAT_FLOAT) {
        height_img = new vil_image_view<float>();
        return classify<float>(lidar_first, lidar_last, ground,
                               (vil_image_view<unsigned int>&)(*label_img),
                               (vil_image_view<float>&)(*height_img), gthresh, vthresh, athresh, hres);
      }
    }
    else
    {
      vcl_cout << "input images have different bit depths" << vcl_endl;
      return false;
    }
  }

  // use the double version
  else if (lidar_first->pixel_format() == VIL_PIXEL_FORMAT_DOUBLE)
  {
    if (lidar_last->pixel_format() == VIL_PIXEL_FORMAT_DOUBLE)
    {
      if (ground->pixel_format() == VIL_PIXEL_FORMAT_DOUBLE) {
        height_img = new vil_image_view<double>();
        return classify<double>(lidar_first, lidar_last, ground,
                                (vil_image_view<unsigned int>&)(*label_img),
                                (vil_image_view<double>&)(*height_img),
                                (double) gthresh, (double) vthresh, (double) athresh, (double) hres);
      }
    }
    else
    {
      vcl_cout << "input images have different bit depths" << vcl_endl;
      return false;
    }
  }

  return false;
}

bool bmdl_classify_process(bprb_func_process& pro)
{
  if (pro.n_inputs()< 3) {
    vcl_cout << "lidar_roi_process: The input number should be 3" << vcl_endl;
    return false; 
  }

  // get the inputs:
  unsigned int i=0;
  vil_image_view_base_sptr first_ret = pro.get_input<vil_image_view_base_sptr>(i++);
  vil_image_view_base_sptr last_ret = pro.get_input<vil_image_view_base_sptr>(i++);
  vil_image_view_base_sptr ground = pro.get_input<vil_image_view_base_sptr>(i++);
  
  //pro.set_input_types
  // check first return's validity
  if (!first_ret) {
    vcl_cout << "bmdl_classify_process -- First return image is not valid!\n";
    return false;
  }

  // check last return's validity
  if (!last_ret) {
    vcl_cout << "bmdl_classify_process -- Last return image is not valid!\n";
    return false;
  }

  // check last return's validity
  if (!ground) {
    vcl_cout << "bmdl_classify_process -- Ground image is not valid!\n";
    return false;
  }

  // read the parameters
  float gthresh=0.0f, vthresh = 0.0f, hres=0.0f; // dummy initialization, to avoid compiler warning
  float athresh=0.0f;

  if (!pro.parameters()->get_value(PARAM_GROUND_THRESH, gthresh) ||
      !pro.parameters()->get_value(PARAM_VEG_THRESH, vthresh) ||
      !pro.parameters()->get_value(PARAM_AREA_THRESH, athresh) ||
      !pro.parameters()->get_value(PARAM_HEIGHT_RES, hres)) {
    vcl_cout << "bmdl_classify_process -- problem getting the parameters!\n";
    return false;
  }

  vil_image_view_base_sptr label_img=0, height_img=0;
  if (!classify(first_ret, last_ret, ground, label_img, height_img, gthresh, vthresh, athresh, hres)) {
    vcl_cout << "bmdl_classify_process -- The process has failed!\n";
    return false;
  }

  vcl_vector<vcl_string> output_types_(4);
  int j=0;
  output_types_[j++]= "vil_image_view_base_sptr";  // label image
  output_types_[j++]= "vil_image_view_base_sptr";  // height image

  j=0;
  // store image output (labels)
  brdb_value_sptr output0 = new brdb_value_t<vil_image_view_base_sptr>(label_img);
  pro.set_output(j++, output0);

  // store image output (height)
  brdb_value_sptr output1 = new brdb_value_t<vil_image_view_base_sptr>(height_img);
  pro.set_output(j++, output1);

  return true;
}

