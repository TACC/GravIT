/* ========================================================================== //
 * This file is released as part of GraviT2 - scalable, platform independent  //
 * ray tracing tacc.github.io/GraviT2                                         //
 *                                                                            //
 * Copyright (c) 2013-2019 The University of Texas at Austin.                 //
 * All rights reserved.                                                       //
 *                                                                            //
 * Licensed under the Apache License, Version 2.0 (the "License");            //
 * you may not use this file except in compliance with the License.           //
 * A copy of the License is included with this software in the file LICENSE.  //
 * If your copy does not contain the License, you may obtain a copy of the    //
 * License at:                                                                //
 *                                                                            //
 *     https://www.apache.org/licenses/LICENSE-2.0                            //
 *                                                                            //
 * Unless required by applicable law or agreed to in writing, software        //
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT  //
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.           //
 * See the License for the specific language governing permissions and        //
 * limitations under the License.                                             //
 * ========================================================================== */

#include "Ray.h"
using namespace gvt2;
Ray::Ray(const vec3f& _origin, const vec3f& _direction) {
    origin = vec3f(_origin);
    direction = vec3f(_direction);
    t_min = FLT_MIN;
    t_max = FLT_MAX;
    t = t_min;
    id = 0;
    type = 0;
}