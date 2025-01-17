/* =======================================================================================
   This file is released as part of GraviT - scalable, platform independent ray tracing
   tacc.github.io/GraviT

   Copyright 2013-2015 Texas Advanced Computing Center, The University of Texas at Austin
   All rights reserved.

   Licensed under the BSD 3-Clause License, (the "License"); you may not use this file
   except in compliance with the License.
   A copy of the License is included with this software in the file LICENSE.
   If your copy does not contain the License, you may obtain a copy of the License at:

       http://opensource.org/licenses/BSD-3-Clause

   Unless required by applicable law or agreed to in writing, software distributed under
   the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.
   See the License for the specific language governing permissions and limitations under
   limitations under the License.

   GraviT is funded in part by the US National Science Foundation under awards ACI-1339863,
   ACI-1339881 and ACI-1339840
   ======================================================================================= */

#include <cassert>
#include <gvt/render/cntx/rcontext.h>
#include <gvt/render/tracer/RayTracer.h>


namespace gvt {
namespace render {

RayTracer::RayTracer(const std::string &name, std::shared_ptr<gvt::render::data::scene::gvtCameraBase> cam,
                     std::shared_ptr<gvt::render::composite::ImageComposite> img)
    : cam(cam), img(img) {
  auto &db = cntx::rcontext::instance();
  adapterType = db.getChild(db.getUnique(name), "adapter");
  std::string filmname = db.getChild(db.getUnique(name), "film");
  width = db.getChild(db.getUnique(filmname), "width");
  height = db.getChild(db.getUnique(filmname), "height");
  resetBVH();
};

RayTracer::~RayTracer(){};

void RayTracer::operator()() {
  cam->AllocateCameraRays();
  cam->generateRays();
  gvt::render::actor::RayVector moved_rays;
  for (auto d : meshRef) {
    std::cout << "Processing " << d.first << std::endl;
    calladapter(d.first, cam->rays, moved_rays);
  }
  processRays(moved_rays);
  img->composite();
};
bool RayTracer::MessageManager(std::shared_ptr<gvt::comm::Message> msg) { return true; };
bool RayTracer::isDone() { return false; };
bool RayTracer::hasWork() { return true; };

void RayTracer::processRays(gvt::render::actor::RayVector &rays, const int src, const int dst) {
  for (gvt::render::actor::Ray &r : rays) {
    img->localAdd(r.mice.id, r.mice.color * r.mice.w, 1.0, r.mice.t);
  }
}

void RayTracer::calladapter(const int instTarget, gvt::render::actor::RayVector &toprocess,
                            gvt::render::actor::RayVector &moved_rays) {
  std::shared_ptr<gvt::render::Adapter> adapter;

  std::shared_ptr<gvt::render::data::primitives::Data> mesh = meshRef[instTarget];

  auto it = adapterCache.find(mesh.get());

  if (it != adapterCache.end()) {
    adapter = it->second;
  } else {
    adapter = 0;
  }

  if (!adapter) {
    switch (adapterType) {
#ifdef GVT_RENDER_ADAPTER_EMBREE
    case gvt::render::adapter::Embree:
      adapter = std::make_shared<gvt::render::adapter::embree::data::EmbreeMeshAdapter>(mesh);
      break;
#endif
#ifdef GVT_RENDER_ADAPTER_EMBREE_STREAM
    case gvt::render::adapter::EmbreeStream:
      adapter = std::make_shared<gvt::render::adapter::embree::data::EmbreeStreamMeshAdapter>(mesh);
      break;
#endif
#ifdef GVT_RENDER_ADAPTER_OSPRAY
    case gvt::render::adapter::Ospray:
      adapter = std::make_shared<gvt::render::adapter::ospray::data::OSPRayAdapter>(mesh, width, height);
      break;
#endif
#ifdef GVT_RENDER_ADAPTER_GALAXY
    case gvt::render::adapter::Pvol:
      adapter = std::make_shared<gvt::render::adapter::galaxy::data::PVolAdapter>(mesh, width, height);
      break;
#endif
#ifdef GVT_RENDER_ADAPTER_MANTA
    case gvt::render::adapter::Manta:
      adapter = new gvt::render::adapter::manta::data::MantaMeshAdapter(mesh);
      break;
#endif
#ifdef GVT_RENDER_ADAPTER_OPTIX
    case gvt::render::adapter::Optix:
      adapter = new gvt::render::adapter::optix::data::OptixMeshAdapter(mesh);
      break;
#endif

#if defined(GVT_RENDER_ADAPTER_OPTIX) && defined(GVT_RENDER_ADAPTER_EMBREE)
    case gvt::render::adapter::Heterogeneous:
      adapter = new gvt::render::adapter::heterogeneous::data::HeterogeneousMeshAdapter(mesh);
      break;
#endif
    default:
      GVT_ERR_MESSAGE("Image scheduler: unknown adapter type: " << adapterType);
    }
    adapterCache[mesh.get()] = adapter;
  }
  GVT_ASSERT(adapter != nullptr, "image scheduler: adapter not set");
  {
    moved_rays.reserve(toprocess.size() * 10);
    adapter->trace(toprocess, moved_rays, instM[instTarget].get(), instMinv[instTarget].get(),
                   instMinvN[instTarget].get(), lights);
    toprocess.clear();
  }
}

float *RayTracer::getImageBuffer() { return img->composite(); };

void RayTracer::resetCamera() {
  //  assert(cntxt != nullptr);
  //  cam = std::make_shared<gvt::render::data::scene::gvtPerspectiveCamera>();
  //
  //
  //  gvt::core::DBNodeH root = cntxt->getRootNode();
  //  gvt::core::DBNodeH camNode = root["Camera"];
  //  gvt::core::DBNodeH filmNode = root["Film"];
  //  glm::vec3 cameraposition = camNode["eyePoint"].value().tovec3();
  //  glm::vec3 focus = camNode["focus"].value().tovec3();
  //  float fov = camNode["fov"].value().toFloat();
  //  glm::vec3 up = camNode["upVector"].value().tovec3();
  //  int rayMaxDepth = camNode["rayMaxDepth"].value().toInteger();
  //  int raySamples = camNode["raySamples"].value().toInteger();
  //
  //  auto& db = cntx::rcontext::instance();
  //
  //
  //  glm::vec3 cameraposition = camNode["eyePoint"].value().tovec3();
  //  glm::vec3 focus = camNode["focus"].value().tovec3();
  //  float fov = camNode["fov"].value().toFloat();
  //  glm::vec3 up = camNode["upVector"].value().tovec3();
  //  int rayMaxDepth = camNode["rayMaxDepth"].value().toInteger();
  //  int raySamples = camNode["raySamples"].value().toInteger();
  //
  //
  //  cam->lookAt(cameraposition, focus, up);
  //  cam->setMaxDepth(rayMaxDepth);
  //  cam->setSamples(raySamples);
  //  cam->setFOV(fov);
  //  cam->setFilmsize(filmNode["width"].value().toInteger(), filmNode["height"].value().toInteger());
}

void RayTracer::resetFilm() {
  //  assert(cntxt != nullptr);
  //  img = std::make_shared<gvt::render::composite::IceTComposite>();
  //  gvt::core::DBNodeH root = cntxt->getRootNode();
  //  gvt::core::DBNodeH filmNode = root["Film"];
  //  img = std::make_shared<gvt::render::composite::IceTComposite>(filmNode["width"].value().toInteger(),
  //                                                                filmNode["height"].value().toInteger());
}

void RayTracer::resetBVH() {
  auto &db = cntx::rcontext::instance();

  cntx::rcontext::children_vector instancenodes = db.getChildren(db.getUnique("Instances"));
  int numInst = instancenodes.size();
  meshRef.clear();
  instM.clear();
  instMinv.clear();
  instMinvN.clear();
  lights.clear();

  bvh = std::make_shared<gvt::render::data::accel::BVH>(instancenodes);

  queue_mutex = new std::mutex[bvh->instanceSet.size()];

  for (auto &nref : bvh->instanceSet) {
    auto &n = nref.get();
    size_t id = db.getChild(n, "id");
    //meshRef[id] = db.getChild(db.deRef(db.getChild(n, "meshRef")), "ptr");
    if (db.getChild(db.deRef(db.getChild(n, "meshRef")), "ptr")
        .v.is<std::shared_ptr<gvt::render::data::primitives::Mesh> >()) {
      meshRef[id] = db.getChild(db.deRef(db.getChild(n, "meshRef")), "ptr")
          .to<std::shared_ptr<gvt::render::data::primitives::Mesh> >();
    }
#ifdef GVT_BUILD_VOLUME  
    else if (db.getChild(db.deRef(db.getChild(n, "meshRef")), "ptr")
        .v.is<std::shared_ptr<gvt::render::data::primitives::Volume> >()) {
      meshRef[id] = db.getChild(db.deRef(db.getChild(n, "meshRef")), "ptr")
          .to<std::shared_ptr<gvt::render::data::primitives::Volume> >();
    } 
#endif // GVT_BUILD_VOLUME
    else {
      meshRef[id] = nullptr;
    }
    instM[id] = db.getChild(n, "mat");
    instMinv[id] = db.getChild(n, "matinv");
    instMinvN[id] = db.getChild(n, "normi");
  }

  auto lightNodes = db.getChildren(db.getUnique("Lights"));

  lights.reserve(lightNodes.size());
  for (auto lightNode : lightNodes) {
    auto &light = lightNode.get();
    glm::vec3 color = db.getChild(light, "color");
    std::string type = db.getChild(light, "type");
    if (type == std::string("PointLight")) {
      glm::vec3 pos = db.getChild(light, "position");
      lights.push_back(std::make_shared<gvt::render::data::scene::PointLight>(pos, color));
    } else if (type == std::string("AmbientLight")) {
      lights.push_back(std::make_shared<gvt::render::data::scene::AmbientLight>(color));
    } else if (type == std::string("AreaLight")) {
      glm::vec3 pos = db.getChild(light, "position");
      glm::vec3 normal = db.getChild(light, "normal");
      auto width = db.getChild(light, "width");
      auto height = db.getChild(light, "height");
      lights.push_back(std::make_shared<gvt::render::data::scene::AreaLight>(pos, color, normal, width, height));
    }
  }
}
} // namespace render
} // namespace gvt
