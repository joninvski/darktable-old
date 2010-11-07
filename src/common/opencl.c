/*
    This file is part of darktable,
    copyright (c) 2009--2010 johannes hanika.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <strings.h>
#include <stdio.h>

#include "common/darktable.h"
#include "common/opencl.h"

void dt_opencl_init(dt_opencl_t *cl)
{
  cl->inited = 0;
  pthread_mutex_init(&cl->lock, NULL);
  cl_int err;
  cl_platform_id all_platforms[5];
  cl_platform_id platform = NULL;
  cl_uint num_platforms = 5;
  err = clGetPlatformIDs (5, all_platforms, &num_platforms);
  if(err != CL_SUCCESS)
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_init] could not get platforms: %d\n", err);
    return;
  }
  platform = all_platforms[0];

  // get the number of GPU devices available to the platform
  // the other common option is CL_DEVICE_TYPE_GPU/CPU (but the latter doesn't work with the nvidia drivers)
  cl_uint num_devices = 0;
  err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
  if(err != CL_SUCCESS)
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_init] could not get device id size: %d\n", err);
    return;
  }

  // create the device list
  cl->num_devs = num_devices;
  cl->dev = (dt_opencl_device_t *)malloc(sizeof(dt_opencl_device_t)*num_devices);
  cl_device_id *devices = (cl_device_id *)malloc(sizeof(cl_device_id)*num_devices);
  err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, num_devices, devices, NULL);
  if(err != CL_SUCCESS)
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_init] could not get devices list: %d\n", err);
    return;
  }
  dt_print(DT_DEBUG_OPENCL, "[opencl_init] found %d devices\n", num_devices);
  for(int k=0;k<num_devices;k++)
  {
    bzero(cl->dev[k].program_used, sizeof(int)*DT_OPENCL_MAX_PROGRAMS);
    bzero(cl->dev[k].kernel_used,  sizeof(int)*DT_OPENCL_MAX_KERNELS);
    pthread_mutex_init(&cl->dev[k].lock, NULL);
    cl->dev[k].devid = devices[k];
    if(darktable.unmuted & DT_DEBUG_OPENCL)
    {
      char infostr[1024];
      size_t infoint;
      size_t infointtab[1024];
      clGetDeviceInfo(cl->dev[k].devid, CL_DEVICE_NAME, sizeof(infostr), &infostr, NULL);
      printf("[opencl_init] device %d: %s \n", k, infostr);
      clGetDeviceInfo(cl->dev[k].devid, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(infoint), &infoint, NULL);
      printf("     MAX_WORK_GROUP_SIZE:      %ld\n", infoint);
      clGetDeviceInfo(cl->dev[k].devid, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(infoint), &infoint, NULL);
      printf("     MAX_WORK_ITEM_DIMENSIONS: %ld\n", infoint);
      printf("     MAX_WORK_ITEM_SIZES:      [ ");
      clGetDeviceInfo(cl->dev[k].devid, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(infointtab), infointtab, NULL);
      for (int i=0;i<infoint;i++) printf("%ld ", infointtab[i]);
      printf("]\n");
    }

    cl->dev[k].context = clCreateContext(0, 1, &cl->dev[k].devid, NULL, NULL, &err);
    if(err != CL_SUCCESS)
    {
      dt_print(DT_DEBUG_OPENCL, "[opencl_init] could not create context for device %d: %d\n", k, err);
      return;
    }
    // create a command queue for first device the context reported
    cl->dev[k].cmd_queue = clCreateCommandQueue(cl->dev[k].context, cl->dev[k].devid, 0, &err);
    if(err != CL_SUCCESS)
    {
      dt_print(DT_DEBUG_OPENCL, "[opencl_init] could not create command queue for device %d: %d\n", k, err);
      return;
    }
    char dtpath[1024], filename[1024], programname[1024];
    dt_get_datadir(dtpath, 1024);
    snprintf(filename, 1024, "%s/kernels/programs.conf", dtpath);
    // now load all darktable cl kernels.
    // TODO: compile as a job?
    FILE *f = fopen(filename, "rb");
    if(f)
    {
      while(!feof(f))
      {
        int rd = fscanf(f, "%[^\n]\n", programname);
        if(rd != 1) continue;
        // remove comments:
        for(int k=0;k<strlen(programname);k++) if(programname[k] == '#')
        {
          programname[k] = '\0';
          while(programname[--k] == ' ') programname[k] = '\0';
          break;
        }
        if(programname[0] == '\0') continue;
        snprintf(filename, 1024, "%s/kernels/%s", dtpath, programname);
        dt_print(DT_DEBUG_OPENCL, "[opencl_init] compiling program `%s' ..\n", programname);
        const int prog = dt_opencl_load_program(cl, k, filename);
        if(dt_opencl_build_program(cl, k, prog))
          dt_print(DT_DEBUG_OPENCL, "[opencl_init] failed to compile program `%s'!\n", programname);
      }
      fclose(f);
    }
    else
    {
      dt_print(DT_DEBUG_OPENCL, "[opencl_init] could not open `%s'!\n", filename);
      return;
    }
  }
  free(devices);
  dt_print(DT_DEBUG_OPENCL, "[opencl_init] successfully initialized.\n");
  cl->inited = 1;
  return;
}

void dt_opencl_cleanup(dt_opencl_t *cl)
{
  for(int i=0;i<cl->num_devs;i++)
  {
    pthread_mutex_destroy(&cl->dev[i].lock);
    for(int k=0;k<DT_OPENCL_MAX_KERNELS; k++) if(cl->dev[i].kernel_used [k]) clReleaseKernel (cl->dev[i].kernel [k]);
    for(int k=0;k<DT_OPENCL_MAX_PROGRAMS;k++) if(cl->dev[i].program_used[k]) clReleaseProgram(cl->dev[i].program[k]);
    clReleaseCommandQueue(cl->dev[i].cmd_queue);
    clReleaseContext(cl->dev[i].context);
  }
  pthread_mutex_destroy(&cl->lock);
}

int dt_opencl_lock_device(dt_opencl_t *cl, const int _dev)
{
  int dev = _dev;
  if(dev < 0 || dev >= cl->num_devs) dev = 0;
  for(int i=0;i<cl->num_devs;i++)
  {
    // start at argument and get first currently unused processor
    const int try_dev = (dev + i) % cl->num_devs;
    if(!pthread_mutex_trylock(&cl->dev[try_dev].lock)) return try_dev;
  }
  // no free GPU :( lock the requested one, after blocking.
  pthread_mutex_lock(&cl->dev[dev].lock);
  return dev;
}

void dt_opencl_unlock_device(dt_opencl_t *cl, const int dev)
{
  if(dev < 0 || dev >= cl->num_devs) return;
  pthread_mutex_unlock(&cl->dev[dev].lock);
}

int dt_opencl_load_program(dt_opencl_t *cl, const int dev, const char *filename)
{
  cl_int err;
  FILE *f = fopen(filename, "rb");
  if(!f)
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_load_program] could not open file `%s'!\n", filename);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  size_t filesize = ftell(f);
  fseek(f, 0, SEEK_SET);
  char file[filesize+1];
  int rd = fread(file, sizeof(char), filesize, f);
  fclose(f);
  if(rd != filesize)
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_load_program] could not read all of file `%s'!\n", filename);
    return -1;
  }
  if(file[filesize-1] != '\n')
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_load_program] no newline at end of file `%s'!\n", filename);
    file[filesize] = '\n';
  }
  int lines = 0;
  for(int k=0;k<filesize;k++) if(file[k] == '\n') lines++;
  const char *sptr[lines+1];
  size_t lengths[lines];
  int curr = 0;
  sptr[curr++] = file;
  for(int k=0;k<filesize;k++)
    if(file[k] == '\n')
    {
      sptr[curr] = file + k + 1;
      lengths[curr-1] = sptr[curr] - sptr[curr-1];
      curr++;
    }
  lengths[lines-1] = file + filesize - sptr[lines-1];
  sptr[lines] = NULL;
  int k = 0;
  for(;k<DT_OPENCL_MAX_PROGRAMS;k++) if(!cl->dev[dev].program_used[k])
  {
    cl->dev[dev].program_used[k] = 1;
    cl->dev[dev].program[k] = clCreateProgramWithSource(cl->dev[dev].context, lines, sptr, lengths, &err);
    if(err != CL_SUCCESS)
    {
      dt_print(DT_DEBUG_OPENCL, "[opencl_load_program] could not create program from file `%s'! (%d)\n", filename, err);
      cl->dev[dev].program_used[k] = 0;
      return -1;
    }
    else break;
  }
  if(k < DT_OPENCL_MAX_PROGRAMS)
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_load_program] successfully loaded program from `%s'\n", filename);
    return k;
  }
  else
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_load_program] too many programs! can't load `%s'\n", filename);
    return -1;
  }
}

int dt_opencl_build_program(dt_opencl_t *cl, const int dev, const int prog)
{
  if(prog < 0 || prog >= DT_OPENCL_MAX_PROGRAMS) return -1;
  cl_program program = cl->dev[dev].program[prog];
  cl_int err;
  // TODO: how to pass sm_23 for fermi?
  err = clBuildProgram(program, 1, &cl->dev[dev].devid, "-cl-fast-relaxed-math -cl-strict-aliasing", 0, 0);
  if(err != CL_SUCCESS)
  {
    dt_print(DT_DEBUG_OPENCL, "[opencl_build_program] could not build program: %d\n", err);
    cl_build_status build_status;
    clGetProgramBuildInfo(program, cl->dev[dev].devid, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &build_status, NULL);
    if (build_status != CL_SUCCESS)
    {
      char *build_log;
      size_t ret_val_size;
      clGetProgramBuildInfo(program, cl->dev[dev].devid, CL_PROGRAM_BUILD_LOG, 0, NULL, &ret_val_size);
      build_log = (char *)malloc(sizeof(char)*(ret_val_size+1));
      clGetProgramBuildInfo(program, cl->dev[dev].devid, CL_PROGRAM_BUILD_LOG, ret_val_size, build_log, NULL);

      build_log[ret_val_size] = '\0';

      dt_print(DT_DEBUG_OPENCL, "BUILD LOG:\n");
      dt_print(DT_DEBUG_OPENCL, "%s\n", build_log);

      free(build_log);
    }
  }
  dt_print(DT_DEBUG_OPENCL, "[opencl_build_program] successfully built program\n");
  return err;
}

int dt_opencl_create_kernel(dt_opencl_t *cl, const int prog, const char *name)
{
  if(prog < 0 || prog >= DT_OPENCL_MAX_PROGRAMS) return -1;
  pthread_mutex_lock(&cl->lock);
  int k = 0;
  for(int dev=0;dev<cl->num_devs;dev++)
  {
    cl_int err;
    for(;k<DT_OPENCL_MAX_KERNELS;k++) if(!cl->dev[dev].kernel_used[k])
    {
      cl->dev[dev].kernel_used[k] = 1;
      cl->dev[dev].kernel[k] = clCreateKernel(cl->dev[dev].program[prog], name, &err);
      if(err != CL_SUCCESS)
      {
        dt_print(DT_DEBUG_OPENCL, "[opencl_create_kernel] could not create kernel `%s'! (%d)\n", name, err);
        cl->dev[dev].kernel_used[k] = 0;
        goto error;
      }
      else break;
    }
    if(k < DT_OPENCL_MAX_KERNELS)
    {
      dt_print(DT_DEBUG_OPENCL, "[opencl_create_kernel] successfully loaded kernel `%s' (%d) for device %d\n", name, k, dev);
    }
    else
    {
      dt_print(DT_DEBUG_OPENCL, "[opencl_create_kernel] too many kernels! can't create kernel `%s'\n", name);
      goto error;
    }
  }
  pthread_mutex_unlock(&cl->lock);
  return k;
error:
  pthread_mutex_unlock(&cl->lock);
  return -1;
}

void dt_opencl_free_kernel(dt_opencl_t *cl, const int kernel)
{
  if(kernel < 0 || kernel >= DT_OPENCL_MAX_KERNELS) return;
  pthread_mutex_lock(&cl->lock);
  for(int dev=0;dev<cl->num_devs;dev++)
  {
    cl->dev[dev].kernel_used [kernel] = 0;
    clReleaseKernel (cl->dev[dev].kernel [kernel]);
  }
  pthread_mutex_unlock(&cl->lock);
}

int dt_opencl_get_max_work_item_sizes(dt_opencl_t *cl, const int dev, size_t *sizes)
{
  return clGetDeviceInfo(cl->dev[dev].devid, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(size_t)*3, sizes, NULL);
}

int dt_opencl_set_kernel_arg(dt_opencl_t *cl, const int dev, const int kernel, const int num, const size_t size, const void *arg)
{
  if(kernel < 0 || kernel >= DT_OPENCL_MAX_KERNELS) return -1;
  return clSetKernelArg(cl->dev[dev].kernel[kernel], num, size, arg);
}

int dt_opencl_enqueue_kernel_2d(dt_opencl_t *cl, const int dev, const int kernel, const size_t *sizes)
{
  if(kernel < 0 || kernel >= DT_OPENCL_MAX_KERNELS) return -1;
  // const size_t local[2] = {16, 16};
  // let the driver choose:
  const size_t *local = NULL;
  return clEnqueueNDRangeKernel(cl->dev[dev].cmd_queue, cl->dev[dev].kernel[kernel], 2, NULL, sizes, local, 0, NULL, NULL);
}

