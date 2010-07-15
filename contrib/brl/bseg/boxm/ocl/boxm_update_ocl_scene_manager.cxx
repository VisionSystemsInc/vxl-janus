
//:
// \file
#include "boxm_update_ocl_scene_manager.h"
#include <vcl_where_root_dir.h>
#include <boxm/ocl/boxm_ocl_utils.h>
#include <vcl_cstdio.h>
#include <vul/vul_timer.h>
#include <boxm/boxm_block.h>
#include <boxm/boxm_scene.h>
#include <boxm/util/boxm_utils.h>
#include <boxm/basic/boxm_block_vis_graph_iterator.h>
#include <vil/vil_save.h>
//: Initializes CPU side input buffers
//put tree structure and data into arrays

bool boxm_update_ocl_scene_manager::init_update(boxm_ocl_scene *scene,
                                                  vpgl_camera_double_sptr cam,
                                                  vil_image_view<float> &obs)
{
  scene_ = scene;
  cam_ = cam;
  input_img_=obs;
  return true;
}


//: update the tree
bool boxm_update_ocl_scene_manager::
build_program(vcl_string const& functor, bool use_cell_data)
{
  vcl_string root = vcl_string(VCL_SOURCE_ROOT_DIR);
  bool octr = this->load_kernel_source(root + "/contrib/brl/bseg/boxm/ocl/octree_library_functions.cl");
  bool bpr  = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/backproject.cl");
  bool stat = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/statistics_library_functions.cl");
  bool rbun = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/ray_bundle_library_functions.cl");
  bool main = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/update_ocl_scene.cl");

  if (!octr||!bpr||!stat||!rbun||!main) {
    vcl_cerr << "Error: boxm_ray_trace_manager : failed to load kernel source (helper functions)\n";
    return false;
  }
  vcl_string patt = "$$step_cell$$", empty = "", zero = "0", one = "1";
  // transfer cell data from global to local memory if use_cell_data_ == true
  vcl_string use = "%%";
  vcl_string::size_type use_start = this->prog_.find(use);
  if (use_start < this->prog_.size()) {
    if (use_cell_data)
      this->prog_ = this->prog_.replace (use_start, 2, one.c_str(), 1);
    else
      this->prog_ = this->prog_.replace (use_start, 2, zero.c_str(), 1);
  }
  else
    return false;
  // assign the functor calling signature
  vcl_string::size_type pos_start = this->prog_.find(patt);
  vcl_string::size_type n1 = patt.size();
  if (pos_start < this->prog_.size()) {
    vcl_string::size_type n2 = functor.size();
    if (!n2)
      return false;
    this->prog_ = this->prog_.replace(pos_start, n1, functor.c_str(), n2);
    return this->build_kernel_program(program_)==SDK_SUCCESS;
  }
  return false;
}
bool 
boxm_update_ocl_scene_manager::build_rendering_program()
{
    vcl_string root = vcl_string(VCL_SOURCE_ROOT_DIR);
    bool octr = this->load_kernel_source(root + "/contrib/brl/bseg/boxm/ocl/octree_library_functions.cl");
    bool bpr  = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/backproject.cl");
    bool exp  = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/expected_functor.cl");
    bool rbun = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/ray_bundle_library_functions.cl");
    bool main = this->append_process_kernels(root + "/contrib/brl/bseg/boxm/ocl/ray_trace_ocl_scene.cl");

    if (!octr||!bpr||!exp||!rbun||!main) {
        vcl_cerr << "Error: boxm_ray_trace_manager : failed to load kernel source (helper functions)\n";
        return false;
    }
    return this->build_kernel_program(program_)==SDK_SUCCESS;
}
bool boxm_update_ocl_scene_manager::set_kernels()
{
  cl_int status = CL_SUCCESS;
  int CHECK_SUCCESS = 1;
  if (!this->release_kernels())
    return false;
  // pass 0
  if (!this->build_program("seg_len_obs(d,image_vect,ray_bundle_array,cached_aux_data)", false))
    return false;
  cl_kernel kernel = clCreateKernel(program_,"update_ocl_scene",&status);
  if (this->check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);
  // pass 1
  if (!this->build_program("pre_infinity(d,image_vect,ray_bundle_array, cached_data, cached_aux_data)", true))
    return false;
  kernel = clCreateKernel(program_,"update_ocl_scene",&status);
  if (this->check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)    return false;
  kernels_.push_back(kernel);
  //pass 2 norm image (pre-requisite to C++ pass 2)
  kernel = clCreateKernel(program_,"proc_norm_image",&status);
  if (this->check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);
  // pass 3 Bayes ratio (pass 2 in C++)
  if (!this->build_program("bayes_ratio(d,image_vect,ray_bundle_array, cached_data, cached_aux_data)", true))
    return false;
  kernel = clCreateKernel(program_,"update_ocl_scene",&status);
  if (this->check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);

  kernel = clCreateKernel(program_,"update_ocl_scene_main",&status);
  if (this->check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
      return false;
  kernels_.push_back(kernel);

  if (!this->build_rendering_program())
    return false;
  kernel = clCreateKernel(program_,"ray_trace_ocl_scene_with_full_data",&status);
  if (this->check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);

  return true;
}


bool boxm_update_ocl_scene_manager::release_kernels()
{
  cl_int status = CL_SUCCESS;
  int CHECK_SUCCESS = 1;
  for (unsigned i = 0; i<kernels_.size(); ++i) {
    if (kernels_[i]) {
      status = clReleaseKernel(kernels_[i]);
      if (this->check_val(status,CL_SUCCESS,"clReleaseKernel failed.")!=CHECK_SUCCESS)
        return false;
    }
  }
  kernels_.clear();
  return true;
}
bool boxm_update_ocl_scene_manager::setup_norm_data(bool use_uniform,float mean,float sigma)
{
  return this->setup_app_density(use_uniform, mean, sigma)
      && this->setup_app_density_buffer()==SDK_SUCCESS;
}
bool boxm_update_ocl_scene_manager::clean_norm_data()
{
  return this->clean_app_density()
      && this->clean_app_density_buffer()==SDK_SUCCESS;
}

bool boxm_update_ocl_scene_manager::setup_app_density(bool use_uniform, float mean, float sigma)
{
#if defined (_WIN32)
  app_density_ =  (cl_float*)_aligned_malloc( sizeof(cl_float4), 16);
#elif defined(__APPLE__)
  app_density_ =  (cl_float*)malloc( sizeof(cl_float4));
#else
  app_density_ =  (cl_float*)memalign(16, sizeof(cl_float4));
#endif
  if (use_uniform) {
    app_density_[0]=1.0f;
    app_density_[1]=0.0f;
    app_density_[2]=0.0f;
    app_density_[3]=0.0f;
  }
  else {
    app_density_[0]=0.0f;
    app_density_[1]=mean;
    app_density_[2]=sigma;
    app_density_[3]=0.0f;
  }
  return true;
}


bool boxm_update_ocl_scene_manager::clean_app_density()
{
  if (app_density_) {
#ifdef _WIN32
    _aligned_free(app_density_);
#else
    free(app_density_);
#endif
    app_density_ = NULL;
    return true;
  }
  else
    return false;
}


int boxm_update_ocl_scene_manager::setup_app_density_buffer()
{
  cl_int status = CL_SUCCESS;
  app_density_buf_ = clCreateBuffer(this->context_,CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                    sizeof(cl_float4),app_density_,&status);
  if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (app density) failed."))
    return SDK_FAILURE;
  else
    return SDK_SUCCESS;
}


int boxm_update_ocl_scene_manager::clean_app_density_buffer()
{
  cl_int status = clReleaseMemObject(app_density_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject (app_density_buf_) failed."))
    return SDK_FAILURE;
  else
    return SDK_SUCCESS;
}

bool boxm_update_ocl_scene_manager::set_args(unsigned pass)
{
  int CHECK_SUCCESS = 1;

  if (kernels_.size()<=0)
    return false;
  cl_int status = CL_SUCCESS;
    if (pass==4)
  {
    status = clSetKernelArg(kernels_[pass], 0,
                            sizeof(cl_mem), (void *)&cell_data_buf_);
    if (this->check_val(status, CL_SUCCESS, "clSetKernelArg failed. (cell_data_buf_)")!=CHECK_SUCCESS)
      return false;

    status = clSetKernelArg(kernels_[pass], 1,
                            sizeof(cl_mem), (void *)&cell_aux_data_buf_);
    if (this->check_val(status, CL_SUCCESS, "clSetKernelArg failed. (cell_aux_data_buf_)")!=CHECK_SUCCESS)
      return false;
    status = clSetKernelArg(kernels_[pass], 2,
                            sizeof(cl_mem), (void *)&lenbuffer_buf_);
    if (this->check_val(status, CL_SUCCESS, "clSetKernelArg failed. (lenbuffer_buf_)")!=CHECK_SUCCESS)
        return false;

    status = clSetKernelArg(kernels_[pass], 3,
                            sizeof(cl_mem), (void *)&numbuffer_buf_);
    return this->check_val(status, CL_SUCCESS,
                           "clSetKernelArg failed. (numbuffer_buf_)")==CHECK_SUCCESS;
  }

  if (pass == 2) { // norm image process //
    status = clSetKernelArg(kernels_[pass], 0,
                            sizeof(cl_mem), (void *)&image_buf_);
    if (this->check_val(status, CL_SUCCESS, "clSetKernelArg failed. (image array)")!=CHECK_SUCCESS)
      return false;

    status = clSetKernelArg(kernels_[pass], 1,
                            sizeof(cl_mem), (void *)&app_density_buf_);
    if (this->check_val(status, CL_SUCCESS, "clSetKernelArg failed. (remote surface appearance)")!=CHECK_SUCCESS)
      return false;
    status = clSetKernelArg(kernels_[pass], 2,
                            sizeof(cl_mem), (void *)&img_dims_buf_);
    return this->check_val(status, CL_SUCCESS,
                           "clSetKernelArg failed. (image dimensions)")==CHECK_SUCCESS;
  }
  if(pass==0 || pass ==1 || pass==3)
  {
  int i=0;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&scene_dims_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (scene_dims_buf_)"))
    return SDK_FAILURE;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&scene_origin_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (scene_orign_buf_)"))
      return SDK_FAILURE;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&block_dims_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (block_dims_buf_)"))
    return SDK_FAILURE;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&block_ptrs_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (block_ptrs_buf_)"))
    return SDK_FAILURE;
  // root level buffer
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&root_level_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (root_level_buf_)"))
    return SDK_FAILURE;
  // the lenght of buffer
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&numbuffer_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (lenbuffer_buf_)"))
    return SDK_FAILURE;

  // the lenght of buffer
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&lenbuffer_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (lenbuffer_buf_)"))
    return SDK_FAILURE;
  // the tree buffer
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&cells_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (cells_buf_)"))
    return SDK_FAILURE;
  // data buffer
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&cell_data_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (cell_data_buf_)"))
    return SDK_FAILURE;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&cell_aux_data_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (cell_data_buf_)"))
      return SDK_FAILURE;

  // camera buffer
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&persp_cam_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (data)"))
    return SDK_FAILURE;
  // roi dimensions
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&img_dims_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (Img dimensions)"))
    return SDK_FAILURE;
  //// input image buffer

  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&image_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (input_image)"))
    return SDK_FAILURE;
  
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&factor_buf_);
  if (this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (factor_buf_)")!=CHECK_SUCCESS)
    return false;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&offset_x_buf_);
  if (this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (offset_x_buf_)")!=CHECK_SUCCESS)
    return false;

  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&offset_y_buf_);
  if (this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (offset_y_buf_)")!=CHECK_SUCCESS)
    return false;
  status = clSetKernelArg(kernels_[pass],i++,3*sizeof(cl_float16),0);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (local cam)"))
    return SDK_FAILURE;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_uint4),0);
  if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (local image dimensions)"))
    return SDK_FAILURE;

  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_uchar4)*this->bni_*this->bnj_,0);

  if (this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (local cache ptr bundle)")!=CHECK_SUCCESS)
    return false;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_int)*this->bni_*this->bnj_,0);
  if (this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (loc code bundle)")!=CHECK_SUCCESS)
    return false;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_float16)*this->bni_*this->bnj_,0);
  if (this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (loc code bundle)")!=CHECK_SUCCESS)
    return false;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_float4)*this->bni_*this->bnj_,0);

  if (this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (loc code bundle)")!=CHECK_SUCCESS)
    return false;
  status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_float4)*this->bni_*this->bnj_,0);
  return this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (loc code bundle)")==CHECK_SUCCESS;
  }
  if(pass==5)
  {
      int i=0;
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&scene_dims_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (scene_dims_buf_)"))
          return SDK_FAILURE;
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&scene_origin_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (scene_orign_buf_)"))
          return SDK_FAILURE;
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&block_dims_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (block_dims_buf_)"))
          return SDK_FAILURE;
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&block_ptrs_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (block_ptrs_buf_)"))
          return SDK_FAILURE;
      // root level buffer
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&root_level_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (root_level_buf_)"))
          return SDK_FAILURE;
      // the lenght of buffer
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&numbuffer_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (lenbuffer_buf_)"))
          return SDK_FAILURE;

      // the lenght of buffer
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&lenbuffer_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (lenbuffer_buf_)"))
          return SDK_FAILURE;
      // the tree buffer
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&cells_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (cells_buf_)"))
          return SDK_FAILURE;
      // data buffer
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&cell_data_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (cell_data_buf_)"))
          return SDK_FAILURE;

      // camera buffer
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&persp_cam_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (data)"))
          return SDK_FAILURE;
      // roi dimensions
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&img_dims_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (Img dimensions)"))
          return SDK_FAILURE;
      //// input image buffer

      status = clSetKernelArg(kernels_[pass],i++,3*sizeof(cl_float16),0);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (local cam)"))
          return SDK_FAILURE;
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_uint4),0);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (local image dimensions)"))
          return SDK_FAILURE;
      status = clSetKernelArg(kernels_[pass],i++,sizeof(cl_mem),(void *)&image_gl_buf_);
      if (!this->check_val(status,CL_SUCCESS,"clSetKernelArg failed. (input_image)"))
          return SDK_FAILURE;
  }

  return SDK_SUCCESS;
}


