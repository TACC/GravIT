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

#include "DomainTracer.h"
#include "Messages/SendRayList.h"
#include <gvt/core/comm/communicator.h>
#include <gvt/core/utils/global_counter.h>
#include <gvt/core/utils/timer.h>
#include <gvt/render/cntx/rcontext.h>

namespace gvt {
namespace render {

bool DomainTracer::areWeDone() {
  std::shared_ptr<gvt::comm::communicator> comm = gvt::comm::communicator::singleton();
  std::shared_ptr<DomainTracer> tracer = std::dynamic_pointer_cast<DomainTracer>(cntx::rcontext::instance().tracer);
  if (!tracer || tracer->getGlobalFrameFinished()) return false;
  bool ret = tracer->isDone();
  return ret;
}

void DomainTracer::Done(bool T) {
  std::shared_ptr<gvt::comm::communicator> comm = gvt::comm::communicator::singleton();
  //  gvt::render::RenderContext &cntxt = *gvt::render::RenderContext::instance();
  std::shared_ptr<DomainTracer> tracer = std::dynamic_pointer_cast<DomainTracer>(cntx::rcontext::instance().tracer);
  if (!tracer) return;
  if (T) {
    tracer->setGlobalFrameFinished(true);
  }
}

DomainTracer::DomainTracer(const std::string &name, std::shared_ptr<gvt::render::data::scene::gvtCameraBase> cam,
                           std::shared_ptr<gvt::render::composite::ImageComposite> img)
    : gvt::render::RayTracer(name, cam, img) {
  RegisterMessage<gvt::comm::EmptyMessage>();
  RegisterMessage<gvt::comm::SendRayList>();
  gvt::comm::communicator &comm = gvt::comm::communicator::instance();
  v = std::make_shared<comm::vote::vote>(DomainTracer::areWeDone, DomainTracer::Done);
  comm.setVote(v);

  auto &db = cntx::rcontext::instance();

  queue_mutex = new std::mutex[meshRef.size()];
  for (auto &m : meshRef) {
    queue[m.first] = gvt::render::actor::RayVector();
  }

  instances_in_node.clear();

  auto inst = db.getChildren(db.getUnique("Instances"));
  auto data = db.getChildren(db.getUnique("Data"));
  gvt::core::Map<cntx::identifier, unsigned> lastAssigned;
  for (auto &rn : data) {
    auto &m = rn.get();
    lastAssigned[rn.get().getid()] = 0;
  }

  unsigned icount = 0;

  for (auto &ri : inst) {
    auto &i = ri.get();
    auto &m = db.deRef(db.getChild(i, "meshRef"));
    size_t id = db.getChild(i, "id");
    std::vector<int> &loc = *(db.getChild(m, "Locations").to<std::shared_ptr<std::vector<int> > >().get());
    remote[id] = loc[lastAssigned[m.getid()] % loc.size()];
    instances_in_node[id] = (remote[id] == db.cntx_comm.rank);
    lastAssigned[m.getid()]++;
  }
}

DomainTracer::~DomainTracer() {
  if (queue_mutex != nullptr) delete[] queue_mutex;
  queue.clear();
}

void DomainTracer::resetBVH() {
  RayTracer::resetBVH();
  //  if (queue_mutex != nullptr) delete[] queue_mutex;

  for (auto &m : meshRef) {
    queue[m.first] = gvt::render::actor::RayVector();
    queue[m.first].reserve(8192);
  }
}

void DomainTracer::operator()() {
  gvt::comm::communicator &comm = gvt::comm::communicator::instance();
  _GlobalFrameFinished = false;

  gvt::core::time::timer t_frame(true, "domain tracer: frame :");
  gvt::core::time::timer t_all(false, "domain tracer: all timers :");
  gvt::core::time::timer t_gather(false, "domain tracer: gather :");
  gvt::core::time::timer t_send(false, "domain tracer: send :");
  gvt::core::time::timer t_shuffle(false, "domain tracer: shuffle :");
  gvt::core::time::timer t_tracer(false, "domain tracer: adapter+tracer :");
  gvt::core::time::timer t_select(false, "domain tracer: select :");
  gvt::core::time::timer t_filter(false, "domain tracer: filter :");
  gvt::core::time::timer t_camera(false, "domain tracer: gen rays :");

  gvt::util::global_counter gc_rays("Number of rays traced :");
  gvt::util::global_counter gc_filter("Number of rays filtered :");
  gvt::util::global_counter gc_shuffle("Number of rays shuffled :");
  gvt::util::global_counter gc_sent("Number of rays sent :");

  t_filter.resume();
  gc_filter.add(cam->rays.size());
  processRaysAndDrop(cam->rays);
  t_filter.stop();
  gvt::render::actor::RayVector returned_rays;

  do {
    int target = -1;
    int amount = 0;
    t_select.resume();
    for (auto &q : queue) {
      if (isInNode(q.first) && q.second.size() > amount) {
        amount = q.second.size();
        target = q.first;
      }
    }
    t_select.stop();

    if (target != -1) {
      gvt::render::actor::RayVector tmp;

      queue_mutex[target].lock();
      t_tracer.resume();
      gc_rays.add(queue[target].size());
      std::swap(queue[target], tmp);
      queue[target].reserve(4096);
      queue_mutex[target].unlock();
      RayTracer::calladapter(target, tmp, returned_rays);
      t_tracer.stop();

      t_shuffle.resume();
      gc_shuffle.add(returned_rays.size());
      processRays(returned_rays, target);
      t_shuffle.stop();
    } else {
      t_send.resume();
      for (auto &q : queue) {
        if (isInNode(q.first) || q.second.empty()) continue;
        queue_mutex[q.first].lock();
        gc_sent.add(q.second.size());
        int sendto = pickNode(q.first);
        std::shared_ptr<gvt::comm::Message> msg = std::make_shared<gvt::comm::SendRayList>(comm.id(), sendto, q.second);
        comm.send(msg, sendto);

        q.second.clear();
        queue_mutex[q.first].unlock();
      }
      t_send.stop();
    }

    if (isDone()) {
      v->PorposeVoting();
    }

  } while (hasWork());
  t_gather.resume();
  img->composite();
  t_gather.stop();
  t_frame.stop();
  t_all = t_gather + t_send + t_shuffle + t_tracer + t_filter + t_select;
  gc_filter.print();
  gc_shuffle.print();
  gc_rays.print();
  gc_sent.print();
}

inline void DomainTracer::processRaysAndDrop(gvt::render::actor::RayVector &rays) {
  auto &db = cntx::rcontext::instance();
  gvt::comm::communicator &comm = gvt::comm::communicator::instance();
  const int chunksize = MAX(4096, rays.size() / (db.getUnique("threads").to<unsigned>() * 4));
  gvt::render::data::accel::BVH &acc = *bvh.get();
  static tbb::auto_partitioner ap;
  tbb::parallel_for(tbb::blocked_range<gvt::render::actor::RayVector::iterator>(rays.begin(), rays.end(), chunksize),
                    [&](tbb::blocked_range<gvt::render::actor::RayVector::iterator> raysit) {

                      gvt::core::Vector<gvt::render::data::accel::BVH::hit> hits =
                          acc.intersect<GVT_SIMD_WIDTH>(raysit.begin(), raysit.end(), -1);

                      gvt::core::Map<int, gvt::render::actor::RayVector> local_queue;
                      for (size_t i = 0; i < hits.size(); i++) {
                        gvt::render::actor::Ray &r = *(raysit.begin() + i);
                        if (hits[i].next != -1)
                          if (instances_in_node[hits[i].next]) local_queue[hits[i].next].push_back(r);
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

inline void DomainTracer::processRays(gvt::render::actor::RayVector &rays, const int src, const int dst) {

  auto &db = cntx::rcontext::instance();

  const int chunksize = MAX(4096, rays.size() / (db.getUnique("threads").to<unsigned>() * 4));
  gvt::render::data::accel::BVH &acc = *bvh.get();
  static tbb::auto_partitioner ap;
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

bool DomainTracer::MessageManager(std::shared_ptr<gvt::comm::Message> msg) {
  std::shared_ptr<gvt::comm::communicator> comm = gvt::comm::communicator::singleton();
  gvt::render::actor::RayVector rays;
  rays.resize(msg->size() / sizeof(gvt::render::actor::Ray));
  std::memcpy(&rays[0], msg->getMessage<void>(), msg->size());
  processRays(rays);
  return true;
}

bool DomainTracer::isDone() {
  if (queue.empty()) return true;
  for (auto &q : queue)
    if (!q.second.empty()) return false;
  return true;
}
bool DomainTracer::hasWork() { return !_GlobalFrameFinished; }
} // namespace render
} // namespace gvt
