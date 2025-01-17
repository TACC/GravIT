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

#ifndef GVT_RENDER_ADAPTER_EMBREE_DATA_EMBREE_MESH_ADAPTER_H
#define GVT_RENDER_ADAPTER_EMBREE_DATA_EMBREE_MESH_ADAPTER_H

#include "gvt/render/Adapter.h"

#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>

namespace gvt {
namespace render {
namespace adapter {
namespace embree {
namespace data {
/// mesh adapter for Intel Embree ray tracer
/** this helper class transforms mesh data from the GraviT internal format
to the format expected by Intel's Embree ray tracer
*/
class EmbreeMeshAdapter : public gvt::render::Adapter {
public:
  /**
   * Construct the Embree mesh adapter.  Convert the mesh
   * at the given node to Embree's format.
   *
   * Initializes Embree the first time it is called.
   */
  EmbreeMeshAdapter(std::shared_ptr<gvt::render::data::primitives::Data> mesh);

  /**
   * Release Embree copy of the mesh.
   */
  virtual ~EmbreeMeshAdapter();

  virtual void trace(gvt::render::actor::RayVector &rayList, gvt::render::actor::RayVector &moved_rays, glm::mat4 *m,
                     glm::mat4 *minv, glm::mat3 *normi, gvt::core::Vector<std::shared_ptr<gvt::render::data::scene::Light> > &lights,
                     size_t begin = 0, size_t end = 0);

  /**
   * Handle to Embree scene.
   */
  RTCScene global_scene;

protected:
  RTCDevice device;

  /**
   * Handle to Embree scene.
   */
  RTCScene scene;

  /**
   * Handle to the Embree triangle mesh.
   */
  unsigned geomId;
  unsigned instID;

  size_t begin, end;
};
}
}
}
}
}

#endif // GVT_RENDER_ADAPTER_EMBREE_DATA_EMBREE_MESH_ADAPTER_H