bool boxm_update_ocl_scene_manager::set_commandqueue()
{
  cl_int status = CL_SUCCESS;
  command_queue_ = clCreateCommandQueue(this->context(),this->devices()[0],CL_QUEUE_PROFILING_ENABLE,&status);
  if (!this->check_val(status,CL_SUCCESS,"Falied in command queue creation" + error_to_string(status)))
    return false;

  return true;
}


bool boxm_update_ocl_scene_manager::release_commandqueue()
{
  if (command_queue_)
  {
    cl_int status = clReleaseCommandQueue(command_queue_);
    if (!this->check_val(status,CL_SUCCESS,"clReleaseCommandQueue failed."))
      return false;
  }
  return true;
}


bool boxm_update_ocl_scene_manager::set_workspace(unsigned pass)
{
  cl_int status = CL_SUCCESS;

  // check the local memeory
  cl_ulong used_local_memory;
  status = clGetKernelWorkGroupInfo(this->kernels_[pass],this->devices()[0],
                                    CL_KERNEL_LOCAL_MEM_SIZE,
                                    sizeof(cl_ulong),&used_local_memory,NULL);
  if (!this->check_val(status,CL_SUCCESS,"clGetKernelWorkGroupInfo CL_KERNEL_LOCAL_MEM_SIZE failed."))
    return 0;

  // determine the work group size
  cl_ulong kernel_work_group_size;
  status = clGetKernelWorkGroupInfo(this->kernels_[pass],this->devices()[0],CL_KERNEL_WORK_GROUP_SIZE,
                                    sizeof(cl_ulong),&kernel_work_group_size,NULL);
  if (!this->check_val(status,CL_SUCCESS,"clGetKernelWorkGroupInfo CL_KERNEL_WORK_GROUP_SIZE, failed."))
    return 0;

  if(pass==0 || pass==1 || pass==3) //: passes for computing aux
  {
      globalThreads[0]=this->wni_/2; globalThreads[1]=this->wnj_/2;
      localThreads[0] =this->bni_  ; localThreads[1] =this->bnj_  ;

  }
  if(pass==2)//: pass for normalizing image
  {
      globalThreads[0]=this->wni_;globalThreads[1]=this->wnj_;
      localThreads[0] =this->bni_;localThreads[0] =this->bnj_;
  }
  if(pass==4)  //: pass for updating data from aux data
  {
    globalThreads[0]=RoundUp(cell_data_size_,64);globalThreads[1]=1;
    localThreads[0]=64;                              localThreads[1]=1;
  }
  if(pass==5)
  {
      globalThreads[0]=this->wni_;globalThreads[1]=this->wnj_;
      localThreads[0] =this->bni_;localThreads[0] =this->bnj_;
  }
  if (used_local_memory > this->total_local_memory())
  {
    vcl_cout << "Unsupported: Insufficient local memory on device.\n";
    return 0;
  }
  else
    return 1;
}


