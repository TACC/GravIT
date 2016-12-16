#define TBB_PREVIEW_STATIC_PARTITIONER 1
#include "gvt/render/adapter/ospray/data/OSPRayAdapter.h"
#include "gvt/core/CoreContext.h"

using namespace gvt::render::adapter::ospray::data;

bool OSPRayAdapter::init = false;

OSPRayAdapter::OSPRayAdapter(gvt::render::data::primitives::Data *data):Adapter(data) {
  theOSPRenderer = ospNewRenderer("ptracer");
}
OSPRayAdapter::OSPRayAdapter(gvt::render::data::primitives::Mesh *data):Adapter(data) {
  theOSPRenderer = ospNewRenderer("ptracer");
}
/***
 * following the function of the other adapters all this one does is map the data
 * in the GVT volume to ospray datatypes. If we are doing this right then this is
 * the first place in an application where ospray calls are made. So this is a 
 * reasonable place to init ospray. In the end you should have an initialized
 * ospray volume object. The adapter needs to maintain a pointer to an ospray model 
 * object.
 */
OSPRayAdapter::OSPRayAdapter(gvt::render::data::primitives::Volume *data):Adapter(data) {
  int n_slices,n_isovalues;
  glm::vec4 *slices;
  glm::vec3 globalorigin;
  glm::vec3 volumedimensions;
  glm::vec3 volumespacing;
  float *isovalues;
  //std::cout << "starting new osprayadapter " << std::endl;
  gvt::render::RenderContext *cntxt = gvt::render::RenderContext::instance();
  gvt::core::DBNodeH root = cntxt->getRootNode();
  width = root["Film"]["width"].value().toInteger();
  height = root["Film"]["height"].value().toInteger();
  theOSPRenderer = ospNewRenderer("ptracer");
  // build the ospray volume from the gvt volume
  //if(theOSPVolume) ospRelease(theOSPVolume);
  //if(theOSPData) ospRelease(theOSPData);
  //if(theOSPModel) ospRelease(theOSPModel);
  theOSPVolume = ospNewVolume("shared_structured_volume");
  data->GetSlices(n_slices,slices);
  if(n_slices != 0) {
    float *slicevector = new float[4*n_slices];
    for(int i=0;i<n_slices;i++)
      std::memcpy(glm::value_ptr(slices[i]),slicevector+(4*i),4*sizeof(float));
      //slicevector[i*4] = glm::value_ptr(slices[i]);
      OSPData sliceData = ospNewData(n_slices,OSP_FLOAT4,slicevector);
      ospSetData(theOSPVolume,"slices",sliceData);
  }
  data->GetIsovalues(n_isovalues,isovalues);
  if(n_isovalues != 0) {
    OSPData isoData = ospNewData(n_isovalues,OSP_FLOAT,isovalues);
    ospSetData(theOSPVolume,"isovalues",isoData);
  }
  data->GetGlobalOrigin(globalorigin);
  osp::vec3f origin;
  origin.x = globalorigin.x;
  origin.y = globalorigin.y;
  origin.z = globalorigin.z;
  ospSetVec3f(theOSPVolume,"gridOrigin",origin);
  data->GetCounts(volumedimensions);
  osp::vec3i counts;
  counts.x = volumedimensions.x;
  counts.y = volumedimensions.y;
  counts.z = volumedimensions.z;
  ospSetVec3i(theOSPVolume,"dimensions",counts);
  data->GetDeltas(volumespacing);
  osp::vec3f spacing;
  spacing.x = volumespacing.x;
  spacing.y = volumespacing.y;
  spacing.z = volumespacing.z;
  ospSetVec3f(theOSPVolume,"gridSpacing",spacing);
  gvt::render::data::primitives::Volume::VoxelType vt = data->GetVoxelType();
  switch(vt){
    case gvt::render::data::primitives::Volume::FLOAT : ospSetString(theOSPVolume,"voxelType","float");
          int numberofsamples = counts.x*counts.y*counts.z;
          OSPData voldata = ospNewData(numberofsamples,OSP_FLOAT,(void*)data->GetSamples(),OSP_DATA_SHARED_BUFFER);
          ospCommit(voldata);
          ospSetObject(theOSPVolume,"voxelData",voldata);
                 break;
    case gvt::render::data::primitives::Volume::UCHAR : ospSetString(theOSPVolume,"voxelType","uchar");
                 break;
    default : std::cerr << " error setting voxel type " << std::endl;
              break;
  }
  data->SetSamplingRate(10.0);
  ospSet1f(theOSPVolume,"samplingRate",data->GetSamplingRate());
  data->GetTransferFunction()->set();
  ospSetObject(theOSPVolume,"transferFunction",data->GetTransferFunction()->GetTheOSPTransferFunction());
  ospSet1i(theOSPVolume,"volume rendering",1);
  ospCommit(theOSPVolume);
  // make a model and stuff the volume in it.
  theOSPModel = ospNewModel();
  ospAddVolume(theOSPModel,theOSPVolume);
  ospCommit(theOSPModel);
  // the model should be added to the renderer
  ospSetObject(theOSPRenderer,"model",theOSPModel);
  ospCommit(theOSPRenderer);
}

