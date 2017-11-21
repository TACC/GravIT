// ======================================================================== //
// Copyright 2009-2016 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "ospray/render/Renderer.h"

namespace ospray {

  //! \brief A concrete implemetation of the Renderer class for rendering
  //!  volumes optionally containing embedded surfaces.
  //!
  class PTracerRenderer : public Renderer {

  public:

    //! Constructor.
    PTracerRenderer();

    //! Destructor.
    ~PTracerRenderer();

    //! Initialize the renderer state, and create the equivalent ISPC volume
    //! renderer object.
    void commit();

    //! A string description of this class.
    std::string toString() const;

#if EXP_DATA_PARALLEL
    /*! per-frame data to describe the data-parallel components */
    void renderFrame(FrameBuffer *fb, const uint32 channelFlags);
#endif

	OSPExternalRays traceExternalRays(OSPExternalRays raysIn);
void foo(){}

  private:

    //! Error checking.
    void exitOnCondition(bool condition, const std::string &message) const;

		float ao_radius;
		int do_shadows;
		int n_ao_rays;
		float Ka, Kd;
		int numLights;
		int rank;
		float epsilon;
  };

  // Inlined function definitions /////////////////////////////////////////////

  inline PTracerRenderer::PTracerRenderer()
  {
  }

  inline PTracerRenderer::~PTracerRenderer()
  {
  }

  inline std::string PTracerRenderer::toString() const
  {
    return("ospray::PTracerRenderer");
  }

  inline void
  PTracerRenderer::exitOnCondition(bool condition,
                                         const std::string &message) const
  {
    if (!condition)
      return;
    exit(1);
  }

} // ::ospray