bool boxm_update_ocl_scene_manager::run(unsigned pass)
{
  int CHECK_SUCCESS = 1;
  cl_int status = SDK_SUCCESS;
  cl_ulong tstart,tend;

  if(pass==0 || pass ==1 || pass==3)
  {
  for (unsigned k=0;k<2;k++)
      {
          for (unsigned l=0;l<2;l++)
          {
              status=clEnqueueWriteBuffer(command_queue_,offset_y_buf_,0,0,sizeof(cl_uint),(void *)&k,0,0,0);
              status=clEnqueueWriteBuffer(command_queue_,offset_x_buf_,0,0,sizeof(cl_uint),(void *)&l,0,0,0);
              clFinish(command_queue_);
              cl_event ceEvent;

              status = clEnqueueNDRangeKernel(command_queue_, kernels_[pass], 2,NULL,globalThreads,localThreads,0,NULL,&ceEvent);
              if (this->check_val(status,CL_SUCCESS,"clEnqueueNDRangeKernel failed. "+error_to_string(status))!=CHECK_SUCCESS)
                  return false;
              status = clFinish(command_queue_);
              if (this->check_val(status,CL_SUCCESS,"clFinish failed."+error_to_string(status))!=CHECK_SUCCESS)
                  return false;
              status = clGetEventProfilingInfo(ceEvent,CL_PROFILING_COMMAND_END,sizeof(cl_ulong),&tend,0);
              status = clGetEventProfilingInfo(ceEvent,CL_PROFILING_COMMAND_START,sizeof(cl_ulong),&tstart,0);
              gpu_time_+= (double)1.0e-6 * (tend - tstart); // convert nanoseconds to milliseconds

          }
      }
  }
  if(pass==2 || pass ==4)
  {
      cl_event ceEvent;

      status = clEnqueueNDRangeKernel(command_queue_, kernels_[pass], 2,NULL,globalThreads,localThreads,0,NULL,&ceEvent);
      if (this->check_val(status,CL_SUCCESS,"clEnqueueNDRangeKernel failed. "+error_to_string(status))!=CHECK_SUCCESS)
          return false;
      status = clFinish(command_queue_);
      status = clGetEventProfilingInfo(ceEvent,CL_PROFILING_COMMAND_END,sizeof(cl_ulong),&tend,0);
      status = clGetEventProfilingInfo(ceEvent,CL_PROFILING_COMMAND_START,sizeof(cl_ulong),&tstart,0);
      gpu_time_+= (double)1.0e-6 * (tend - tstart); // convert nanoseconds to milliseconds

  }

  if (this->check_val(status,CL_SUCCESS,"clFinish failed."+error_to_string(status))!=CHECK_SUCCESS)
    return false;


  return true;
}