/*** this routine maps ospexternal rays to gravit rays
 *
 */
void OSPRayAdapter::OSP2GVTMoved_Rays(OSPExternalRays &out, OSPExternalRays &rl, gvt::render::actor::RayVector &moved_rays) {
  int raycount;
  // plug in the rays into moved_rays
  // the idea is to pile all the rays to moved_rays and let the scheduler sort 'em
  // first check the out rays. out consists of generated rays (ao, shadow, ?) 
  if( out && out->GetCount() != 0) { // pack the output into moved_rays
    raycount = out->GetCount();
    moved_rays.resize(raycount);
    for (int i=0; i< out->GetCount(); i++) {
      gvt::render::actor::Ray &ray = moved_rays[i];
      ray.origin.x = out->xr.ox[i];
      ray.origin.y = out->xr.oy[i];
      ray.origin.z = out->xr.oz[i];
      ray.direction.x = out->xr.dx[i];
      ray.direction.y = out->xr.dy[i];
      ray.direction.z = out->xr.dz[i];
      ray.color.r = out->xr.r[i];
      ray.color.g = out->xr.g[i];
      ray.color.b = out->xr.b[i];
      ray.w = out->xr.o[i]; // store ospray opacity in the w component of the gvt ray
      ray.t = out->xr.t[i];
      ray.t_max = out->xr.tMax[i];
      ray.id = out->xr.y[i]*width + out->xr.x[i];
    //  ray.depth = 10;
      ray.type = out->xr.type[i] == EXTERNAL_RAY_PRIMARY ? RAY_PRIMARY :
        out->xr.type[i] == EXTERNAL_RAY_SHADOW ? RAY_SHADOW :
        out->xr.type[i] == EXTERNAL_RAY_AO ? RAY_AO : RAY_EMPTY;
      //int spoot = (out->xr.term[i] & EXTERNAL_RAY_SURFACE ? RAY_SURFACE : 0 ) |
      ray.depth = (out->xr.term[i] & EXTERNAL_RAY_SURFACE ? RAY_SURFACE : 0 ) |
        (out->xr.term[i] & EXTERNAL_RAY_OPAQUE ? RAY_OPAQUE : 0) |
        (out->xr.term[i] & EXTERNAL_RAY_BOUNDARY ? RAY_BOUNDARY : 0) |
        (out->xr.term[i] & EXTERNAL_RAY_TIMEOUT ? RAY_TIMEOUT : 0);
    }
  } else { raycount = 0; }
  // now do the rl rays which may be terminated as indicated in their term variable.  
  moved_rays.resize(raycount + rl->GetCount());
  for(int i=raycount, ii=0; i <raycount + rl->GetCount();i++,ii++){
    gvt::render::actor::Ray &ray = moved_rays[i];
    ray.origin.x = rl->xr.ox[ii];
    ray.origin.y = rl->xr.oy[ii];
    ray.origin.z = rl->xr.oz[ii];
    ray.direction.x = rl->xr.dx[ii];
    ray.direction.y = rl->xr.dy[ii];
    ray.direction.z = rl->xr.dz[ii];
    ray.color.r = rl->xr.r[ii];
    ray.color.g = rl->xr.g[ii];
    ray.color.b = rl->xr.b[ii];
    if((rl->xr.r[ii] != 0 ) || (rl->xr.g[ii] != 0) || (rl->xr.b[ii]!= 0)) 
      std::cout << rl->xr.r[ii] << " " << rl->xr.g[ii] << " " << rl->xr.b[ii]  << std::endl;
    ray.w = rl->xr.o[ii];
    ray.t = rl->xr.t[ii];
    ray.t_max = rl->xr.tMax[ii];
    //ray.id = rl->xr.x[i];
    ray.id = rl->xr.y[ii]*width + rl->xr.x[ii];
    //ray.depth = rl->xr.y[i];
    //ray.depth = 1.0;
    ray.type = rl->xr.type[ii] == EXTERNAL_RAY_PRIMARY ? RAY_PRIMARY :
      rl->xr.type[ii] == EXTERNAL_RAY_SHADOW ? RAY_SHADOW :
      rl->xr.type[ii] == EXTERNAL_RAY_AO ? RAY_AO : RAY_EMPTY;
    //int spoot = (rl->xr.term[i] & EXTERNAL_RAY_SURFACE ? RAY_SURFACE : 0 ) |
    ray.depth = (rl->xr.term[ii] & EXTERNAL_RAY_SURFACE ? RAY_SURFACE : 0 ) |
      (rl->xr.term[ii] & EXTERNAL_RAY_OPAQUE ? RAY_OPAQUE : 0) |
      (rl->xr.term[ii] & EXTERNAL_RAY_BOUNDARY ? RAY_BOUNDARY : 0) |
      (rl->xr.term[ii] & EXTERNAL_RAY_TIMEOUT ? RAY_TIMEOUT : 0);
    //memcpy((void*)&(ray.t_min), (void*) &spoot, sizeof(spoot));
  }
}
OSPExternalRays OSPRayAdapter::GVT2OSPRays(gvt::render::actor::RayVector &rayList) { 
  OSPExternalRays out = ospNewExternalRays() ;
  out->Allocate(rayList.size());
  for (int i = 0; i<rayList.size();i++) {
    out->xr.ox[i] = rayList[i].origin.x;
    out->xr.oy[i] = rayList[i].origin.y;
    out->xr.oz[i] = rayList[i].origin.z;
    out->xr.dx[i] = rayList[i].direction.x;
    out->xr.dy[i] = rayList[i].direction.y;
    out->xr.dz[i] = rayList[i].direction.z;
    out->xr.r[i] = rayList[i].color.r;
    out->xr.g[i] = rayList[i].color.g;
    out->xr.b[i] = rayList[i].color.b;
    //out->xr.o[i] = 0.0; // volume renderer uses w to carry opacity in and out.
    out->xr.o[i] = rayList[i].w; // volume renderer uses w to carry opacity in and out.
    out->xr.t[i] = rayList[i].t;
    //out->xr.t[i] = 0.0;
    out->xr.tMax[i] = rayList[i].t_max;
    out->xr.type[i] = RAY_PRIMARY ;
    //out->xr.type[i] = rayList[i].type == RAY_PRIMARY ? EXTERNAL_RAY_PRIMARY :
     // rayList[i].type == RAY_SHADOW ? EXTERNAL_RAY_SHADOW :
      //rayList[i].type == RAY_AO ? EXTERNAL_RAY_AO : EXTERNAL_RAY_EMPTY;
    out->xr.term[i] = 0;
    // x and y are calculated from ray id and image dimensions. 
    out->xr.x[i] = rayList[i].id % width; // volume renderer uses id to store px
    out->xr.y[i] = rayList[i].id / width; // volume renderer uses depth to store py
    //out->xr.y[i] = rayList[i].depth; // volume renderer uses depth to store py
    //std::cout << out->xr.ox[i] << " " << out->xr.oy[i] << " " << out->xr.oz[i] << " " ;
    //std::cout << out->xr.dx[i] << " " << out->xr.dy[i] << " " << out->xr.dz[i] << " " ;
    //std::cout << out->xr.r[i] << " " << out->xr.g[i] << " " << out->xr.b[i] << " " ;
    //std::cout << out->xr.o[i] << " " << out->xr.t[i] << " " << out->xr.tMax[i] << " " ;
    //std::cout << out->xr.x[i] << " " << out->xr.y[i] << " " << out->xr.type[i] << std::endl;
  }
  return out;
}

