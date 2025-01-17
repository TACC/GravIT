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
/*
 * DomainTracer.h
 *
 *  Created on: Dec 8, 2013
 *      Author: jbarbosa
 */

#ifndef GVT_RENDER_ALGORITHM_DOMAIN_TRACER_H
#define GVT_RENDER_ALGORITHM_DOMAIN_TRACER_H

#include <gvt/render/Schedulers.h>
#include <gvt/render/Types.h>
#include <gvt/render/algorithm/TracerBase.h>
#include <iterator>

#ifdef GVT_RENDER_ADAPTER_EMBREE
#include <gvt/render/adapter/embree/EmbreeMeshAdapter.h>
#endif

#ifdef GVT_RENDER_ADAPTER_EMBREE_STREAM
#include <gvt/render/adapter/embree/EmbreeStreamMeshAdapter.h>
#endif

#ifdef GVT_RENDER_ADAPTER_MANTA
#include <gvt/render/adapter/manta/MantaMeshAdapter.h>
#endif
#ifdef GVT_RENDER_ADAPTER_OPTIX
#include <gvt/render/adapter/optix/OptixMeshAdapter.h>
#endif
#if defined(GVT_RENDER_ADAPTER_OPTIX) && defined(GVT_RENDER_ADAPTER_EMBREE)
#include <gvt/render/adapter/heterogeneous/HeterogeneousMeshAdapter.h>
#endif
#ifdef GVT_RENDER_ADAPTER_OSPRAY
#include <gvt/render/adapter/ospray/OSPRayAdapter.h>
#endif
#ifdef GVT_RENDER_ADAPTER_GALAXY
#include <gvt/render/adapter/galaxy/PVolAdapter.h>
#endif


#include <gvt/core/utils/global_counter.h>

#include <set>

#define RAY_BUF_SIZE 10485760 // 10 MB per neighbor