bool boxm_update_ocl_scene_manager::run_scene()
{
 // cl_int status = CL_SUCCESS;
 // bool good=true;
  //vcl_string error_message="";
  //vul_timer timer;
  //good=good && set_scene_data()
  //          && set_all_blocks()
  //          && set_scene_data_buffers()
  //          && set_offset_buffers(0,0,2)
  //          && set_tree_buffers();
  //// run the raytracing
  //good=good && set_input_view()
  //          && set_input_view_buffers();
  //this->set_kernels();
  //this->set_commandqueue();

  //for (unsigned pass = 0; pass<5; pass++)
  //{
  //    this->set_args(pass);
  //    this->set_workspace(pass);
  //    if (!this->run(pass))
  //        return false;
  //    this->read_output_image();
  //    this->save_image();
  //}
  //this->read_trees();
  //this->archive_tree_data();

  //good =good && release_tree_buffers();
  //good =good && clean_tree();
  //good=good && read_output_image();
  //good=good && release_input_view_buffers();
  ////good=good && clean_input_view();
  //good=good && release_scene_data_buffers();
  //good=good && clean_scene_data();

  //// release the command Queue

  //this->release_kernels();
  //vcl_cout << "Timing Analysis\n"
  //         << "===============\n"
  //        <<"openCL Running time "<<gpu_time_<<" ms\n"
  ////       << "Running block "<<total_gpu_time/1000<<"s\n"
  ////       << "total block loading time = " << total_load_time << "s\n"
  ////       << "total block processing time = " << total_raytrace_time << 's' << vcl_endl
  //;
  //return true;

    return setup_online_processing()&&
           online_processing()      && 
           finish_online_processing();
}
bool boxm_update_ocl_scene_manager::setup_online_processing()
{
  cl_int status = CL_SUCCESS;
  bool good=true;
  vcl_string error_message="";
  vul_timer timer;
  good=good && set_scene_data()
            && set_all_blocks()
            && set_scene_data_buffers()
            && set_offset_buffers(0,0,2)
            && set_tree_buffers();
  // run the raytracing
  good=good && set_input_view()
            && set_input_view_buffers();
  good=good && this->set_kernels()
            &&   this->set_commandqueue();

  return good;


}
bool boxm_update_ocl_scene_manager::online_processing()
{
  gpu_time_=0;
  for (unsigned pass = 0; pass<5; pass++)
  {
      this->set_args(pass);
      this->set_workspace(pass);
      if (!this->run(pass))
          return false;
      if(pass==2)
      {
          this->read_output_image();
          this->save_image();
      }
  }
  vcl_cout << "Timing Analysis\n"
      << "===============\n"
      <<"openCL Running time "<<gpu_time_<<" ms\n"
      //       << "Running block "<<total_gpu_time/1000<<"s\n"
      //       << "total block loading time = " << total_load_time << "s\n"
      //       << "total block processing time = " << total_raytrace_time << 's' << vcl_endl
      ;

  return true;
}
bool boxm_update_ocl_scene_manager::rendering()
{
  gpu_time_=0;
  unsigned pass = 5;
  this->set_args(pass);
  this->set_workspace(pass);
  if (!this->run(pass))
      return false;
  vcl_cout << "Timing Analysis\n"
      << "===============\n"
      <<"openCL Running time "<<gpu_time_<<" ms\n"
      //       << "Running block "<<total_gpu_time/1000<<"s\n"
      //       << "total block loading time = " << total_load_time << "s\n"
      //       << "total block processing time = " << total_raytrace_time << 's' << vcl_endl
      ;

  return true;
}
bool boxm_update_ocl_scene_manager::finish_online_processing()
{
  cl_int status = CL_SUCCESS;
  bool good=true;
  this->release_kernels();

  this->read_trees();
  this->archive_tree_data();

  good =good && release_tree_buffers();
  good =good && clean_tree();
  good=good && read_output_image();
  good=good && release_input_view_buffers();
  //good=good && clean_input_view();
  good=good && release_scene_data_buffers();
  good=good && clean_scene_data();

  // release the command Queue

  return good;

}
void boxm_update_ocl_scene_manager::save_image()
{
  if (!image_)
    return;
  vil_image_view<float> img0(this->wni_,this->wnj_);
  vil_image_view<float> img1(this->wni_,this->wnj_);
  vil_image_view<float> img2(this->wni_,this->wnj_);
  vil_image_view<float> img3(this->wni_,this->wnj_);


  for (unsigned j=0;j<this->wnj_;j++)
    for (unsigned i=0;i<this->wni_;i++)
      img0(i,j)=image_[(j*this->wni_+i)*4];
  for (unsigned j=0;j<this->wnj_;j++)
    for (unsigned i=0;i<this->wni_;i++)
      img1(i,j)=image_[(j*this->wni_+i)*4+1];
  for (unsigned j=0;j<this->wnj_;j++)
    for (unsigned i=0;i<this->wni_;i++)
      img2(i,j)=image_[(j*this->wni_+i)*4+2];
  for (unsigned j=0;j<this->wnj_;j++)
    for (unsigned i=0;i<this->wni_;i++)
      img3(i,j)=image_[(j*this->wni_+i)*4+3];


  vil_save(img0,"f:/APL/img0.tiff");
  vil_save(img1,"f:/APL/img1.tiff");
  vil_save(img2,"f:/APL/img2.tiff");
  vil_save(img3,"f:/APL/img3.tiff");
}

bool boxm_update_ocl_scene_manager:: read_output_image()
{
  cl_event events[2];

  // Enqueue readBuffers
  int status = clEnqueueReadBuffer(command_queue_,image_buf_,CL_TRUE,
                                   0,this->wni_*this->wnj_*sizeof(cl_float4),
                                   image_,
                                   0,NULL,&events[0]);

  if (!this->check_val(status,CL_SUCCESS,"clEnqueueBuffer (image_)failed."))
    return false;

  // Wait for the read buffer to finish execution
  status = clWaitForEvents(1, &events[0]);
  if (!this->check_val(status,CL_SUCCESS,"clWaitForEvents failed."))
    return false;

  status = clReleaseEvent(events[0]);
  return this->check_val(status,CL_SUCCESS,"clReleaseEvent failed.")==1;
}


bool boxm_update_ocl_scene_manager:: read_trees()
{
  cl_event events[2];

  // Enqueue readBuffers
  int status = clEnqueueReadBuffer(command_queue_,cells_buf_,CL_TRUE,
                                   0,cells_size_*sizeof(cl_int4),
                                   cells_,
                                   0,NULL,&events[0]);

  if (!this->check_val(status,CL_SUCCESS,"clEnqueueBuffer (cells )failed."))
    return false;
  status = clWaitForEvents(1, &events[0]);
  if (!this->check_val(status,CL_SUCCESS,"clWaitForEvents failed."))
    return false;

  status = clEnqueueReadBuffer(command_queue_,cell_data_buf_,CL_TRUE,
                               0,cell_data_size_*sizeof(cl_float16),
                               cell_data_,
                               0,NULL,&events[0]);

  if (!this->check_val(status,CL_SUCCESS,"clEnqueueBuffer (cell data )failed."))
    return false;
  status = clEnqueueReadBuffer(command_queue_,cell_aux_data_buf_,CL_TRUE,
                               0,cell_data_size_*sizeof(cl_float4),
                               cell_aux_data_,
                               0,NULL,&events[0]);

  if (!this->check_val(status,CL_SUCCESS,"clEnqueueBuffer (cell data )failed."))
    return false;
  status = clWaitForEvents(1, &events[0]);
  if (!this->check_val(status,CL_SUCCESS,"clWaitForEvents failed."))
    return false;

  status = clWaitForEvents(1, &events[0]);
  if (!this->check_val(status,CL_SUCCESS,"clWaitForEvents failed."))
    return false;
  // Wait for the read buffer to finish execution

  status = clReleaseEvent(events[0]);
  return this->check_val(status,CL_SUCCESS,"clReleaseEvent failed.")==1;
}


