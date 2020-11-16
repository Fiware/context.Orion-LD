/*
*
* Copyright 2019 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Ken Zangelin
*/
extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "orionld/common/CHECK.h"                                // CHECKx()
#include "orionld/common/SCOMPARE.h"                             // SCOMPAREx
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldErrorResponse.h"                 // orionldErrorResponseCreate
#include "orionld/types/OrionldGeoLocation.h"                    // OrionldGeoLocation
#include "orionld/payloadCheck/pcheckGeoPropertyValue.h"         // pcheckGeoPropertyValue
#include "orionld/kjTree/kjTreeToGeoLocation.h"                  // Own Interface



// -----------------------------------------------------------------------------
//
// kjTreeToGeoLocation -
//
bool kjTreeToGeoLocation(KjNode* geoLocationNodeP, OrionldGeoLocation* locationP)
{
  char*    geoType;
  KjNode*  geoCoordsP;

  if (pcheckGeoPropertyValue(geoLocationNodeP, &geoType, &geoCoordsP) == false)
  {
    LM_E(("pcheckGeoPropertyValue failed"));
    // pcheckGeoPropertyValue sets the Error Response
    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // Now the type and cooordinates of the GeoJSON is in 'orionldState.geoTypeP' and 'orionldState.geoCoordsP'
  //
  locationP->geoType     = geoType;
  locationP->coordsNodeP = geoCoordsP;

  return true;
}
