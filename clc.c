/*
  Copyright (c) 2010, Mattias Holm <mattias.holm(at)openorbit.org>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE AUTOHR BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <err.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __APPLE__
  #include <OpenCL/OpenCL.h>
#else
  #include <CL/cl.h>
#endif


typedef struct mapped_file_t {
  off_t fileLenght;
  int fd;
  void *data;
} mapped_file_t;

mapped_file_t map_file(const char *path);
void unmap_file(mapped_file_t *mf);

static off_t
get_flen(const char *path)
{
  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    return 0;
  }

  off_t len = lseek(fd, 0, SEEK_END);
  close(fd);

  return len;
}

mapped_file_t
map_file(const char *path)
{
  mapped_file_t mf = {0, -1, NULL};
  off_t len = get_flen(path);
  if (len == 0) {
    // No data or no file with that name
    return mf;
  }

  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    perror(NULL);
    assert(0 && "file not opened");
  }

  mf.fd = fd;
  mf.data = mmap(NULL, (size_t)len, PROT_READ, MAP_FILE|MAP_PRIVATE, fd, 0);
  mf.fileLenght = len;

  return mf;
}

void
unmap_file(mapped_file_t *mf)
{
  munmap(mf->data, mf->fileLenght);
  close(mf->fd);

  mf->fd = -1;
  mf->data = NULL;
  mf->fileLenght = 0;
}


struct {
  char *infile;
  char *outfile;
  char *platform;
  char *device;
  int shouldListPlatforms;
} options = {NULL, NULL, NULL, NULL, 0};


void
usage()
{
  printf(
    "OpenCL compiler and syntax checker\n"
    "----------------------------------\n"
    "Options:\n"
    "  -c <IN FILENAME> : File to compile.\n"
    "  -o <OUT FILENAME>: Rename out file (default: '<IN FILENAME>.clo').\n"
    "  -a <ARCH NAME>   : Architecture to compile for, string of format\n"
    "                     '<PLATFORM>/<DEVICE>'. The default is first platform and\n"
    "                     the default device is the first device ordered by class\n"
    "                     in priority of DEFAULT, GPU, ACCELERATOR, CPU.\n"
    "  -l               : List all platforms and devices on the system.\n");

  exit(0);
}


void
parseopts(int argc, char **argv)
{
  char ch;
  bool options_set = false;

  while ((ch = getopt(argc, argv, "c:o:a:l")) != -1) {
    options_set = true;

    switch (ch) {
    case 'c':
      options.infile = strdup(optarg);
      break;
    case 'o':
      options.outfile = strdup(optarg);
      break;
    case 'a':
      options.platform = strdup(optarg);
      options.device = strchr(options.platform, '/') + 1;
      break;
    case 'l':
      options.shouldListPlatforms = 1;
      break;
    case '?':
    default:
      usage();
    }
  }

  if (! options_set) {
    usage();
  }


  if (options.infile && (options.outfile == NULL)) {
    asprintf(&options.outfile, "%s.clo", options.infile);
  }
}

#define PNAME_SIZE 128
cl_uint gNumPlatforms = 0;
cl_platform_id *gPlatforms = NULL;
char **gPlatformNames = NULL;
char **gPlatformVendors = NULL;
cl_uint *gNumDevices = NULL;
cl_device_id **gDevices = NULL;

void
get_platforms(void)
{
  // Get number of platforms
  if (clGetPlatformIDs(0, NULL, &gNumPlatforms) != CL_SUCCESS) {
    errx(1, "did not find any platforms");
  }

  gPlatforms = calloc(gNumPlatforms, sizeof(cl_platform_id));
  gPlatformNames = calloc(gNumPlatforms, sizeof(char*));
  gPlatformVendors = calloc(gNumPlatforms, sizeof(char*));

  if (clGetPlatformIDs(gNumPlatforms, gPlatforms, NULL) != CL_SUCCESS) {
    errx(1, "could not read platform list");
  }

  for (cl_uint i = 0 ; i < gNumPlatforms ; i ++) {
    gPlatformNames[i] = calloc(PNAME_SIZE, sizeof(char));
    if (clGetPlatformInfo(gPlatforms[i], CL_PLATFORM_NAME, PNAME_SIZE, gPlatformNames[i], NULL) != CL_SUCCESS) {
      errx(1, "could not read platform name");
    }

    gPlatformVendors[i] = calloc(PNAME_SIZE, sizeof(char));
    if (clGetPlatformInfo(gPlatforms[i], CL_PLATFORM_VENDOR, PNAME_SIZE, gPlatformVendors[i], NULL) != CL_SUCCESS) {
      errx(1, "could not read platform vendor");
    }
  }
}

void
list_platforms(void)
{
  printf("Platforms supported on THIS machine:\n");
  printf("ID: VENDOR / NAME\n");
  for (cl_uint i = 0 ; i < gNumPlatforms ; i++) {
    printf("%u: '%s' / '%s'\n", i, gPlatformVendors[i], gPlatformNames[i]);
  }
}

void
get_devices(void)
{
  gNumDevices = calloc(gNumPlatforms, sizeof(cl_uint));
  gDevices = calloc(gNumPlatforms, sizeof(cl_device_id*));

  for (cl_uint i = 0 ; i < gNumPlatforms ; i++) {
    if (clGetDeviceIDs(gPlatforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &gNumDevices[i]) != CL_SUCCESS) {
      errx(1, "could not get device count for platform %s\n", gPlatformNames[i]);
    }
    gDevices[i] = calloc(gNumDevices[i], sizeof(cl_device_id));

    if (clGetDeviceIDs(gPlatforms[i], CL_DEVICE_TYPE_ALL, gNumDevices[i], gDevices[i], NULL) != CL_SUCCESS) {
      errx(1, "could not get devices for platform %s\n", gPlatformNames[i]);
    }
  }
}

void
list_devices(void)
{
  cl_device_type typ;
  char name[128];
  char vendor[128];

  printf("Devices supported on THIS machine:\n");
  printf("PLATFORM ID: VENDOR / NAME TYPE\n");

  for (cl_uint i = 0 ; i < gNumPlatforms ; i++) {
    for (cl_uint j = 0 ; j < gNumDevices[i] ; j++) {
      clGetDeviceInfo(gDevices[i][j], CL_DEVICE_TYPE, sizeof(cl_device_type), &typ, NULL);
      clGetDeviceInfo(gDevices[i][j], CL_DEVICE_NAME, sizeof(name), &name, NULL);
      clGetDeviceInfo(gDevices[i][j], CL_DEVICE_VENDOR, sizeof(vendor), &vendor, NULL);


      printf("%d: '%s' / '%s' ", i, vendor, name);

      if (typ & CL_DEVICE_TYPE_CPU) printf("-processor-");
      if (typ & CL_DEVICE_TYPE_GPU) printf("-graphics processor-");
      if (typ & CL_DEVICE_TYPE_ACCELERATOR) printf("-accellerator-");
      if (typ & CL_DEVICE_TYPE_DEFAULT) printf("-default-");
      printf("\n");
    }
  }
}

cl_device_id
get_device_for_keys(const char *platformName, const char *devName)
{
  char name[128];

  for (cl_uint i = 0 ; i < gNumPlatforms ; i++) {
    if (! strcmp(gPlatformNames[i], platformName) ) {
      for (cl_uint j = 0 ; j < gNumDevices[i] ; j++) {
        clGetDeviceInfo(gDevices[i][j], CL_DEVICE_NAME, sizeof(name), &name, NULL);
        if ( !strcmp(devName, name)) {
          return gDevices[i][j];
        }
      }
    }
  }

  errx(1, "could not find device '%s/%s'\n");
}



cl_device_id
get_default_device(void)
{
  cl_device_type typ;

  // TODO:
  //clGetDeviceIDs(platform, CL_DEVICE_TYPE_DEFAULT, 10, devices, &numDevices)

  for (cl_uint i = 0 ; i < gNumPlatforms ; i++) {
    for (cl_uint j = 0 ; j < gNumDevices[i] ; j++) {
      clGetDeviceInfo(gDevices[i][j], CL_DEVICE_TYPE, sizeof(cl_device_type), &typ, NULL);
      if (typ & CL_DEVICE_TYPE_DEFAULT) return gDevices[i][j];
    }
    for (cl_uint j = 0 ; j < gNumDevices[i] ; j++) {
      clGetDeviceInfo(gDevices[i][j], CL_DEVICE_TYPE, sizeof(cl_device_type), &typ, NULL);
      if (typ & CL_DEVICE_TYPE_GPU) return gDevices[i][j];
    }
    for (cl_uint j = 0 ; j < gNumDevices[i] ; j++) {
      clGetDeviceInfo(gDevices[i][j], CL_DEVICE_TYPE, sizeof(cl_device_type), &typ, NULL);
      if (typ & CL_DEVICE_TYPE_ACCELERATOR) return gDevices[i][j];
    }
    for (cl_uint j = 0 ; j < gNumDevices[i] ; j++) {
      clGetDeviceInfo(gDevices[i][j], CL_DEVICE_TYPE, sizeof(cl_device_type), &typ, NULL);
      if (typ & CL_DEVICE_TYPE_CPU) return gDevices[i][j];
    }
  }
  assert(0 && "should not be reached");
}

cl_context
create_context(cl_device_id devid)
{
  cl_platform_id platform;

  if (clGetDeviceInfo(devid, CL_DEVICE_PLATFORM, sizeof(platform), &platform, NULL) != CL_SUCCESS) {
    errx(1, "could not get platform for device");
  }

  cl_context_properties prop[] = {
    CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
    0
  };

  cl_int err;

  cl_context ctxt = clCreateContext(prop,
                                    1, &devid, // One device in our context
                                    NULL, NULL, // No notificiation function
                                    &err);
  if (err != CL_SUCCESS) {
    errx(1, "could not create context");
  }

  return ctxt;
}


cl_device_id
get_and_check_device(void)
{
  cl_device_id devid = get_device_for_keys(options.platform, options.device);
  return devid;
}


// Large enough for anybody, still, we don't want this on the stack.
char buildlog[128*1024];


int main(int argc, char** argv) {
  parseopts(argc, argv);

  get_platforms();
  get_devices();

  if (options.shouldListPlatforms) {
    list_platforms();
    list_devices();
    exit(0);
  }

  cl_device_id device;
  if ((options.platform == NULL) || (options.device == NULL)) {
    device = get_default_device();
  } else {
    device = get_and_check_device();
  }

  cl_context ctxt = create_context(device);

  mapped_file_t in = map_file(options.infile);

  // create & compile program
  cl_int err;
  size_t sz = in.fileLenght;

  cl_program prog = clCreateProgramWithSource(ctxt, 1, (const char**)&in.data, &sz, &err);

  cl_int res = clBuildProgram(prog, 0, 0, 0, 0, 0);
  switch (res) {
  case CL_SUCCESS:
    break;
  case CL_BUILD_PROGRAM_FAILURE:
    clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, sizeof(buildlog), buildlog, NULL);
    buildlog[1023] = '\0';
    fprintf(stderr, "%s\n", buildlog);
    break;
  default:
    errx(1, "unhandled error when building program");
  }


  cl_uint devices = 0;

  if (clGetProgramInfo(prog, CL_PROGRAM_NUM_DEVICES,
                       sizeof(cl_uint), &devices, NULL) != CL_SUCCESS) {
    errx(1, "could not extract number of devices from prog");
  }

  size_t binsizes[devices];
  size_t binsize_count;

  if (clGetProgramInfo(prog, CL_PROGRAM_BINARY_SIZES,
                       sizeof(binsizes), binsizes, &binsize_count) != CL_SUCCESS) {
    errx(1, "could not extract binary sizes");
  }

  unsigned char *binaries[devices];
  size_t binary_count = 0;
  for (unsigned i = 0 ; i < devices ; i ++) {
    binaries[i] = (unsigned char*)alloca(binsizes[i]);
  }

  if (clGetProgramInfo(prog, CL_PROGRAM_BINARIES,
                       sizeof(binaries), binaries, &binary_count) != CL_SUCCESS) {
    errx(1, "could not extract binaries");
  }

  char tmp[strlen(options.outfile)+4];

  for (unsigned i = 0 ; i < devices ; i++) {
    if (devices > 1) {
      sprintf(tmp, "%s.%d", options.outfile, i);
    } else {
      sprintf(tmp, "%s", options.outfile);
    }

    int fd = open(tmp, O_CREAT|O_RDWR);
    fchmod(fd, S_IRUSR|S_IWUSR);

    if (write(fd, binaries[i], binsizes[i]) != (ssize_t)binsizes[i]) {
      fprintf(stderr, "did not write full file\n");
    }
    close(fd);
  }

  return 0;
}