void boxm_update_ocl_scene_manager::print_tree()
{
  vcl_cout << "Tree Input\n";
  if (cells_)
    for (unsigned i = 0; i<cells_size_*4; i+=4) {
      int data_ptr = 16*cells_[i+2];
      int aux_data_ptr = 4*cells_[i+2];
      vcl_cout << "tree input[" << i/4 << "]("
               << cells_[i]   << ' '
               << cells_[i+1] << ' '
               << cells_[i+2] << ' '
               << cells_[i+3];
      if (data_ptr>0)
        vcl_cout << '[' << cell_data_[data_ptr] << ','
                 << cell_data_[data_ptr+1] << ','
                 << cell_data_[data_ptr+2] << ','
                 << cell_data_[data_ptr+3] << ','
                 << cell_data_[data_ptr+4] << ','
                 << cell_data_[data_ptr+5] << ','
                 << cell_data_[data_ptr+6] << ','
                 << cell_data_[data_ptr+7] << ','
                 << cell_data_[data_ptr+8] << ','
                 << cell_data_[data_ptr+9] << ','
                 << cell_data_[data_ptr+10] << ','
                 << cell_data_[data_ptr+11] << ','
                 << cell_data_[data_ptr+12] << ','
                 << cell_data_[data_ptr+13] << ','
                 << cell_data_[data_ptr+14] << ','
                 << cell_data_[data_ptr+15] << ']';

      vcl_cout << ")\n";
    }
}





bool boxm_update_ocl_scene_manager::clean_update()
{
  return true;
}

/*******************************************
 * build_kernel_program - builds kernel program
 * from source (a vcl string)
 *******************************************/

