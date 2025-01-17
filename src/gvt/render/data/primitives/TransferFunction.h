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
#ifndef GVT_RENDER_DATA_PRIMITIVES_TRANSFERFUNCTION_H
#define GVT_RENDER_DATA_PRIMITIVES_TRANSFERFUNCTION_H

#include "ospray/ospray.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace gvt {
namespace render {
namespace data {
namespace primitives {

class TransferFunction {
public:
  TransferFunction();
  ~TransferFunction();
  void load(std::string cname, std::string oname);
  float *getOpacity() { return opacity; }
  glm::vec2 *getOpacityMap() { return opacitymap; }
  int getOpacityCount() { return n_opacities; }
  glm::vec3 *getColors() { return color; }
  glm::vec4 *getColorMap() { return colormap; }
  int getColorCount() { return n_colors; }
  OSPTransferFunction GetTheOSPTransferFunction() { return theOSPTransferFunction; }
  void setValueRange(glm::vec2 range) { valueRange = range; }
  glm::vec2 getValueRange() {return valueRange;} 
  bool set();

protected:
  bool DeviceCommit();
  glm::vec4 *colormap;
  glm::vec2 *opacitymap;
  glm::vec3 color[256];
  glm::vec2 valueRange;
  float opacity[256];
  int n_colors, n_opacities;
  OSPTransferFunction theOSPTransferFunction;
};
} // namespace primitives
} // namespace data
} // namespace render
} // namespace gvt
#endif
