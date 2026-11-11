#include "street_heading.h"
#include <math.h>
bool street_heading_transfer(float old_az,float old_yaw,float new_az,float *out){
  if(!out||!isfinite(old_az)||!isfinite(old_yaw)||!isfinite(new_az))return false;
  const float d=.017453292519943295f;
  *out=remainderf(old_yaw+(old_az-new_az)*d,6.283185307179586f);
  return isfinite(*out);
}