int boxm_update_ocl_scene_manager::build_kernel_program(cl_program & program)
{
  cl_int status = CL_SUCCESS;
  vcl_size_t sourceSize[] = { this->prog_.size() };
  if (!sourceSize[0]) return SDK_FAILURE;
  if (program) {
    status = clReleaseProgram(program);
    program = 0;
    if (!this->check_val(status,
      CL_SUCCESS,
      "clReleaseProgram failed."))
      return SDK_FAILURE;
  }
  const char * source = this->prog_.c_str();

  program = clCreateProgramWithSource(this->context_,
                                      1,
                                      &source,
                                      sourceSize,
                                      &status);
  if (!this->check_val(status,
                       CL_SUCCESS,
                       "clCreateProgramWithSource failed."))
    return SDK_FAILURE;

  // create a cl program executable for all the devices specified
  status = clBuildProgram(program,
                          1,
                          this->devices_,
                          "",
                          NULL,
                          NULL);
  if (!this->check_val(status,
                       CL_SUCCESS,
                       error_to_string(status)))
  {
    vcl_size_t len;
    char buffer[2048];
    clGetProgramBuildInfo(program, this->devices_[0],
                          CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
    vcl_printf("%s\n", buffer);
    return SDK_FAILURE;
  }
  else
    return SDK_SUCCESS;
}


bool boxm_update_ocl_scene_manager::set_scene_data()
{
    return set_scene_dims()
        && set_scene_origin()
        && set_block_dims()
        && set_block_ptrs()
        && set_root_level();
}


bool boxm_update_ocl_scene_manager::clean_scene_data()
{
    return clean_scene_dims()
        && clean_scene_origin()
        && clean_block_dims()
        && clean_block_ptrs()
        && clean_root_level();
}


bool boxm_update_ocl_scene_manager::set_scene_data_buffers()
{
    return set_scene_dims_buffers()
        && set_scene_origin_buffers()
        && set_block_dims_buffers()
        && set_block_ptrs_buffers()
        && set_root_level_buffers();
}


bool boxm_update_ocl_scene_manager::release_scene_data_buffers()
{
    return release_scene_dims_buffers()
        && release_scene_origin_buffers()
        && release_block_dims_buffers()
        && release_block_ptrs_buffers()
        && release_root_level_buffers();
}


bool boxm_update_ocl_scene_manager::set_root_level()
{
  if (scene_==NULL)
  {
    vcl_cout<<"Scene is Missing "<<vcl_endl;
    return false;
  }
  root_level_=3; // TODO: for now its hardcoded
  return true;
}


bool boxm_update_ocl_scene_manager::clean_root_level()
{
  root_level_=0;
  return true;
}


bool boxm_update_ocl_scene_manager::set_scene_origin()
{
  if (scene_==NULL)
  {
    vcl_cout<<"Scene is Missing "<<vcl_endl;
    return false;
  }
  vgl_point_3d<double> orig=scene_->origin();

  scene_origin_=(cl_float *)boxm_ocl_utils::alloc_aligned(1,sizeof(cl_float4),16);

  scene_origin_[0]=(float)orig.x();
  scene_origin_[1]=(float)orig.y();
  scene_origin_[2]=(float)orig.z();
  scene_origin_[3]=0.0f;

  return true;
}


bool boxm_update_ocl_scene_manager::set_scene_origin_buffers()
{
  cl_int status;
  scene_origin_buf_ = clCreateBuffer(this->context_,
                                     CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     4*sizeof(cl_float),
                                     scene_origin_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (scene_origin_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::release_scene_origin_buffers()
{
  cl_int status;
  status = clReleaseMemObject(scene_origin_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (scene_origin_buf_).")==1;
}


bool boxm_update_ocl_scene_manager::clean_scene_origin()
{
  if (scene_origin_)
    boxm_ocl_utils::free_aligned(scene_origin_);
  return true;
}


bool boxm_update_ocl_scene_manager::set_scene_dims()
{
  if (scene_==NULL)
  {
    vcl_cout<<"Scene is Missing "<<vcl_endl;
    return false;
  }
  scene_->block_num(scene_x_,scene_y_,scene_z_);
  scene_dims_=(cl_int *)boxm_ocl_utils::alloc_aligned(1,sizeof(cl_int4),16);

  scene_dims_[0]=scene_x_;
  scene_dims_[1]=scene_y_;
  scene_dims_[2]=scene_z_;
  scene_dims_[3]=0;

  return true;
}


bool boxm_update_ocl_scene_manager::set_scene_dims_buffers()
{
  cl_int status;
  scene_dims_buf_ = clCreateBuffer(this->context_,
                                   CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   sizeof(cl_int4),
                                   scene_dims_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (scene_dims_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::release_scene_dims_buffers()
{
  cl_int status;
  status = clReleaseMemObject(scene_dims_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (scene_dims_buf_).")==1;
}


bool boxm_update_ocl_scene_manager::clean_scene_dims()
{
  if (scene_dims_)
    boxm_ocl_utils::free_aligned(scene_dims_);
  return true;
}


bool boxm_update_ocl_scene_manager::set_block_dims()
{
  if (scene_==NULL)
  {
    vcl_cout<<"Scene is Missing "<<vcl_endl;
    return false;
  }
  double x,y,z;
  scene_->block_dim(x,y,z);

  block_dims_=(cl_float *)boxm_ocl_utils::alloc_aligned(1,sizeof(cl_float4),16);

  block_dims_[0]=(float)x;
  block_dims_[1]=(float)y;
  block_dims_[2]=(float)z;
  block_dims_[3]=0;

  return true;
}


bool boxm_update_ocl_scene_manager::set_block_dims_buffers()
{
  cl_int status;
  block_dims_buf_ = clCreateBuffer(this->context_,
                                   CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   4*sizeof(cl_float),
                                   block_dims_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (block_dims_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::release_block_dims_buffers()
{
  cl_int status;
  status = clReleaseMemObject(block_dims_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (block_dims_buf_).")==1;
}


bool boxm_update_ocl_scene_manager::clean_block_dims()
{
  if (block_dims_)
    boxm_ocl_utils::free_aligned(block_dims_);
  return true;
}


bool boxm_update_ocl_scene_manager::set_block_ptrs()
{
  if (scene_==NULL)
  {
    vcl_cout<<"Scene is Missing "<<vcl_endl;
    return false;
  }
  scene_->block_num(scene_x_,scene_y_,scene_z_);

  int numblocks=scene_x_*scene_y_*scene_z_;
  block_ptrs_=(cl_int*)boxm_ocl_utils::alloc_aligned(numblocks,sizeof(cl_int4),16);


  vcl_cout<<"B;lock size "<<(float)numblocks*16/1024.0/1024.0<<"MB"<<vcl_endl;
  int index=0;
  for (vbl_array_3d<int4>::iterator iter=scene_->blocks_.begin();iter!=scene_->blocks_.end();iter++)
  {

      block_ptrs_[index++]=(*iter)[0];
      block_ptrs_[index++]=(*iter)[1];
      block_ptrs_[index++]=(*iter)[2];
      block_ptrs_[index++]=(*iter)[3];
  }
    
  return true;
}


bool boxm_update_ocl_scene_manager::set_block_ptrs_buffers()
{
  cl_int status;
  block_ptrs_buf_ = clCreateBuffer(this->context_,
                                   CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   scene_x_*scene_y_*scene_z_*sizeof(cl_int4),
                                   block_ptrs_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (block_ptrs_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::release_block_ptrs_buffers()
{
  cl_int status;
  status = clReleaseMemObject(block_ptrs_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (block_ptrs_buf_).")==1;
}


bool boxm_update_ocl_scene_manager::clean_block_ptrs()
{
  if (block_ptrs_)
    boxm_ocl_utils::free_aligned(block_ptrs_);
  return true;
}


bool boxm_update_ocl_scene_manager::set_root_level_buffers()
{
  cl_int status;
  root_level_buf_ = clCreateBuffer(this->context_,
                                   CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   sizeof(cl_uint),
                                   &root_level_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (root level) failed.")==1;
}


bool boxm_update_ocl_scene_manager::release_root_level_buffers()
{
  cl_int status;
  status = clReleaseMemObject(root_level_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (root_level_buf_)."))
    return false;
  return true;
}


bool boxm_update_ocl_scene_manager::set_input_view()
{
  return set_persp_camera()
      && set_input_image();
}


bool boxm_update_ocl_scene_manager::clean_input_view()
{
  return clean_persp_camera()
      && clean_input_image();
}


bool boxm_update_ocl_scene_manager::set_input_view_buffers()
{
  return set_persp_camera_buffers()
      && set_input_image_buffers()
      && set_image_dims_buffers();
}


bool boxm_update_ocl_scene_manager::release_input_view_buffers()
{
  return release_persp_camera_buffers()
      && release_input_image_buffers();
}


bool boxm_update_ocl_scene_manager::set_all_blocks()
{

  if (!scene_)
    return false;

  scene_->tree_buffer_shape(numbuffer_,lenbuffer_);
  cells_size_=numbuffer_*lenbuffer_;
  cell_data_size_=numbuffer_*lenbuffer_;
  cells_ = NULL;
  cell_data_ = NULL;
  cell_aux_data_=NULL;

  cells_=(cl_int *)boxm_ocl_utils::alloc_aligned(cells_size_,sizeof(cl_int4),16);
  cell_data_=(cl_float *)boxm_ocl_utils::alloc_aligned(cell_data_size_,sizeof(cl_float16),16);
  cell_aux_data_=(cl_float *)boxm_ocl_utils::alloc_aligned(cell_data_size_,sizeof(cl_float4),16);
 

  vcl_cout<<" Cells    "<<(float)cells_size_*16/1024.0/1024.0  <<"MB"<<vcl_endl;
  vcl_cout<<" Data     "<<(float)cells_size_*16*4/1024.0/1024.0<<"MB"<<vcl_endl;
  vcl_cout<<" Aux Data "<<(float)cells_size_*4*4/1024.0/1024.0 <<"MB"<<vcl_endl;

  if (cells_== NULL||cell_data_ == NULL || cell_aux_data_==NULL)
  {
    vcl_cout << "Failed to allocate host memory. (tree input)\n";
    return false;
  }
  for(unsigned i=0;i<cell_data_size_*4;)
      for(unsigned j=0;j<4;j++)
          cell_aux_data_[i++]=0.0;

  int index=0;
  for(vbl_array_2d<int4>::iterator iter=scene_->tree_buffers_.begin();iter!=scene_->tree_buffers_.end();iter++)
  {
      cells_[index++]=(*iter)[0];
      cells_[index++]=(*iter)[1];
      cells_[index++]=(*iter)[2];
      cells_[index++]=(*iter)[3];
  }
  int indexapp=0;
  for(vbl_array_2d<float16>::iterator iter=scene_->data_buffers_.begin();iter!=scene_->data_buffers_.end();iter++)
      for(unsigned i=0;i<16;i++)
          cell_data_[indexapp++]=(*iter)[i];
  return true;
}

void boxm_update_ocl_scene_manager::archive_tree_data()
{
  int index=0;
  for(vbl_array_2d<int4>::iterator iter=scene_->tree_buffers_.begin();iter!=scene_->tree_buffers_.end();iter++)
  {
      (*iter)[0]=cells_[index++];
      (*iter)[1]=cells_[index++];
      (*iter)[2]=cells_[index++];
      (*iter)[3]=cells_[index++];
  }
  int indexapp=0;
  for(vbl_array_2d<float16>::iterator iter=scene_->data_buffers_.begin();iter!=scene_->data_buffers_.end();iter++)
      for(unsigned i=0;i<16;i++)
          (*iter)[i]=cell_data_[indexapp++];
   
 //indexapp=0;
 //for(vbl_array_2d<float16>::iterator iter=scene_->data_buffers_.begin();iter!=scene_->data_buffers_.end();iter++)
 //     for(unsigned i=0;i<4;i++)
 //         vcl_cout<<cell_aux_data_[indexapp++]<<" ";

 // indexapp=0;


}
bool boxm_update_ocl_scene_manager::clean_tree()
{
  if (cells_)
    boxm_ocl_utils::free_aligned(cells_);
  if (cell_data_)
    boxm_ocl_utils::free_aligned(cell_data_);
  if (cell_aux_data_)
    boxm_ocl_utils::free_aligned(cell_aux_data_);

  lenbuffer_=0;
  numbuffer_=0;
  return true;
}


bool boxm_update_ocl_scene_manager::set_tree_buffers()
{
  cl_int status;
  cells_buf_ = clCreateBuffer(this->context_,
                              CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                              cells_size_*sizeof(cl_int4),
                              cells_,&status);
  if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (tree) failed."))
    return false;

  cell_data_buf_ = clCreateBuffer(this->context_,
                                  CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                  cell_data_size_*sizeof(cl_float16),
                                  cell_data_,&status);
 if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (cell data) failed."))
    return false;

  cell_aux_data_buf_ = clCreateBuffer(this->context_,
                                   CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                   cell_data_size_*sizeof(cl_float4),
                                   cell_aux_data_,&status);
  if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (cell data) failed."))
    return false;

  numbuffer_buf_=clCreateBuffer(this->context_,
                                CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                sizeof(cl_uint),
                                &numbuffer_,&status);
  if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (numbuffer_) failed."))
    return false;

  lenbuffer_buf_=clCreateBuffer(this->context_,
                                CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                sizeof(cl_uint),
                                &lenbuffer_,&status);
  if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (lenbuffer_) failed."))
    return false;

 return true;
}


bool boxm_update_ocl_scene_manager::release_tree_buffers()
{
  cl_int status;
  status = clReleaseMemObject(lenbuffer_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (lenbuffer_buf_)."))
    return false;
  status = clReleaseMemObject(numbuffer_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (numbuffer_buf_)."))
    return false;
  
  status = clReleaseMemObject(cells_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (cells_buf_)."))
    return false;
  status = clReleaseMemObject(cell_data_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (cell_data_buf_)."))
    return false;
  status = clReleaseMemObject(cell_aux_data_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (cell_alpha_buf_).");
}


bool boxm_update_ocl_scene_manager::set_persp_camera(vpgl_perspective_camera<double> * pcam)
{
  if (pcam)
  {
    vnl_svd<double>* svd=pcam->svd();
    vnl_matrix<double> Ut=svd->U().conjugate_transpose();
    vnl_matrix<double> V=svd->V();
    vnl_vector<double> Winv=svd->Winverse().diagonal();
    persp_cam_=(cl_float *)boxm_ocl_utils::alloc_aligned(3,sizeof(cl_float16),16);

    int cnt=0;
    for (unsigned i=0;i<Ut.rows();i++)
    {
      for (unsigned j=0;j<Ut.cols();j++)
        persp_cam_[cnt++]=(cl_float)Ut(i,j);

      persp_cam_[cnt++]=0;
    }

    for (unsigned i=0;i<V.rows();i++)
      for (unsigned j=0;j<V.cols();j++)
        persp_cam_[cnt++]=(cl_float)V(i,j);

    for (unsigned i=0;i<Winv.size();i++)
      persp_cam_[cnt++]=(cl_float)Winv(i);

    vgl_point_3d<double> cam_center=pcam->get_camera_center();
    persp_cam_[cnt++]=(cl_float)cam_center.x();
    persp_cam_[cnt++]=(cl_float)cam_center.y();
    persp_cam_[cnt++]=(cl_float)cam_center.z();
    return true;
  }
  else {
    vcl_cerr << "Error set_persp_camera() : Missing camera\n";
    return false;
  }
}


bool boxm_update_ocl_scene_manager::set_persp_camera()
{
  if (vpgl_perspective_camera<double>* pcam =
      dynamic_cast<vpgl_perspective_camera<double>*>(cam_.ptr()))
  {
    vnl_svd<double>* svd=pcam->svd();

    vnl_matrix<double> Ut=svd->U().conjugate_transpose();
    vnl_matrix<double> V=svd->V();
    vnl_vector<double> Winv=svd->Winverse().diagonal();

    persp_cam_=(cl_float *)boxm_ocl_utils::alloc_aligned(3,sizeof(cl_float16),16);

    int cnt=0;
    for (unsigned i=0;i<Ut.rows();i++)
    {
      for (unsigned j=0;j<Ut.cols();j++)
        persp_cam_[cnt++]=(cl_float)Ut(i,j);

      persp_cam_[cnt++]=0;
    }

    for (unsigned i=0;i<V.rows();i++)
      for (unsigned j=0;j<V.cols();j++)
        persp_cam_[cnt++]=(cl_float)V(i,j);

    for (unsigned i=0;i<Winv.size();i++)
      persp_cam_[cnt++]=(cl_float)Winv(i);

    vgl_point_3d<double> cam_center=pcam->get_camera_center();
    persp_cam_[cnt++]=(cl_float)cam_center.x();
    persp_cam_[cnt++]=(cl_float)cam_center.y();
    persp_cam_[cnt++]=(cl_float)cam_center.z();
    return true;
  }
  else {
    vcl_cerr << "Error set_persp_camera() : unsupported camera type\n";
    return false;
  }
}


bool boxm_update_ocl_scene_manager::clean_persp_camera()
{
  if (persp_cam_)
    boxm_ocl_utils::free_aligned(persp_cam_);
  return true;
}


bool boxm_update_ocl_scene_manager::set_persp_camera_buffers()
{
  cl_int status;
  persp_cam_buf_ = clCreateBuffer(this->context_,
                                  CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  3*sizeof(cl_float16),
                                  persp_cam_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (persp_cam_buf_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::write_persp_camera_buffers()
{
  cl_int status;
  status=clEnqueueWriteBuffer(command_queue_,persp_cam_buf_,CL_TRUE, 0,3*sizeof(cl_float16), persp_cam_, 0, 0, 0);
  if (!this->check_val(status,CL_SUCCESS,"clEnqueueWriteBuffer (persp_cam_buf_) failed."))
    return false;
  clFinish(command_queue_);

  return true;
}


bool boxm_update_ocl_scene_manager::release_persp_camera_buffers()
{
  cl_int status;
  status = clReleaseMemObject(persp_cam_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (persp_cam_buf_).")==1;
}



bool boxm_update_ocl_scene_manager::set_input_image()
{
  wni_=(cl_uint)RoundUp(input_img_.ni(),bni_);
  wnj_=(cl_uint)RoundUp(input_img_.nj(),bnj_);

  image_=(cl_float *)boxm_ocl_utils::alloc_aligned(wni_*wnj_,sizeof(cl_float4),16);
  img_dims_=(cl_uint *)boxm_ocl_utils::alloc_aligned(1,sizeof(cl_uint4),16);
  
  for (unsigned i=0;i<wni_*wnj_*4;i++)
        image_[i]=0.0;
  // pad the image
  for (unsigned i=0;i<input_img_.ni();i++)
  {
      for (unsigned j=0;j<input_img_.nj();j++)
      {
          image_[(j*wni_+i)*4]=input_img_(i,j);
          image_[(j*wni_+i)*4+1]=0.0f;
          image_[(j*wni_+i)*4+2]=1.0f;
          image_[(j*wni_+i)*4+3]=0.0f;
      }
  }

  img_dims_[0]=0;
  img_dims_[1]=0;
  img_dims_[2]=input_img_.ni();
  img_dims_[3]=input_img_.nj();

  if (image_==NULL || img_dims_==NULL)
  {
    vcl_cerr<<"Failed allocation of image or image dimensions\n";
    return false;
  }
  return true;
}
bool boxm_update_ocl_scene_manager::set_input_image(vil_image_view<float>  obs)
{
  //wni_=(cl_uint)RoundUp(obs.ni(),bni_);
  //wnj_=(cl_uint)RoundUp(obs.nj(),bnj_);

  //if(image_)
  //    boxm_ocl_utils::free_aligned(image_);

  //image_=(cl_float *)boxm_ocl_utils::alloc_aligned(wni_*wnj_,sizeof(cl_float4),16);
  //if(img_dims_)
  //    boxm_ocl_utils::free_aligned(img_dims_);

  //img_dims_=(cl_uint *)boxm_ocl_utils::alloc_aligned(1,sizeof(cl_uint4),16);

  
  for (unsigned i=0;i<wni_*wnj_*4;i++)
        image_[i]=0.0;
  // pad the image
  for (unsigned i=0;i<obs.ni();i++)
  {
      for (unsigned j=0;j<obs.nj();j++)
      {
          image_[(j*wni_+i)*4]=obs(i,j);
          image_[(j*wni_+i)*4+1]=0.0f;
          image_[(j*wni_+i)*4+2]=1.0f;
          image_[(j*wni_+i)*4+3]=0.0f;
      }
  }

  img_dims_[0]=0;
  img_dims_[1]=0;
  img_dims_[2]=obs.ni();
  img_dims_[3]=obs.nj();

  if (image_==NULL || img_dims_==NULL)
  {
    vcl_cerr<<"Failed allocation of image or image dimensions\n";
    return false;
  }
  return true;
}

bool boxm_update_ocl_scene_manager::clean_input_image()
{
  if (image_)
    boxm_ocl_utils::free_aligned(image_);
  if (img_dims_)
    boxm_ocl_utils::free_aligned(img_dims_);
  return true;
}


bool boxm_update_ocl_scene_manager::set_input_image_buffers()
{
  cl_int status;

  image_buf_ = clCreateBuffer(this->context_,
                              CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                              wni_*wnj_*sizeof(cl_float4),
                              image_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (image_buf_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::write_image_buffer()
{
    cl_int status;
    status=clEnqueueWriteBuffer(command_queue_,image_buf_,CL_TRUE, 0,wni_*wnj_*sizeof(cl_float4), image_, 0, 0, 0);
    if (!this->check_val(status,CL_SUCCESS,"clEnqueueWriteBuffer (image_buf_) failed."))
        return false;
    status=clFinish(command_queue_);
    if (!this->check_val(status,CL_SUCCESS,"clFinish (writing) failed."))
        return false;

    status=clEnqueueWriteBuffer(command_queue_,img_dims_buf_,CL_TRUE, 0,sizeof(cl_uint4), img_dims_, 0, 0, 0);
    if (!this->check_val(status,CL_SUCCESS,"clEnqueueWriteBuffer (imd_dims_buf_) failed."))
        return false;
    status=clFinish(command_queue_);
    if (!this->check_val(status,CL_SUCCESS,"clFinish (writing) failed."))
        return false;

    return true;
}

bool boxm_update_ocl_scene_manager::set_image_dims_buffers()
{
  cl_int status;

  img_dims_buf_ = clCreateBuffer(this->context_,
                                 CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 sizeof(cl_uint4),
                                 img_dims_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (imd_dims_buf_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::release_input_image_buffers()
{
  cl_int status;
  status = clReleaseMemObject(image_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (image_buf_)."))
    return false;

  status = clReleaseMemObject(img_dims_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (img_dims_buf_).")==1;
}


bool boxm_update_ocl_scene_manager::set_offset_buffers(int offset_x,int offset_y,int factor)
{
  cl_int status;
  offset_x_=offset_x;
  offset_y_=offset_y;
  factor_=factor;
  factor_buf_ = clCreateBuffer(this->context_,
                               CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                               sizeof(cl_int),
                               &factor_,&status);
  if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (factor_) failed."))
    return false;

  offset_x_buf_ = clCreateBuffer(this->context_,
                                 CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 sizeof(cl_int),
                                 &offset_x_,&status);
  if (!this->check_val(status,CL_SUCCESS,"clCreateBuffer (offset_x_) failed."))
    return false;
  offset_y_buf_ = clCreateBuffer(this->context_,
                                 CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 sizeof(cl_int),
                                 &offset_y_,&status);
  return this->check_val(status,CL_SUCCESS,"clCreateBuffer (offset_y_) failed.")==1;
}


bool boxm_update_ocl_scene_manager::release_offset_buffers()
{
  cl_int status;
  status = clReleaseMemObject(factor_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (factor_buf_)."))
    return false;

  status = clReleaseMemObject(offset_x_buf_);
  if (!this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (offset_x_buf_)."))
    return false;

  status = clReleaseMemObject(offset_y_buf_);
  return this->check_val(status,CL_SUCCESS,"clReleaseMemObject failed (offset_y_buf_).")==1;
}