void OSPRayAdapter::trace(gvt::render::actor::RayVector &rayList, gvt::render::actor::RayVector &moved_rays, glm::mat4 *m, glm::mat4 *minv, glm::mat3 *normi, std::vector<gvt::render::data::scene::Light *> &lights, size_t begin ,size_t end) { 
  // lights
  // todo sort point and area lights. For now assume point light. 
  // gravit stores light position and color. ospray uses direction instead. 
  // need to  derive direction from position. Assume all point lights 
  // have direction pointing to origin. Also scale to unit vector.
  float* lghts = new float[3*lights.size()];
  float* lghtptr = lghts;
  gvt::render::data::scene::Light lgt;
  for(gvt::render::data::scene::Light *lgt : lights) {
    glm::vec3 pos = lgt->position;
    float d = 1/sqrt(pos[0]*pos[0] + pos[1]*pos[1] + pos[2]*pos[2]);
    lghtptr[0] = -pos[0]*d;
    lghtptr[1] = -pos[1]*d;
    lghtptr[2] = -pos[2]*d;
    lghtptr +=3;
  }
  OSPData lightData = ospNewData(lights.size(),OSP_FLOAT3,lghts);
  ospSetData(theOSPRenderer,"lights",lightData);
  float ka,kd,arad;
  int nar;
  bool dos;
  ka = 0.6;
  kd = 0.4;
  dos = false;
  arad = 0.0;
  nar = 0;
  ospSet1i(theOSPRenderer,"do_shadows",0);
  ospSet1i(theOSPRenderer,"n_ao_rays",nar);
  ospSet1f(theOSPRenderer,"ao_radius",arad);
  ospSet1f(theOSPRenderer,"Ka",ka);
  ospSet1f(theOSPRenderer,"Kd",kd);
  ospCommit(theOSPRenderer);
  std::cout << " tracin " << rayList.size() << " rays" << std::endl; 
  // convert GVT RayVector into the OSPExternalRays used by ospray. 
  OSPExternalRays rl = GVT2OSPRays(rayList);
  // trace'em 
  OSPExternalRays out = ospTraceRays(theOSPRenderer,rl); // ospray trace
  // push everything from out and rl into moved_rays for sorting into houses
  // YA Griffindor. 
  OSP2GVTMoved_Rays(out,rl,moved_rays);
  std::cout << " adapter rl size " << rl->GetCount() << " moved rays size " << moved_rays.size() << std::endl;
  // out and rl are no longer needed since they have been copied into moved_rays so 
  // whack 'em. 
  delete out;
  delete rl;
}
void OSPRayAdapter::initospray(int * argc, char**argv) {
  if (!OSPRayAdapter::init) {
    ospInit(argc,(const char**)argv);
    OSPRayAdapter::init = true;
  }
}
OSPRayAdapter::~OSPRayAdapter() {
  ospRelease(theOSPRenderer);
}

