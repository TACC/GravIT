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

#ifndef GVT_RENDER_ADAPTER_EMBREE_DATA_TRANSFORMS_H
#define GVT_RENDER_ADAPTER_EMBREE_DATA_TRANSFORMS_H

#include <gvt/core/data/Transform.h>

#include <gvt/core/Math.h>
#include <gvt/render/actor/Ray.h>
#include <gvt/render/data/Primitives.h>

#include <vector>

#include <embree2/rtcore.h>
#include <embree2/rtcore_ray.h>

namespace gvt {
namespace render {
namespace adapter {
namespace embree {
namespace data {
GVT_TRANSFORM_TEMPLATE; // see gvt/core/data/Transform.h

/// return an Embree-compliant point
template <> struct transform_impl<float[3], gvt::core::math::Point4f> {
  static inline gvt::core::math::Vector4f transform(const float r[3]) {
    return gvt::core::math::Point4f(r[0], r[1], r[2], 1.f);
  }
};

/// return an Embree-compliant vector
template <> struct transform_impl<float[3], gvt::core::math::Vector4f> {
  static inline gvt::core::math::Vector4f transform(const float r[3]) {
    return gvt::core::math::Vector4f(r[0], r[1], r[2], 0.f);
  }
};

/// return an Embree-compliant ray from a GraviT ray
template <> struct transform_impl<gvt::render::actor::Ray, RTCRay> {
  static inline RTCRay transform(const gvt::render::actor::Ray &r) {
    RTCRay ray;
    ray.org[0] = r.origin[0];
    ray.org[1] = r.origin[1];
    ray.org[2] = r.origin[2];
    ray.dir[0] = r.direction[0];
    ray.dir[1] = r.direction[1];
    ray.dir[2] = r.direction[2];
    return ray;
  }
};

/// return a GraviT-compliant ray from an Embree ray
template <> struct transform_impl<RTCRay, gvt::render::actor::Ray> {
  static inline gvt::render::actor::Ray transform(const RTCRay &r) {
    gvt::core::math::Point4f o(r.org[0], r.org[1], r.org[2], 1);
    gvt::core::math::Vector4f d(r.dir[0], r.dir[1], r.dir[2], 0);

    return gvt::render::actor::Ray(
        gvt::render::adapter::embree::data::transform<float[3], gvt::core::math::Point4f>(r.org),
        gvt::render::adapter::embree::data::transform<float[3], gvt::core::math::Vector4f>(r.dir));
  }
};
}
}
}
}
}

#endif /* GVT_RENDER_ADAPTER_EMBREE_DATA_TRANSFORMS_H */
