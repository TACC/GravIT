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

#include <algorithm>

#include "ImageTracer.h"
#include <gvt/core/comm/communicator.h>
#include <gvt/core/utils/timer.h>
namespace gvt {
namespace render {
ImageTracer::ImageTracer(const std::string &name, std::shared_ptr<gvt::render::data::scene::gvtCameraBase> cam,
                         std::shared_ptr<gvt::render::composite::ImageComposite> img)
    : gvt::render::RayTracer(name, cam, img) {
  queue_mutex = new std::mutex[meshRef.size()];
  for (auto &m : meshRef) {
    queue[m.first] = gvt::render::actor::RayVector();
  }
}
ImageTracer::~ImageTracer() {
  if (queue_mutex != nullptr) delete[] queue_mutex;
  queue.clear();
}

void ImageTracer::resetBVH() {
  RayTracer::resetBVH();
  if (queue_mutex != nullptr) delete[] queue_mutex;
  for (auto &m : meshRef) {
    queue[m.first] = gvt::render::actor::RayVector();
  }
}

void ImageTracer::operator()() {

  img->reset();

  gvt::core::time::timer t_frame(true, "image tracer: frame: ");
  gvt::core::time::timer t_all(false, "image tracer: all timers: ");
  gvt::core::time::timer t_gather(false, "image tracer: gather: ");
  gvt::core::time::timer t_shuffle(false, "image tracer: shuffle: ");
  gvt::core::time::timer t_tracer(false, "image tracer: adapter+trace : ");
  gvt::core::time::timer t_select(false, "image tracer: select : ");
  gvt::core::time::timer t_filter(false, "image tracer: filter : ");
  gvt::core::time::timer t_camera(false, "image tracer: gen rays : ");
  t_camera.resume();
  cam->AllocateCameraRays();
  cam->generateRays();
  t_camera.stop();

  std::cout << "Generated rays" << std::endl;

  t_filter.resume();
  processRaysAndDrop(cam->rays);
  t_filter.stop();
  gvt::render::actor::RayVector returned_rays;

  std::cout << "Procesed rays" << std::endl;

  do {
    int target = -1;
    int amount = 0;
    t_select.resume();
    for (auto &q : queue) {
      if (q.second.size() > amount) {
        amount = q.second.size();
        target = q.first;
      }
    }
    t_select.stop();
    if (target != -1) {
      t_tracer.resume();
      returned_rays.reserve(queue[target].size() * 10);
      RayTracer::calladapter(target, queue[target], returned_rays);
      queue[target].clear();
      t_tracer.stop();
      t_shuffle.resume();
      processRays(returned_rays, target);
      t_shuffle.stop();
    }
  } while (hasWork());
  t_gather.resume();
  img->composite();
  t_gather.stop();
  t_all = t_gather + t_shuffle + t_tracer + t_select + t_filter;
}

void ImageTracer::processRaysAndDrop(gvt::render::actor::RayVector &rays) {

  std::cout << "Yeap" << std::endl;

  gvt::comm::communicator &comm = gvt::comm::communicator::instance();

  const unsigned ray_chunk = rays.size() / comm.lastid();
  const unsigned ray_start = ray_chunk * comm.id();
  const unsigned ray_end = ray_chunk * (comm.id() + 1);

  const int chunksize =
      MAX(GVT_SIMD_WIDTH, ray_chunk / (cntx::rcontext::instance().getUnique("threads").to<unsigned>() * 4));
  gvt::render::data::accel::BVH &acc = *bvh.get();

  static tbb::simple_partitioner ap;
  tbb::parallel_for(tbb::blocked_range<gvt::render::actor::RayVector::iterator>(rays.begin() + ray_start,
                                                                                rays.begin() + ray_end, chunksize),
                    [&](tbb::blocked_range<gvt::render::actor::RayVector::iterator> raysit) {

                      gvt::core::Vector<gvt::render::data::accel::BVH::hit> hits =
                          acc.intersect<GVT_SIMD_WIDTH>(raysit.begin(), raysit.end(), -1);

                      gvt::core::Map<int, gvt::render::actor::RayVector> local_queue;
                      for (size_t i = 0; i < hits.size(); i++) {
                        gvt::render::actor::Ray &r = *(raysit.begin() + i);
                        if (hits[i].next != -1) {
                          r.mice.origin = r.mice.origin + r.mice.direction * (hits[i].t * 0.95f);
                          local_queue[hits[i].next].push_back(r);
                        }
                      }
                      for (auto &q : local_queue) {
                        queue_mutex[q.first].lock();
                        queue[q.first].insert(queue[q.first].end(),
                                              std::make_move_iterator(local_queue[q.first].begin()),
                                              std::make_move_iterator(local_queue[q.first].end()));
                        queue_mutex[q.first].unlock();
                      }
                    },
                    ap);

  rays.clear();
}

void ImageTracer::processRays(gvt::render::actor::RayVector &rays, const int src, const int dst) {

  const int chunksize = MAX(4096, rays.size() / (cntx::rcontext::instance().getUnique("threads").to<unsigned>() * 4));
  gvt::render::data::accel::BVH &acc = *bvh.get();
  static tbb::simple_partitioner ap;
  tbb::parallel_for(
      tbb::blocked_range<gvt::render::actor::RayVector::iterator>(rays.begin(), rays.end(), chunksize),
      [&](tbb::blocked_range<gvt::render::actor::RayVector::iterator> raysit) {

        gvt::core::Vector<gvt::render::data::accel::BVH::hit> hits =
            acc.intersect<GVT_SIMD_WIDTH>(raysit.begin(), raysit.end(), src);

        gvt::core::Map<int, gvt::render::actor::RayVector> local_queue;
        for (size_t i = 0; i < hits.size(); i++) {
          gvt::render::actor::Ray &r = *(raysit.begin() + i);
          //                        if (hits[i].next != -1) {
          //                          r.origin = r.origin + r.direction * (hits[i].t * 0.95f);
          //                          local_queue[hits[i].next].push_back(r);
          //                        } else if (r.type == gvt::render::actor::Ray::SHADOW && glm::length(r.color) > 0) {
          //                          img->localAdd(r.id, r.color * r.w, 1.f, r.t);
          //                        }
          bool write_to_fb = false;
          int target_queue = -1;
          //#ifdef GVT_RENDER_ADAPTER_OSPRAY
          if ( adapterType == gvt::render::adapter::Pvol 
            || adapterType == gvt::render::adapter::Ospray ) {
            // std::cout << "initially ray " << r.id << " r.depth " << std::bitset<8>(r.depth)<< " r.type " <<
            // std::bitset<8>(r.type) << " r.color " << r.color <<std::endl;
            if (r.mice.depth & RAY_BOUNDARY) {
              // check to see if this ray hit anything in bvh
              if (hits[i].next != -1) {
                r.mice.depth &= ~RAY_BOUNDARY;
                r.mice.origin = r.mice.origin + r.mice.direction * (hits[i].t * (1.0f + std::numeric_limits<float>::epsilon()));
                target_queue = hits[i].next;
                // local_queue[hits[i].next].push_back(r);
              } else {
                r.mice.depth &= ~RAY_BOUNDARY;
                r.mice.depth |= RAY_EXTERNAL_BOUNDARY;
                target_queue = -1;
              }
            }
            // std::cout << "after boundary test ray " << r.id << " r.depth " << std::bitset<8>(r.depth)<< " r.type "
            // << std::bitset<8>(r.type) << std::endl; check types
            if (r.mice.type == RAY_PRIMARY) {
              if ((r.mice.depth & RAY_OPAQUE) | (r.mice.depth & RAY_EXTERNAL_BOUNDARY)) {
                write_to_fb = true;
                target_queue = -1;
              } else if (r.mice.depth & ~RAY_BOUNDARY) {
                target_queue = src;
              }
            } else if (r.mice.type == RAY_SHADOW) {
              if (r.mice.depth & RAY_EXTERNAL_BOUNDARY) {
                //                              tbb::mutex::scoped_lock fbloc(colorBuf_mutex[r.id % width]);
                // colorBuf[r.id] += glm::vec4(r.color, r.w);
                img->localAdd(r.mice.id, r.mice.color * r.mice.w, 1.f, r.mice.t);
              } else if (r.mice.depth & RAY_BOUNDARY) {
                r.mice.origin = r.mice.origin + r.mice.direction * (hits[i].t * 1.00f);
                local_queue[hits[i].next].push_back(r);
              }
            } else if (r.mice.type == RAY_AO) {
              if (r.mice.depth & (RAY_EXTERNAL_BOUNDARY | RAY_TIMEOUT)) {
                //                              tbb::mutex::scoped_lock fbloc(colorBuf_mutex[r.id % width]);
                //                colorBuf[r.id] += glm::vec4(r.color, r.w);
                img->localAdd(r.mice.id, r.mice.color * r.mice.w, 1.f, r.mice.t);
              } else if (r.mice.depth & RAY_BOUNDARY) {
                r.mice.origin = r.mice.origin + r.mice.direction * (hits[i].t * 1.00f);
                local_queue[hits[i].next].push_back(r);
              }
            }
            if (write_to_fb) {
              //                            tbb::mutex::scoped_lock fbloc(colorBuf_mutex[r.id % width]);
              // std::cout << "TB: writing colorBuf["<<r.id<<"] "<< r.color << std::endl;
              // colorBuf[r.id] += glm::vec4(r.color, r.w);
              img->localAdd(r.mice.id, r.mice.color * r.mice.w, 1.f, r.mice.t);
            }
            if (target_queue != -1) {
              local_queue[target_queue].push_back(r);
            }
          } else {
            if (hits[i].next != -1) {
              r.mice.origin = r.mice.origin + r.mice.direction * (hits[i].t * 0.95f);
              local_queue[hits[i].next].push_back(r);
            } else if (r.mice.type == gvt::render::actor::Ray::SHADOW && glm::length(r.mice.color) > 0) {
              //                            tbb::mutex::scoped_lock fbloc(colorBuf_mutex[r.id % width]);
              // colorBuf[r.id] += glm::vec4(r.color, r.w);
              img->localAdd(r.mice.id, r.mice.color * r.mice.w, 1.f, r.mice.t);
            }
          }
        }
        for (auto &q : local_queue) {
          queue_mutex[q.first].lock();
          queue[q.first].insert(queue[q.first].end(), std::make_move_iterator(local_queue[q.first].begin()),
                                std::make_move_iterator(local_queue[q.first].end()));
          queue_mutex[q.first].unlock();
        }
      },
      ap);

  rays.clear();
}

bool ImageTracer::MessageManager(std::shared_ptr<gvt::comm::Message> msg) { return RayTracer::MessageManager(msg); }

bool ImageTracer::isDone() {
  if (queue.empty()) return true;
  for (auto &q : queue)
    if (!q.second.empty()) return false;
  return true;
}
bool ImageTracer::hasWork() { return !isDone(); }
} // namespace render
} // namespace gvt