namespace gvt {
namespace render {
namespace algorithm {

/// work scheduler that strives to keep domains loaded and send rays
/**
  The Domain scheduler strives to schedule work such that loaded domains remain loaded
  and rays are sent to the process that contains the loaded domain (or is responsible
  for loading the domain). A domain is loaded in at most one process at any time. If
  there are sufficent processes to load all domains, the entire render will proceed
  in-core.

  This scheduler can become unbalanced when:
   - there are more processes than domains, excess processes will remain idle
   - rays are concentrated at a few domains, processes with other domains loaded
  can remain idle
   - when there are few rays remaining to render, other processes can remain
  idle

     \sa HybridTracer, ImageTracer
   */
template <> class Tracer<gvt::render::schedule::DomainScheduler> : public AbstractTrace {
public:
  std::set<int> neighbors;

  size_t rays_start, rays_end;

  gvt::core::Map<int, int> mpiInstanceMap;

  Tracer(std::shared_ptr<gvt::render::data::scene::gvtCameraBase> camera,
         std::shared_ptr<gvt::render::composite::ImageComposite> image, std::string const &camname = "Camera",
         std::string const &filmname = "Film", std::string const &schedulername = "Scheduler")
      : AbstractTrace(camera, image, camname, filmname, schedulername) {

          //std::cerr << "initialize domain tracer " << std::endl;
    Initialize();
  }

  void resetInstances() {
    AbstractTrace::resetInstances();
    adapterCache.clear();
    mpiInstanceMap.clear();
    Initialize();
  }

  virtual void Initialize() {
      //std::cerr << "get inst" << std::endl;
    auto inst = db.getChildren(db.getUnique("Instances"));
      //std::cerr << "get data" << std::endl;
    auto data = db.getChildren(db.getUnique("Data"));
      //std::cerr << "get lastAssigned" << std::endl;
    gvt::core::Map<cntx::identifier, unsigned> lastAssigned;
    for (auto &rn : data) {
      auto &m = rn.get();
      lastAssigned[rn.get().getid()] = 0;
    }

    unsigned icount = 0;

    //std::cerr << "DomainTracer: loop on inst" << std::endl;
    for (auto &ri : inst) {
      //std::cerr << " get ri " << std::endl;
      auto &i = ri.get();
      //std::cerr << " get m " << std::endl;
      auto &m = db.deRef(db.getChild(i, "meshRef"));
      //std::cerr << " get id " << std::endl;
      size_t id = db.getChild(i, "id");
      //std::cerr << " get loc " << std::endl;
      std::vector<int> &loc = *(db.getChild(m, "Locations").to<std::shared_ptr<std::vector<int> > >().get());
      //std::cerr << " map instances " << std::endl;
      mpiInstanceMap[id] = loc[lastAssigned[m.getid()] % loc.size()];
      lastAssigned[m.getid()]++;
    }
    //std::cerr << " done looping on inst " << std::endl;
  }

  virtual ~Tracer() {}

  void shuffleDropRays(gvt::render::actor::RayVector &rays) {

    const size_t chunksize = MAX(4096, rays.size() / (db.getUnique("threads").to<unsigned>() * 4));

    static gvt::render::data::accel::BVH &acc = *dynamic_cast<gvt::render::data::accel::BVH *>(acceleration.get());
    static tbb::simple_partitioner ap;

    tbb::parallel_for(tbb::blocked_range<gvt::render::actor::RayVector::iterator>(rays.begin(), rays.end(), chunksize),
                      [&](tbb::blocked_range<gvt::render::actor::RayVector::iterator> raysit) {

       gvt::core::Vector<gvt::render::data::accel::BVH::hit> hits =
          acc.intersect<GVT_SIMD_WIDTH>(raysit.begin(), raysit.end(), -1);
       gvt::core::Map<int, gvt::render::actor::RayVector> local_queue;
       for (size_t i = 0; i < hits.size(); i++) {
          gvt::render::actor::Ray &r = *(raysit.begin() + i);
          if (hits[i].next != -1) {
            r.mice.origin = r.mice.origin + r.mice.direction * (hits[i].t * 0.95f);
            const bool inRank = mpiInstanceMap[hits[i].next] == mpi.rank;
            if (inRank) local_queue[hits[i].next].push_back(r);
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

  inline void FilterRaysLocally() { shuffleDropRays(rays); }

  inline void operator()() {

    gvt::core::time::timer t_diff(false, "domain tracer: diff timers/frame:");
    gvt::core::time::timer t_all(false, "domain tracer: all timers:");
    gvt::core::time::timer t_frame(true, "domain tracer: frame :");
    gvt::core::time::timer t_gather(false, "domain tracer: gather :");
    gvt::core::time::timer t_send(false, "domain tracer: send :");
    gvt::core::time::timer t_shuffle(false, "domain tracer: shuffle :");
    gvt::core::time::timer t_trace(false, "domain tracer: trace :");
    gvt::core::time::timer t_sort(false, "domain tracer: select :");
    gvt::core::time::timer t_adapter(false, "domain tracer: adapter :");
    gvt::core::time::timer t_filter(false, "domain tracer: filter :");

    gvt::util::global_counter gc_rays("Number of rays traced :");
    gvt::util::global_counter gc_filter("Number of rays filtered :");
    gvt::util::global_counter gc_shuffle("Number of rays shuffled :");
    gvt::util::global_counter gc_sent("Number of rays sent :");

    clearBuffer();
    //int adapterType = db.getChild(db.getUnique(schedulername), "adapter");
    adapterType = db.getChild(db.getUnique(schedulername), "adapter");

    t_filter.resume();
    gc_filter.add(rays.size());
    FilterRaysLocally();
    t_filter.stop();

    // process domains until all rays are terminated
    bool all_done = false;
    int nqueue = 0;
    std::set<int> doms_to_send;
    int lastInstance = -1;
    // gvt::render::data::domain::AbstractDomain* dom = NULL;

    gvt::render::actor::RayVector moved_rays;
    moved_rays.reserve(1000);

    int instTarget = -1;
    size_t instTargetCount = 0;

    gvt::render::Adapter *adapter = 0;
    do {

      do {
        // process domain with most rays queued
        instTarget = -1;
        instTargetCount = 0;

        t_sort.resume();

        for (auto &q : queue) {
          const bool inRank = mpiInstanceMap[q.first] == mpi.rank;
          if (inRank && q.second.size() > instTargetCount) {
            instTargetCount = q.second.size();
            instTarget = q.first;
          }
        }
        t_sort.stop();

        //std::cerr << " instTarget " << instTarget << std::endl;
        if (instTarget >= 0) {

          t_adapter.resume();
          //std::cout << " domaintracer: create empty adapter shared pointer" << std::endl;
          std::shared_ptr<gvt::render::Adapter> adapter = 0;

          //std::cout << " domaintracer: grab mesh shared pointer " << std::endl;
          std::shared_ptr<gvt::render::data::primitives::Data> mesh = meshRef[instTarget];

          auto it = adapterCache.find(mesh);
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
#ifdef GVT_RENDER_ADAPTER_MANTA
            case gvt::render::adapter::Manta:
              adapter = std::make_shared<gvt::render::adapter::manta::data::MantaMeshAdapter>(mesh.get());
              break;
#endif
#ifdef GVT_RENDER_ADAPTER_OPTIX
            case gvt::render::adapter::Optix:
              adapter = std::make_shared<gvt::render::adapter::optix::data::OptixMeshAdapter>(mesh.get());
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
#if defined(GVT_RENDER_ADAPTER_OPTIX) && defined(GVT_RENDER_ADAPTER_EMBREE)
            case gvt::render::adapter::Heterogeneous:
              adapter =
                  std::make_shared<gvt::render::adapter::heterogeneous::data::HeterogeneousMeshAdapter>(mesh.get());
              break;
#endif
            default:
              GVT_ERR_MESSAGE("domain scheduler: unknown adapter type: " << adapterType);
            }

            adapterCache[mesh] = adapter;
          }
          t_adapter.stop();
          GVT_ASSERT(adapter != nullptr, "domain scheduler: adapter not set");
          // end getAdapterFromCache concept

          {
            t_trace.resume();

            gc_rays.add(this->queue[instTarget].size());
            moved_rays.reserve(this->queue[instTarget].size() * 10);
            adapter->trace(this->queue[instTarget], moved_rays, instM[instTarget].get(), instMinv[instTarget].get(),
                           instMinvN[instTarget].get(), lights);

            this->queue[instTarget].clear();
            t_trace.stop();
          }

          t_shuffle.resume();
          gc_shuffle.add(moved_rays.size());
          shuffleRays(moved_rays, instTarget);
          moved_rays.clear();
          t_shuffle.stop();
        }
      } while (instTarget != -1);

      {
        t_send.resume();
        // done with current domain, send off rays to their proper processors.
        SendRays(gc_sent);
        // are we done?

        // root proc takes empty flag from all procs
        int not_done = 0;

        for (auto &q : queue) not_done += q.second.size();

        int *empties = (mpi.rank == 0) ? new int[mpi.world_size] : NULL;
        MPI_Gather(&not_done, 1, MPI_INT, empties, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (mpi.rank == 0) {
          not_done = 0;
          for (size_t i = 0; i < mpi.world_size; ++i) not_done += empties[i];
          for (size_t i = 0; i < mpi.world_size; ++i) empties[i] = not_done;
        }

        MPI_Scatter(empties, 1, MPI_INT, &not_done, 1, MPI_INT, 0, MPI_COMM_WORLD);

        all_done = (not_done == 0);
        t_send.stop();
        delete[] empties;
      }
    } while (!all_done);

    // add colors to the framebuffer

    t_gather.resume();
    this->gatherFramebuffers(this->rays_end - this->rays_start);
    t_gather.stop();
    t_frame.stop();
    t_all = t_sort + t_trace + t_shuffle + t_gather + t_adapter + t_filter + t_send;
    t_diff = t_frame - t_all;

    gc_filter.print();
    gc_shuffle.print();
    gc_rays.print();
    gc_sent.print();
  }

  inline bool SendRays(gvt::util::global_counter &counter) {
    int *outbound = new int[2 * mpi.world_size];
    int *inbound = new int[2 * mpi.world_size];
    MPI_Request *reqs = new MPI_Request[2 * mpi.world_size];
    MPI_Status *stat = new MPI_Status[2 * mpi.world_size];
    unsigned char **send_buf = new unsigned char *[mpi.world_size];
    unsigned char **recv_buf = new unsigned char *[mpi.world_size];
    int *send_buf_ptr = new int[mpi.world_size];

    // if there is only one rank we dont need to go through this routine.
    if (mpi.world_size < 2) return false;
    // init bufs
    for (size_t i = 0; i < 2 * mpi.world_size; ++i) {
      inbound[i] = outbound[i] = 0;
      reqs[i] = MPI_REQUEST_NULL;
    }

    // count how many rays are to be sent to each neighbor
    for (auto &q : queue) {
      // n is the rank this vector of rays (q.second) belongs on.
      size_t n = mpiInstanceMap[q.first]; // bds
      if (n != mpi.rank) {                // bds if instance n is not this rank send rays to it.
        int n_ptr = 2 * n;
        int buf_size = 0;

        outbound[n_ptr] += q.second.size(); // outbound[n_ptr] has number of rays going

        for (size_t r = 0; r < q.second.size(); ++r) {
          buf_size += (q.second)[r].packedSize(); // rays can have diff packed sizes
        }
        outbound[n_ptr + 1] += buf_size;    // size of buffer needed to hold rays
        outbound[n_ptr + 1] += sizeof(int); // bds add space for the queue number
        outbound[n_ptr + 1] += sizeof(int); // bds add space for the number of rays in queue
      }
    }

    // let the neighbors know what's coming
    // and find out what's coming here

    int tag = 0;
    for (size_t n = 0; n < mpi.world_size; ++n) // bds sends to self?
      MPI_Irecv(&inbound[2 * n], 2, MPI_INT, n, tag, MPI_COMM_WORLD, &reqs[2 * n]);
    for (size_t n = 0; n < mpi.world_size; ++n) // bds send to self
      MPI_Isend(&outbound[2 * n], 2, MPI_INT, n, tag, MPI_COMM_WORLD, &reqs[2 * n + 1]);

    MPI_Waitall(2 * mpi.world_size, reqs, stat);

    // set up send and recv buffers
    for (size_t i = 0, j = 0; i < mpi.world_size; ++i, j += 2) {
      send_buf_ptr[i] = 0;
      if (outbound[j] > 0)
        send_buf[i] = new unsigned char[outbound[j + 1]];
      else
        send_buf[i] = 0;
      if (inbound[j] > 0)
        recv_buf[i] = new unsigned char[inbound[j + 1]];
      else
        recv_buf[i] = 0;
    }
    for (size_t i = 0; i < 2 * mpi.world_size; ++i) reqs[i] = MPI_REQUEST_NULL;

    //  ************************ post non-blocking receive *********************
    tag = tag + 1;
    for (size_t n = 0; n < mpi.world_size; ++n) { // bds loop through all ranks
      if (inbound[2 * n] > 0) {
        MPI_Irecv(recv_buf[n], inbound[2 * n + 1], MPI_UNSIGNED_CHAR, n, tag, MPI_COMM_WORLD, &reqs[2 * n]);
      }
    }
    // ******************** pack the send buffers *********************************
    // gvt::core::Vector<int> to_del;
    // for (gvt::core::Map<int, gvt::render::actor::RayVector>::iterator q = queue.begin(); q != queue.end(); ++q) {
    for (auto &q : queue) {
      int n = mpiInstanceMap[q.first]; // bds use instance map
      if (outbound[2 * n] > 0) {
        *((int *)(send_buf[n] + send_buf_ptr[n])) = q.first;         // bds load queue number into send buffer
        send_buf_ptr[n] += sizeof(int);                              // bds advance pointer
        *((int *)(send_buf[n] + send_buf_ptr[n])) = q.second.size(); // bds load number of rays into send buffer
        send_buf_ptr[n] += sizeof(int);                              // bds advance pointer
        for (size_t r = 0; r < q.second.size(); ++r) {               // load the rays in this queue
          gvt::render::actor::Ray ray = (q.second)[r];
          send_buf_ptr[n] += ray.pack(send_buf[n] + send_buf_ptr[n]);
        }
        // to_del.push_back(q->first);
        q.second.clear();
      }
    }
    for (size_t n = 0; n < mpi.world_size; ++n) { // bds loop over all
      counter.add(outbound[2 * n + 1]);
      if (outbound[2 * n] > 0) {
        MPI_Isend(send_buf[n], outbound[2 * n + 1], MPI_UNSIGNED_CHAR, n, tag, MPI_COMM_WORLD, &reqs[2 * n + 1]);
      }
    }

    MPI_Waitall(2 * mpi.world_size, reqs, stat); // XXX TODO refactor to use Waitany?

    // ******************* unpack rays into the queues **************************
    for (int n = 0; n < mpi.world_size; ++n) { // bds loop over all
      if (inbound[2 * n] > 0) {
        int ptr = 0;
        while (ptr < inbound[2 * n + 1]) {
          int q_number = *((int *)(recv_buf[n] + ptr)); // bds get queue number
          ptr += sizeof(int);
          int raysinqueue = *((int *)(recv_buf[n] + ptr)); // bds get rays in this queue
          ptr += sizeof(int);
          for (int c = 0; c < raysinqueue; ++c) {
            gvt::render::actor::Ray r(recv_buf[n] + ptr);
            queue[q_number].push_back(r);
            ptr += r.packedSize();
          }
        }
      }
    }

    // clean up
    for (size_t i = 0; i < mpi.world_size; ++i) {
      delete[] send_buf[i];
      delete[] recv_buf[i];
    }
    delete[] send_buf_ptr;
    delete[] send_buf;
    delete[] recv_buf;
    delete[] inbound;
    delete[] outbound;
    delete[] reqs;
    delete[] stat;
    return false;
  }
};
} // namespace algorithm
} // namespace render
} // namespace gvt
#endif /* GVT_RENDER_ALGORITHM_DOMAIN_TRACER_H */
